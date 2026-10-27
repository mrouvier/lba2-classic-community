#include "SEA_FX.H"
#include "C_EXTERN.H"      /* FirstTime: a change of look redraws the scene whole */
#include "3DEXT/VAR_EXT.H" /* SkySeaTexture, the sea's page */
#include "SETTINGS.H"

#include <POLYGON/POLY.H>     /* Fill_Poly, Switch_Fillers, PtrMap, RepMask */
#include <POLYGON/POLYFILT.H> /* PolyTexFilterNZW: the refill keeps the texture filter */
#include <SVGA/CLIP.H>        /* ClipXMin.. ClipYMax, UnsetClip */
#include <SVGA/DIRTYBOX.H>    /* BoxStaticAdd, BoxBlitStaticListToScreen */
#include <SVGA/SCREEN.H>      /* Log, Screen, ModeDesiredX/Y */
#include <SYSTEM/TIMER.H>     /* TimerRefHR, the current's clock */

#include <stdlib.h>
#include <string.h>

/* See SEA_FX.H. */

S32 SeaMotion = 0;

/* A change of look redraws the scene whole: the kept quads, the offset and the
   page all come from a full redraw. */
static void SeaFx_Changed(S32 value) {
    (void)value;
    FirstTime = AFF_ALL_FLIP;
}

static const T_SETTING SeaFxSettings[] = {
    /* Pixels only: the offset comes from the game clock, the refill reads the
       depths and writes none, nothing draws from the shared random stream. */
    {"SeaMotion", NULL, "gfx_sea", &SeaMotion, SETTING_OR_DEFAULT, 0, 0, 2, NULL, NULL, SeaFx_Changed, NULL,
     "Sea: 0 the 1997 still sea, 1 moving with a slow current, 2 moving with chop", SETTING_NOT_RECORDED,
     SETTING_BEFORE(0)},
};
#define SEA_FX_SETTING_COUNT ((S32)(sizeof(SeaFxSettings) / sizeof(SeaFxSettings[0])))

/* The quads the last full redraw filled, as Draw_Poly projected and clipped
   them, with their texture coordinates before the offset. A cube is 16 quads
   and the horizon draws its neighbours', so a few hundred at most; past the
   pool the refill stands down until the next full redraw rather than move part
   of the sea. Draw_Poly's clip keeps at most 10 corners. */
#define SEA_FX_MAX_QUADS 512
#define SEA_FX_MAX_CORNERS 10
typedef struct {
    Struc_Point pts[SEA_FX_MAX_CORNERS];
    S32 n;
} T_SEA_QUAD;
static T_SEA_QUAD s_quads[SEA_FX_MAX_QUADS];
static S32 s_count = 0;
static S32 s_overflow = 0;

/* The offset and chop phase the sea was last filled with, by the full redraw
   or a refill; a refill with nothing new to show does nothing. */
static S32 s_originU = 0, s_originV = 0;
static S32 s_filledPhase = -1;

/* The choppy page, laid out as SkySeaTexture is (256 bytes a row, the sea in
   the first 128 columns), and the chop phase it was composed at. */
static U8 s_page[SEA_FX_PAGE * 256];
static S32 s_pagePhase = -1;

/* Pixels a background object drew over the background on the last full
   redraw, which the refill puts back: a byte a pixel, set inside the boxes
   below and cleared there at the next full redraw. */
#define SEA_FX_MAX_BAKED 64
typedef struct {
    S32 x0, y0, x1, y1;
} T_SEA_BOX;
static U8 *s_keep = NULL;
static S32 s_keepPitch = 0, s_keepRows = 0;
static T_SEA_BOX s_baked[SEA_FX_MAX_BAKED];
static S32 s_bakedCount = 0;

static void ComposePage(S32 phase) {
    const U8 *src = SkySeaTexture;
    const S32 m = SeaFx_MeanStep(src, 256);
    S32 u, v;

    for (v = 0; v < SEA_FX_PAGE; v++) {
        const U8 *row = src + v * 256;
        U8 *out = s_page + v * 256;
        for (u = 0; u < SEA_FX_PAGE; u++)
            out[u] = SeaFx_Mix(row[u], src[SeaFx_FineTexel(u, v, phase)], m);
    }
    s_pagePhase = phase;
}

/* Set the offset and the page for game time t; answers whether either moved
   since the sea was last filled. */
static S32 SetFrame(U32 t) {
    const S32 u = SeaFx_Origin(t, SEA_FX_DRIFT_U);
    const S32 v = SeaFx_Origin(t, SEA_FX_DRIFT_V);
    const S32 phase = (SeaMotion >= 2) ? SeaFx_ChopPhase(t) : -1;
    const S32 moved = u != s_originU || v != s_originV || phase != s_filledPhase;

    s_originU = u;
    s_originV = v;
    s_filledPhase = phase;
    if (phase >= 0 && phase != s_pagePhase)
        ComposePage(phase);
    return moved;
}

static void ClearBaked(void) {
    S32 i, y;
    for (i = 0; i < s_bakedCount; i++) {
        const T_SEA_BOX *b = &s_baked[i];
        for (y = b->y0; y <= b->y1; y++)
            memset(s_keep + y * s_keepPitch + b->x0, 0, (size_t)(b->x1 - b->x0 + 1));
    }
    s_bakedCount = 0;
}

void SeaFx_FrameBegin(void) {
    s_count = 0;
    s_overflow = 0;
    if (s_keep != NULL)
        ClearBaked();
    if (SeaMotion)
        (void)SetFrame(TimerRefHR);
}

U8 *SeaFx_Map(U8 *skySea) {
    return (SeaMotion >= 2 && s_pagePhase >= 0) ? s_page : skySea;
}

void SeaFx_Quad(Struc_Point *pts, S32 n) {
    S32 i;

    if (!SeaMotion)
        return;

    if (n <= SEA_FX_MAX_CORNERS && s_count < SEA_FX_MAX_QUADS) {
        memcpy(s_quads[s_count].pts, pts, (size_t)n * sizeof(Struc_Point));
        s_quads[s_count].n = n;
        s_count++;
    } else {
        s_overflow = 1;
    }

    for (i = 0; i < n; i++) {
        pts[i].Pt_MapU = SeaFx_Shift(pts[i].Pt_MapU, s_originU);
        pts[i].Pt_MapV = SeaFx_Shift(pts[i].Pt_MapV, s_originV);
    }
}

void SeaFx_Baked(S32 x0, S32 y0, S32 x1, S32 y1) {
    const S32 pitch = (S32)ScreenPitch;
    const S32 rows = (S32)ModeDesiredY;

    if (!SeaMotion || !s_count)
        return;

    if (s_keep == NULL || s_keepPitch != pitch || s_keepRows != rows) {
        free(s_keep);
        s_keep = (U8 *)calloc((size_t)pitch * (size_t)rows, 1);
        s_keepPitch = pitch;
        s_keepRows = rows;
        s_bakedCount = 0;
        if (s_keep == NULL) {
            s_overflow = 1; /* nowhere to note the object: the sea stands still */
            return;
        }
    }
    if (s_bakedCount >= SEA_FX_MAX_BAKED) {
        s_overflow = 1;
        return;
    }

    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 >= pitch)
        x1 = pitch - 1;
    if (y1 >= rows)
        y1 = rows - 1;
    if (x1 < x0 || y1 < y0)
        return;

    SeaFx_NoteDrawn((const U8 *)Log, (const U8 *)Screen, s_keep, pitch, x0, y0, x1, y1);
    s_baked[s_bakedCount].x0 = x0;
    s_baked[s_bakedCount].y0 = y0;
    s_baked[s_bakedCount].x1 = x1;
    s_baked[s_bakedCount].y1 = y1;
    s_bakedCount++;
}

void SeaFx_Refill(void) {
    PTR_U8 saveMap = PtrMap;
    const S32 saveRep = RepMask;
    Struc_Point pts[SEA_FX_MAX_CORNERS];
    S32 bx0 = 0x7FFFFFFF, by0 = 0x7FFFFFFF, bx1 = -1, by1 = -1;
    S32 q, i;

    if (!SeaMotion || !s_count || s_overflow)
        return;

    if (!SetFrame(TimerRefHR))
        return;

    Switch_Fillers(FILL_POLY_FOG_NZW);
    PolyTexFilterNZW = 1;
    PtrMap = SeaFx_Map(SkySeaTexture);
    RepMask = 0x7F7F;
    UnsetClip();

    for (q = 0; q < s_count; q++) {
        const T_SEA_QUAD *quad = &s_quads[q];
        for (i = 0; i < quad->n; i++) {
            pts[i] = quad->pts[i];
            pts[i].Pt_MapU = SeaFx_Shift(pts[i].Pt_MapU, s_originU);
            pts[i].Pt_MapV = SeaFx_Shift(pts[i].Pt_MapV, s_originV);
            if (pts[i].Pt_XE < bx0)
                bx0 = pts[i].Pt_XE;
            if (pts[i].Pt_XE > bx1)
                bx1 = pts[i].Pt_XE;
            if (pts[i].Pt_YE < by0)
                by0 = pts[i].Pt_YE;
            if (pts[i].Pt_YE > by1)
                by1 = pts[i].Pt_YE;
        }
        Fill_Poly(POLY_TEXTURE_Z_FOG, 0, quad->n, pts);
    }

    PolyTexFilterNZW = 0;
    PtrMap = saveMap;
    RepMask = saveRep;
    Switch_Fillers(FILL_POLY_TEXTURES);

    if (bx0 < ClipXMin)
        bx0 = ClipXMin;
    if (by0 < ClipYMin)
        by0 = ClipYMin;
    if (bx1 > ClipXMax)
        bx1 = ClipXMax;
    if (by1 > ClipYMax)
        by1 = ClipYMax;
    if (bx1 < bx0 || by1 < by0)
        return;

    /* The background objects baked over the sea: their pixels, still in the
       background, go back into the frame before the frame goes there. */
    for (q = 0; q < s_bakedCount; q++) {
        const T_SEA_BOX *b = &s_baked[q];
        const S32 x0 = (b->x0 > bx0) ? b->x0 : bx0, x1 = (b->x1 < bx1) ? b->x1 : bx1;
        const S32 y0 = (b->y0 > by0) ? b->y0 : by0, y1 = (b->y1 < by1) ? b->y1 : by1;
        if (x0 <= x1 && y0 <= y1)
            SeaFx_PutBack((U8 *)Log, (const U8 *)Screen, s_keep, s_keepPitch, x0, y0, x1, y1);
    }

    BoxStaticAdd(bx0, by0, bx1, by1);
    BoxBlitStaticListToScreen();
}

void SeaFx_ReadConfig(void) {
    Settings_ReadConfig(SeaFxSettings, SEA_FX_SETTING_COUNT);
}

void SeaFx_WriteConfig(void) {
    Settings_WriteConfig(SeaFxSettings, SEA_FX_SETTING_COUNT);
}

void SeaFx_RegisterCvars(void) {
    Settings_RegisterCvars(SeaFxSettings, SEA_FX_SETTING_COUNT);
}

const T_SETTING *SeaFx_Settings(S32 *count) {
    *count = SEA_FX_SETTING_COUNT;
    return SeaFxSettings;
}
