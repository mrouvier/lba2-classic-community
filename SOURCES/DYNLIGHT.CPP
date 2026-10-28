#include "DYNLIGHT.H"
#include "C_EXTERN.H" /* FirstTime, CubeMode, ListExtra, ListObjet, PtrPalCurrent, IdxPalette, sprites */
#include "FLOW.H"     /* ListPartFlow: the magic ball's burst */
#include "GRILLE.H"   /* BufferMaskBrick, GrilleBrickDrawn: an interior's bricks */
#include "MESSAGE.H"  /* FlagAnimWhoSpeak: the extras the scene skips while someone speaks */
#include "SETTINGS.H"
#include "3DEXT/VAR_EXT.H" /* ListDecors, MapIntensity, HQR_Isle_Obj, FogCoul, ClipZFar; T_DECORS by C_EXTERN.H */

#include <3D/CAMERA.H>     /* CameraXr.., LFactorX/Y, XCentre/YCentre, NearClip */
#include <3D/DATAMAT.H>    /* MatriceWorld */
#include <POLYGON/POLY.H>  /* PtrZBuffer, Fill_Z_Fog_Near/Far */
#include <SVGA/CLIP.H>     /* ClipWindowXMin.. */
#include <SVGA/DIRTYBOX.H> /* BoxMovingAdd */
#include <SVGA/SCREEN.H>   /* Log, Screen, ModeDesiredX/Y, TabOffLine */
#include <SVGA/VIDEO.H>    /* Phys, ModeResX, TabOffPhysLine */
#include <SYSTEM/HQRRESS.H>
#include <SYSTEM/LE.H>
#include <SYSTEM/TIMER.H> /* TimerRefHR: the extras' blink */

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* See DYNLIGHT.H. */

S32 DynLight = 0;

/* Console only: draw a cross at each light found (1). */
static S32 DynLightDebug = 0;

/* A change of light redraws the scene whole: the static pools are baked into
   the background on a full redraw. */
static void DynLight_Changed(S32 value) {
    (void)value;
    FirstTime = AFF_ALL_FLIP;
}

static const T_SETTING DynLightSettings[] = {
    /* Pixels only: the passes read the scene and write the frame, never game
       state, and draw nothing from the shared random stream. */
    {"DynLight", NULL, "gfx_dynlight", &DynLight, SETTING_OR_DEFAULT, 0, 0, 2, NULL, NULL, DynLight_Changed, NULL,
     "Lighting: 0 the 1997 light, 1 dynamic lights in the palette, 2 and colour at present time",
     SETTING_NOT_RECORDED, SETTING_BEFORE(0)},
    {NULL, NULL, "gfx_dynlight_debug", &DynLightDebug, SETTING_OR_DEFAULT, 0, 0, 1, NULL, NULL, DynLight_Changed,
     NULL, "Lighting: 1 marks each light found with a cross", SETTING_NOT_RECORDED, SETTING_BEFORE(0)},
};
#define DYN_LIGHT_SETTING_COUNT ((S32)(sizeof(DynLightSettings) / sizeof(DynLightSettings[0])))

void DynLight_ReadConfig(void) {
    Settings_ReadConfig(DynLightSettings, DYN_LIGHT_SETTING_COUNT);
}

void DynLight_WriteConfig(void) {
    Settings_WriteConfig(DynLightSettings, DYN_LIGHT_SETTING_COUNT);
}

void DynLight_RegisterCvars(void) {
    Settings_RegisterCvars(DynLightSettings, DYN_LIGHT_SETTING_COUNT);
}

const T_SETTING *DynLight_Settings(S32 *count) {
    *count = DYN_LIGHT_SETTING_COUNT;
    return DynLightSettings;
}

/*══════════════════════════════════════════════════════════════════════════*
    The lights
 *══════════════════════════════════════════════════════════════════════════*/

/* A light of the frame. Outdoors in camera space: x right, y up, z depth,
   world units, as the exterior projection sees it (Xp = XCentre +
   x*LFactorX/z). Indoors at its world position, the isometric camera being
   a translation (DynLight_IsoProject). */
typedef struct {
    float x, y, z;
    float reach; /* nothing past it */
    float emitR; /* the radius of what gives the light (a globe, a ball), 0 for none */
    S32 level;   /* the pool at the light, sixteenths of a table level */
    S32 halo;    /* level 1: the halo around what gives it, sixteenths */
    S32 glow;    /* level 2: the glow around what gives it, 0..255 */
    S32 tint;    /* level 2: the tint at the light, 0..256 */
    U8 colour;   /* its colour, a palette index */
} T_DYN_LIGHT;

/* The street lamps the last full redraw gathered (their camera holds until
   the next full redraw: a camera move redraws whole), and the moving lights of
   this frame. */
#define DYN_STATIC_LIGHTS 32
#define DYN_MOVING_LIGHTS 48
static T_DYN_LIGHT s_static[DYN_STATIC_LIGHTS];
static S32 s_nbStatic = 0;
static S32 s_gathering = 0; /* from AffGrilleExt's DynLight_FrameBegin to its bake */
static T_DYN_LIGHT s_moving[DYN_MOVING_LIGHTS];
static S32 s_nbMoving = 0;

/* The Z-buffer's nearest and farthest value per block (DynLight_ZBlocks),
   valid from the full redraw that wrote the depths until the next. */
#define DYN_BLOCK 4 /* the side of the depth blocks, DynLight_ZBlocks */
#define DYN_BLOCK_SHIFT 2
static U16 *s_zmin = NULL, *s_zmax = NULL;
static S32 s_bw = 0, s_bh = 0;
static S32 s_zValid = 0;
/* Level 2: the lamps' tint, valid from the full redraw that gathered them. */
static S32 s_tintStaticValid = 0;

/* Interiors (DynLight_Iso*): the background's position per pixel, kept as
   (X + Z) / 2 and the face it shows, rebuilt while an interior's bricks are
   drawn, with the camera and the centre they were drawn with. */
static S32 s_iso = 0;         /* this frame lights an interior */
static U16 *s_isoS = NULL;    /* (X + Z) / 2, DYN_ISO_NONE where no brick shows */
static U8 *s_isoF = NULL;     /* the face, DYN_ISO_TOP.. */
static float *s_isoD = NULL;  /* X - Z of each column's pixels */
static S32 s_isoValid = 0;    /* the frame's background is the one described */
static S32 s_isoBuilding = 0; /* between DynLight_IsoBegin and DynLight_IsoEnd */
static S32 s_isoCamX, s_isoCamY, s_isoCamZ, s_isoXC, s_isoYC;
static T_DYN_BOX s_isoClip;  /* the clip the bricks were drawn in */
static S32 s_isoScale = 256; /* how strong the lights are in this room, 256ths */

/* A street lamp: its pool reaches one and a half times the globe's height, so
   the ground under it takes a third of the light and the walls about it more;
   its globe is about the decor's width across. */
#define DYN_LAMP_LEVEL 224 /* fourteen table levels at the globe */
#define DYN_LAMP_REACH_MIN 2048
#define DYN_LAMP_REACH_MAX 8192
#define DYN_LAMP_HALO 160
#define DYN_LAMP_GLOW 200
#define DYN_LAMP_TINT 256

/* The halo (level 1) and the glow (level 2) reach so many emitter radii out on
   screen; past a quarter of the screen they stop growing. */
#define DYN_HALO_SCALE 4
#define DYN_GLOW_SCALE 5

/* By day the moving lights give half: the palette pixel they brighten is
   already bright. Night is a dark fog colour, as the storm's and the moon's. */
#define DYN_DAY_SCALE 128 /* 256ths */

static S32 DynLight_Night(void) {
    return PtrPalCurrent != NULL && DynLight_Luma(PtrPalCurrent + 3 * (FogCoul & 0xFF)) < 64;
}

/* From cube-local world to camera space, with the camera in place. Its own
   arithmetic: the engine's rotate and project helpers write the shared X0/Xp
   scratch and the projection capture. */
static void DynLight_ToCam(S32 wx, S32 wy, S32 wz, T_DYN_LIGHT *l) {
    const float x = (float)wx, y = (float)wy, z = (float)wz;
    l->x = x * MatriceWorld.F.M11 + y * MatriceWorld.F.M12 + z * MatriceWorld.F.M13 - (float)CameraXr;
    l->y = x * MatriceWorld.F.M21 + y * MatriceWorld.F.M22 + z * MatriceWorld.F.M23 - (float)CameraYr;
    l->z = (float)CameraZr - (x * MatriceWorld.F.M31 + y * MatriceWorld.F.M32 + z * MatriceWorld.F.M33);
}

/*──────────────────────────────────────────────────────────────────────────*
    Street lamps, found once per cube per island
 *──────────────────────────────────────────────────────────────────────────*/

#define DYN_LAMPS_PER_CUBE 16
typedef struct {
    S16 dec;          /* its index in the cube's decor list */
    S32 x, y, z;      /* the globe, cube-local */
    S32 reach, emitR; /* world units */
    U8 colour;        /* the globe's */
} T_DYN_LAMP;
typedef struct {
    S8 built;
    U8 n;
    T_DYN_LAMP lamp[DYN_LAMPS_PER_CUBE];
} T_DYN_CUBE_LAMPS;
static T_DYN_CUBE_LAMPS s_cubeLamps[MAX_CUBES_PER_ISLE];
static S32 s_cube = -1; /* the cube LoadCube set the tables for, -1 for none */

void DynLight_IslandLoaded(void) {
    memset(s_cubeLamps, 0, sizeof(s_cubeLamps));
    s_cube = -1;
}

void DynLight_CubeLoaded(S32 cube) {
    /* Kept even while the light is off: with no horizon, or one cube, LoadCube
       is not run again until the next cube, and turning the light on then has
       to know which cube is drawn. */
    s_cube = (cube >= 0 && cube < MAX_CUBES_PER_ISLE) ? cube : -1;
}

/* The lamps of the cube LoadCube set the tables for, found the first time:
   MapIntensity, ListDecors and NbObjDecors are that cube's while it is drawn.
   What is kept is copied: the HQR cache moves its entries. */
static const T_DYN_CUBE_LAMPS *DynLight_CubeLamps(void) {
    T_DYN_CUBE_LAMPS *c;
    S32 i;
    if (s_cube < 0 || MapIntensity == NULL)
        return NULL;
    c = &s_cubeLamps[s_cube];
    if (c->built)
        return c;
    c->built = 1;
    c->n = 0;
    for (i = 0; i < NbObjDecors && c->n < DYN_LAMPS_PER_CUBE; i++) {
        const T_DECORS *d = &ListDecors[i];
        T_DYN_POOL pool;
        T_DYN_LAMP *l;
        const U8 *body;
        S32 h, colour, w;
        if (!DynLight_LampAt(MapIntensity, d->Xworld, d->Zworld, &pool))
            continue;
        /* The decor's body, from the island's body cache (fetched when it
           was last drawn, or read again: the cache's order moves, no pointer
           the engine holds does). Its offsets are trusted as the renderer
           trusts them. */
        body = (const U8 *)HQR_Get(HQR_Isle_Obj, d->Body & 0xFFFF);
        colour = body ? DynLight_TopColour(body) : -1;
        if (colour < 0)
            continue;
        l = &c->lamp[c->n++];
        l->dec = (S16)i;
        l->x = d->Xworld;
        l->y = d->YMax; /* the globe's centre: the body's YMax leaves its top out */
        l->z = d->Zworld;
        h = d->YMax - d->Yworld;
        l->reach = h + h / 2;
        if (l->reach < DYN_LAMP_REACH_MIN)
            l->reach = DYN_LAMP_REACH_MIN;
        if (l->reach > DYN_LAMP_REACH_MAX)
            l->reach = DYN_LAMP_REACH_MAX;
        w = d->XMax - d->XMin < d->ZMax - d->ZMin ? d->XMax - d->XMin : d->ZMax - d->ZMin;
        l->emitR = w * 7 / 16;
        if (l->emitR < 64)
            l->emitR = 64;
        if (l->emitR > 512)
            l->emitR = 512;
        l->colour = (U8)colour;
    }
    return c;
}

void DynLight_FrameBegin(void) {
    s_nbStatic = 0;
    s_gathering = 1;
    s_zValid = 0;          /* the redraw writes the Z-buffer anew */
    s_tintStaticValid = 0; /* and the lamps light it anew */
}

void DynLight_GatherCube(void) {
    const T_DYN_CUBE_LAMPS *c;
    S32 k;
    /* Only the cubes a full redraw draws: a terrain drawn outside one (the
       debug tools' benchmark) would add its lamps again. */
    if (!DynLight || !s_gathering)
        return;
    c = DynLight_CubeLamps();
    if (c == NULL)
        return;
    for (k = 0; k < c->n && s_nbStatic < DYN_STATIC_LIGHTS; k++) {
        const T_DYN_LAMP *lamp = &c->lamp[k];
        T_DYN_LIGHT *l = &s_static[s_nbStatic];
        /* Hidden by a game variable this frame (AffichageObjetDecorsZBuf). */
        if (lamp->dec >= NbObjDecors || (ListDecors[lamp->dec].Body & DEC_INVISIBLE))
            continue;
        DynLight_ToCam(lamp->x, lamp->y, lamp->z, l);
        if (l->z + lamp->reach <= (float)NearClip || l->z - lamp->reach >= (float)ClipZFar)
            continue;
        l->reach = (float)lamp->reach;
        l->emitR = (float)lamp->emitR;
        l->level = DYN_LAMP_LEVEL;
        l->halo = DYN_LAMP_HALO;
        l->glow = DYN_LAMP_GLOW;
        l->tint = DYN_LAMP_TINT;
        l->colour = lamp->colour;
        s_nbStatic++;
    }
}

/*──────────────────────────────────────────────────────────────────────────*
    Moving lights: the magic ball, its trail, the spells, the fire
 *──────────────────────────────────────────────────────────────────────────*/

/* The colour a sprite gives, per palette: of its own pixels, the one nearest
   the mean of its bright half (those at least half as bright as its
   brightest), so a ball's white highlight does not make a red ball's light
   white; and how bright its brightest pixel is, which a fading fireball's
   light follows. HQR_Get touches the cache's order, so each sprite is read
   once per palette. */
#define DYN_SPRITES 512
static U8 s_sprKnown[DYN_SPRITES];
static U8 s_sprColour[DYN_SPRITES];
static U8 s_sprLuma[DYN_SPRITES];
static S32 s_sprPalette = -1;

static S32 DynLight_SpriteColour(S32 num, S32 *luma) {
    if (num < 0 || num >= DYN_SPRITES || PtrPalCurrent == NULL || HQRPtrSpriteRaw == NULL)
        return -1;
    if (s_sprPalette != IdxPalette) {
        memset(s_sprKnown, 0, sizeof(s_sprKnown));
        s_sprPalette = IdxPalette;
    }
    if (!s_sprKnown[num]) {
        const U8 *bank = (const U8 *)HQR_Get(HQRPtrSpriteRaw, num);
        S32 best = 0, bestL = 0;
        if (bank != NULL) {
            const U8 *spr = bank + ReadLE_U32(bank);
            const S32 n = spr[0] * spr[1];
            const U8 *px = spr + 4;
            S32 i, sr = 0, sg = 0, sb = 0, cnt = 0, bestD = 0x7FFFFFFF;
            for (i = 0; i < n; i++)
                if (px[i] && DynLight_Luma(PtrPalCurrent + 3 * px[i]) > bestL)
                    bestL = DynLight_Luma(PtrPalCurrent + 3 * px[i]);
            for (i = 0; i < n; i++) {
                const U8 *rgb = PtrPalCurrent + 3 * px[i];
                if (!px[i] || 2 * DynLight_Luma(rgb) < bestL)
                    continue;
                sr += rgb[0];
                sg += rgb[1];
                sb += rgb[2];
                cnt++;
            }
            if (cnt) {
                sr /= cnt;
                sg /= cnt;
                sb /= cnt;
                for (i = 0; i < n; i++) {
                    const U8 *rgb = PtrPalCurrent + 3 * px[i];
                    S32 d;
                    if (!px[i] || 2 * DynLight_Luma(rgb) < bestL)
                        continue;
                    d = (rgb[0] - sr) * (rgb[0] - sr) + (rgb[1] - sg) * (rgb[1] - sg) + (rgb[2] - sb) * (rgb[2] - sb);
                    if (d < bestD) {
                        bestD = d;
                        best = px[i];
                    }
                }
            }
        }
        s_sprKnown[num] = 1;
        s_sprColour[num] = (U8)best;
        s_sprLuma[num] = (U8)bestL;
    }
    *luma = s_sprLuma[num];
    return s_sprKnown[num] && s_sprLuma[num] ? s_sprColour[num] : -1;
}

typedef struct {
    S32 reach, level, halo, emitR, glow, tint;
} T_DYN_KIND;
static const T_DYN_KIND DynKindBall = {2600, 176, 112, 110, 170, 256};
static const T_DYN_KIND DynKindTrail = {1500, 72, 48, 80, 90, 128};
static const T_DYN_KIND DynKindBolt = {3000, 160, 96, 90, 150, 220};
static const T_DYN_KIND DynKindShield = {1100, 40, 24, 40, 40, 96};
static const T_DYN_KIND DynKindFire = {2400, 160, 96, 120, 150, 220};
static const T_DYN_KIND DynKindBurst = {2000, 128, 0, 0, 0, 160}; /* reach: see DynLight_GatherMoving */
static const T_DYN_KIND DynKindHero = {3000, 176, 0, 0, 0, 256};

/* The impacts' fire: the flash (139..142), the fireball (143..166) and the
   sparks (57..62). */
static S32 DynLight_FireSprite(S32 num) {
    return (num >= 139 && num <= 166) || (num >= 57 && num <= 62);
}

static void DynLight_AddMoving(S32 wx, S32 wy, S32 wz, const T_DYN_KIND *k, S32 scale, S32 colour) {
    T_DYN_LIGHT *l;
    if (s_nbMoving >= DYN_MOVING_LIGHTS || colour < 0 || scale <= 0)
        return;
    l = &s_moving[s_nbMoving];
    if (s_iso) { /* indoors the light keeps its world position; its screen box culls it */
        l->x = (float)wx;
        l->y = (float)wy;
        l->z = (float)wz;
    } else {
        DynLight_ToCam(wx, wy, wz, l);
        if (l->z + k->reach <= (float)NearClip || l->z - k->reach >= (float)Fill_Z_Fog_Far)
            return;
    }
    l->reach = (float)k->reach;
    l->emitR = (float)k->emitR;
    l->level = (k->level * scale) >> 8;
    l->halo = (k->halo * scale) >> 8;
    l->glow = (k->glow * scale) >> 8;
    l->tint = (k->tint * scale) >> 8;
    l->colour = (U8)colour;
    s_nbMoving++;
}

/* The game state the scene drew this frame, read and never written: the
   extras of the light kinds that AffScene shows or is about to show (its
   wait, time-in, talking and blink tests, read after AffScene ran, so a
   trail whose wait ends this frame lights a frame before its sprite shows;
   DynLight_AddMoving's near and far test stands in for AffScene's preclip,
   so a light just behind the camera still lights the ground in view; indoors
   the light's screen box does), the flows alive, the hero on fire. Outdoors
   the lights give half by day; indoors as much as the room is dark. */
static void DynLight_GatherMoving(void) {
    const S32 day = s_iso ? s_isoScale : (DynLight_Night() ? 256 : DYN_DAY_SCALE);
    S32 n;
    s_nbMoving = 0;
    for (n = 0; n < MAX_EXTRAS; n++) {
        const T_EXTRA *e = &ListExtra[n];
        const S32 spr = e->Sprite;
        const U32 flags = e->Flags;
        const T_DYN_KIND *k;
        S32 luma = 0, colour, scale = day;
        if (spr == -1 || (spr & 0x8000)) /* free, or a pof, a 3D object, a slab */
            continue;
        if (flags & (EXTRA_TIME_IN | EXTRA_WAIT))
            continue;
        if (FlagAnimWhoSpeak && (flags & (EXTRA_ANIM_SPRITE | EXTRA_CLIGNOTE)))
            continue;
        if ((flags & EXTRA_CLIGNOTE) && (TimerRefHR & 0xFF) < 100)
            continue;
        if (flags & EXTRA_MAGIC_BALL)
            k = &DynKindBall;
        else if (flags & EXTRA_TRAINEE)
            k = &DynKindTrail;
        else if (spr == SPRITE_FOUDRE)
            k = &DynKindBolt;
        else if (spr == SPRITE_PROTECT)
            k = &DynKindShield;
        else if ((flags & EXTRA_ANIM_SPRITE) && DynLight_FireSprite(spr))
            k = &DynKindFire;
        else
            continue;
        colour = DynLight_SpriteColour(spr, &luma);
        if (k == &DynKindFire) /* the fireball fades: its light with it */
            scale = (scale * luma) / 255;
        DynLight_AddMoving(e->PosX, e->PosY, e->PosZ, k, scale, colour);
    }

    /* A flow of coloured, bright dots (the magic ball's burst), at the middle
       of its live dots, as strong as it still has dots alive. Grey dust and
       smoke give none. */
    for (n = 0; n < MAX_FLOWS; n++) {
        const S_PART_FLOW *f = &ListPartFlow[n];
        S32 i, live = 0, sx = 0, sy = 0, sz = 0, sr = 0, sg = 0, sb = 0, mx, mn, luma, colour = -1, best = -1;
        if (f->NbDot <= 0 || f->PtrListDot == NULL || PtrPalCurrent == NULL)
            continue;
        for (i = 0; i < f->NbDot; i++) {
            const S_ONE_DOT *d = &f->PtrListDot[i];
            const U8 *rgb;
            if (d->Mode != 1) /* shown (FLOW.CPP's DOT_DISPLAY) */
                continue;
            rgb = PtrPalCurrent + 3 * (d->Couleur & 0xFF);
            live++;
            sx += d->X;
            sy += d->Y;
            sz += d->Z;
            sr += rgb[0];
            sg += rgb[1];
            sb += rgb[2];
            if (DynLight_Luma(rgb) > best) {
                best = DynLight_Luma(rgb);
                colour = d->Couleur & 0xFF;
            }
        }
        if (!live)
            continue;
        sr /= live;
        sg /= live;
        sb /= live;
        mx = sr > sg ? (sr > sb ? sr : sb) : (sg > sb ? sg : sb);
        mn = sr < sg ? (sr < sb ? sr : sb) : (sg < sb ? sg : sb);
        luma = (sr * 77 + sg * 150 + sb * 29) >> 8;
        if (luma < 110 || mx - mn < 48)
            continue;
        {
            /* A candle's few dots light less far than the magic ball's
               burst of thirty: 600 units and 250 more per square root of a
               dot alive. */
            T_DYN_KIND k = DynKindBurst;
            k.reach = 600 + (S32)(250.0f * sqrtf((float)live));
            if (k.reach > DynKindBurst.reach)
                k.reach = DynKindBurst.reach;
            DynLight_AddMoving(sx / live, sy / live, sz / live, &k, (day * live) / f->NbDot, colour);
        }
    }

    /* The hero burning in lava (OBJECT.CPP gives the body GEN_BODY_FEU). */
    {
        const T_OBJET *hero = &ListObjet[NUM_PERSO];
        if (hero->GenBody == GEN_BODY_FEU && !(hero->Flags & INVISIBLE) && hero->Obj.Body.Num != -1) {
            S32 luma = 0;
            const S32 colour = DynLight_SpriteColour(139, &luma);
            DynLight_AddMoving(hero->Obj.X, hero->Obj.Y + hero->YMax / 2, hero->Obj.Z, &DynKindHero, day, colour);
        }
    }
}

/*══════════════════════════════════════════════════════════════════════════*
    The palette passes (level 1)
 *══════════════════════════════════════════════════════════════════════════*/

/* The levels the lights leave, in sixteenths: worked out once per depth
   block where the block is one surface, pixel by pixel elsewhere (an edge, a
   halo); a pixel's level is the sum of the two. */
typedef struct {
    U8 *pix;       /* a byte per pixel, Log's pitch */
    U8 *blk;       /* a byte per depth block */
    T_DYN_BOX box; /* where either is not zero, in pixels */
} T_DYN_ACC;

/* The frame's size the buffers are for. */
static S32 s_w = 0, s_h = 0;
static T_DYN_ACC s_staticAcc = {NULL, NULL, {0, 0, -1, -1}}; /* the lamps', from the last full redraw */
static T_DYN_ACC s_movAcc = {NULL, NULL, {0, 0, -1, -1}};    /* the moving lights', this frame */
/* Inside s_staticAcc.box, from the last full redraw: */
static U8 *s_ref = NULL;      /* the background as the lamps lit it */
static U8 *s_base = NULL;     /* the background before they did */
static U8 *s_lit = NULL;      /* 1 where the lamps own the pixel: 0 under an object baked since */
static float *s_kx = NULL;    /* (x - XCentre) / LFactorX, per column */
static S32 s_staticValid = 0; /* s_staticAcc and s_ref describe the background */

/* The surface table, for the palette the frame's indices are in. */
static U8 s_surf[16 * 256];
static S32 s_surfIdx = -1;
static const U8 *s_surfPal = NULL;

static void DynLight_FreeAcc(T_DYN_ACC *a) {
    free(a->pix);
    free(a->blk);
    a->pix = a->blk = NULL;
    a->box.x0 = a->box.y0 = 0;
    a->box.x1 = a->box.y1 = -1;
}

static void DynLight_FreeBuffers(void) {
    DynLight_FreeAcc(&s_staticAcc);
    DynLight_FreeAcc(&s_movAcc);
    free(s_ref);
    free(s_base);
    free(s_lit);
    free(s_kx);
    free(s_zmin);
    free(s_zmax);
    free(s_isoS);
    free(s_isoF);
    free(s_isoD);
    s_isoS = NULL;
    s_isoF = NULL;
    s_isoD = NULL;
    s_isoValid = 0;
    s_ref = s_base = s_lit = NULL;
    s_kx = NULL;
    s_zmin = s_zmax = NULL;
    s_bw = s_bh = 0;
    s_zValid = 0;
    s_w = s_h = 0;
    s_staticValid = 0;
}

static S32 DynLight_Buffers(void) {
    const S32 w = (S32)ModeDesiredX, h = (S32)ModeDesiredY;
    if (w == s_w && h == s_h && s_staticAcc.pix != NULL)
        return 1;
    DynLight_FreeBuffers();
    if (w <= 0 || h <= 0)
        return 0;
    s_bw = (w + DYN_BLOCK - 1) / DYN_BLOCK;
    s_bh = (h + DYN_BLOCK - 1) / DYN_BLOCK;
    s_staticAcc.pix = (U8 *)calloc((size_t)w * h, 1);
    s_staticAcc.blk = (U8 *)calloc((size_t)s_bw * s_bh, 1);
    s_movAcc.pix = (U8 *)calloc((size_t)w * h, 1);
    s_movAcc.blk = (U8 *)calloc((size_t)s_bw * s_bh, 1);
    s_ref = (U8 *)malloc((size_t)w * h);
    s_base = (U8 *)malloc((size_t)w * h);
    s_lit = (U8 *)malloc((size_t)w * h);
    s_kx = (float *)malloc(sizeof(float) * w);
    s_zmin = (U16 *)malloc(sizeof(U16) * (size_t)s_bw * s_bh);
    s_zmax = (U16 *)malloc(sizeof(U16) * (size_t)s_bw * s_bh);
    s_isoS = (U16 *)malloc(sizeof(U16) * (size_t)w * h);
    s_isoF = (U8 *)malloc((size_t)w * h);
    s_isoD = (float *)malloc(sizeof(float) * w);
    if (s_staticAcc.pix == NULL || s_staticAcc.blk == NULL || s_movAcc.pix == NULL || s_movAcc.blk == NULL ||
        s_ref == NULL || s_base == NULL || s_lit == NULL || s_kx == NULL || s_zmin == NULL || s_zmax == NULL ||
        s_isoS == NULL || s_isoF == NULL || s_isoD == NULL) {
        DynLight_FreeBuffers();
        return 0;
    }
    s_w = w;
    s_h = h;
    return 1;
}

static S32 DynLight_Surf(void) {
    if (PtrPalCurrent == NULL)
        return 0;
    if (s_surfIdx != IdxPalette || s_surfPal != PtrPalCurrent) {
        DynLight_BuildSurf(PtrPalCurrent, s_surf);
        s_surfIdx = IdxPalette;
        s_surfPal = PtrPalCurrent;
    }
    return 1;
}

static void DynLight_Columns(void) {
    const float inv = 1.0f / (float)(LFactorX > 0 ? LFactorX : 1);
    S32 x;
    for (x = 0; x < s_w; x++)
        s_kx[x] = (float)(x - XCentre) * inv;
}

static S32 DynLight_BoxEmpty(const T_DYN_BOX *b) {
    return b->x1 < b->x0 || b->y1 < b->y0;
}

static void DynLight_Clip(T_DYN_BOX *b, const T_DYN_BOX *win) {
    if (b->x0 < win->x0)
        b->x0 = win->x0;
    if (b->y0 < win->y0)
        b->y0 = win->y0;
    if (b->x1 > win->x1)
        b->x1 = win->x1;
    if (b->y1 > win->y1)
        b->y1 = win->y1;
}

static void DynLight_Union(T_DYN_BOX *u, const T_DYN_BOX *b) {
    if (DynLight_BoxEmpty(b))
        return;
    if (DynLight_BoxEmpty(u)) {
        *u = *b;
        return;
    }
    if (b->x0 < u->x0)
        u->x0 = b->x0;
    if (b->y0 < u->y0)
        u->y0 = b->y0;
    if (b->x1 > u->x1)
        u->x1 = b->x1;
    if (b->y1 > u->y1)
        u->y1 = b->y1;
}

static void DynLight_Clear(T_DYN_ACC *a) {
    const T_DYN_BOX *b = &a->box;
    S32 y;
    if (!DynLight_BoxEmpty(b)) {
        const S32 bx0 = b->x0 / DYN_BLOCK, bx1 = b->x1 / DYN_BLOCK;
        for (y = b->y0; y <= b->y1; y++)
            memset(a->pix + TabOffLine[y] + b->x0, 0, (size_t)(b->x1 - b->x0 + 1));
        for (y = b->y0 / DYN_BLOCK; y <= b->y1 / DYN_BLOCK; y++)
            memset(a->blk + (size_t)y * s_bw + bx0, 0, (size_t)(bx1 - bx0 + 1));
    }
    a->box.x0 = a->box.y0 = 0;
    a->box.x1 = a->box.y1 = -1;
}

/* A pixel's level, the block's and its own. */
static S32 DynLight_AccAt(const T_DYN_ACC *a, U32 i, S32 x, S32 y) {
    const S32 v = a->pix[i] + a->blk[(size_t)(y >> DYN_BLOCK_SHIFT) * s_bw + (x >> DYN_BLOCK_SHIFT)];
    return v > 255 ? 255 : v;
}

/* The light's sphere on screen, clipped to `win`; 0 when nothing of it shows. */
static S32 DynLight_LightBox(const T_DYN_LIGHT *l, float r, const T_DYN_BOX *win, T_DYN_BOX *b) {
    S32 full;
    if (!DynLight_SphereBox(l->x, l->y, l->z, r, (float)LFactorX, (float)LFactorY, XCentre, YCentre,
                            (float)NearClip, b, &full))
        return 0;
    if (full)
        *b = *win;
    else
        DynLight_Clip(b, win);
    return !DynLight_BoxEmpty(b);
}

/* The fog the fillers draw, as how much of a light survives at depth d: all
   of it before the fog starts, none at the far clip (DynLight_FogKeep, in
   floating point for the per-pixel passes). */
typedef struct {
    float zs; /* a Z-buffer value to a depth */
    float invLx, invLy;
    float fogNear, fogFar, fogInv;
} T_DYN_VIEW;

static void DynLight_View(T_DYN_VIEW *v) {
    v->zs = (float)Fill_Z_Fog_Far * (1.0f / 65536.0f);
    v->invLx = 1.0f / (float)(LFactorX > 0 ? LFactorX : 1);
    v->invLy = 1.0f / (float)(LFactorY > 0 ? LFactorY : 1);
    v->fogNear = (float)Fill_Z_Fog_Near;
    v->fogFar = (float)Fill_Z_Fog_Far;
    v->fogInv = Fill_Z_Fog_Far > Fill_Z_Fog_Near ? 1.0f / (float)(Fill_Z_Fog_Far - Fill_Z_Fog_Near) : 0.0f;
}

static float DynLight_Keep(const T_DYN_VIEW *v, float d) {
    if (d <= v->fogNear)
        return 1.0f;
    if (d >= v->fogFar)
        return 0.0f;
    return (v->fogFar - d) * v->fogInv;
}

/* The columns of row y a light's sphere covers, clipped to [x0, x1]; 0 when
   none. */
static S32 DynLight_Span(const T_DYN_LIGHT *l, float r2, float ky, S32 *x0, S32 *x1) {
    float k0, k1;
    S32 a, b;
    if (!DynLight_RowSpan(l->x, l->y, l->z, r2, ky, &k0, &k1))
        return 0;
    if (k0 > -1e29f) {
        const float f = k0 * (float)LFactorX;
        a = f < -1e6f ? *x0 : XCentre + (S32)f - 1;
        if (a > *x0)
            *x0 = a;
    }
    if (k1 < 1e29f) {
        const float f = k1 * (float)LFactorX;
        b = f > 1e6f ? *x1 : XCentre + (S32)f + 1;
        if (b < *x1)
            *x1 = b;
    }
    return *x0 <= *x1;
}

/* A light at one point of the background, from the Z-buffer value there and
   the ray's slopes: its level in sixteenths, faded in the fog. */
static S32 DynLight_At(const T_DYN_LIGHT *l, const T_DYN_VIEW *v, float kx, float ky, float zb, float r2,
                       float inv2) {
    const float d = zb * v->zs;
    const float dz = d - l->z;
    float dx, dy, d2;
    if (dz * dz >= r2)
        return 0;
    dx = kx * d - l->x;
    dy = ky * d - l->y;
    d2 = dx * dx + dy * dy + dz * dz;
    if (d2 >= r2)
        return 0;
    return (S32)((float)DynLight_FalloffF(d2, inv2, l->level) * DynLight_Keep(v, d));
}

/* The Z-buffer values of the depths from z - r to z + r; 0 when none is. */
static S32 DynLight_ZRange(const T_DYN_VIEW *v, float z, float r, S32 *lo, S32 *hi) {
    const float a = (z - r) / v->zs, b = (z + r) / v->zs + 1.0f;
    *lo = a <= 0.0f ? 0 : (a >= 65535.0f ? 65535 : (S32)a);
    *hi = b >= 65534.0f ? 65534 : (b <= 0.0f ? -1 : (S32)b);
    return *lo <= *hi;
}

/* The Z-buffer's nearest and farthest value in each 4x4 block, built once
   after each full redraw (the only one that writes depth) and read by every
   light: a light passes over the blocks its depths miss without touching the
   Z-buffer, and works a block on one surface out once. */

static void DynLight_ZBlocks(void) {
    const S32 full = s_w >> DYN_BLOCK_SHIFT; /* the blocks four pixels wide */
    S32 bx, by, x, y;
    if (s_zValid)
        return;
    for (by = 0; by < s_bh; by++) {
        const S32 y0 = by * DYN_BLOCK, y1 = y0 + DYN_BLOCK - 1 < s_h - 1 ? y0 + DYN_BLOCK - 1 : s_h - 1;
        U16 *mn = s_zmin + (size_t)by * s_bw, *mx = s_zmax + (size_t)by * s_bw;
        const U16 *z = PtrZBuffer + TabOffLine[y0];
        /* The first row sets them, the others narrow them, four pixels a block. */
        for (bx = 0; bx < full; bx++) {
            const U16 *p = z + (bx << DYN_BLOCK_SHIFT);
            const U16 a = p[0] < p[1] ? p[0] : p[1], b = p[2] < p[3] ? p[2] : p[3];
            const U16 c = p[0] > p[1] ? p[0] : p[1], d = p[2] > p[3] ? p[2] : p[3];
            mn[bx] = a < b ? a : b;
            mx[bx] = c > d ? c : d;
        }
        for (y = y0 + 1; y <= y1; y++) {
            z = PtrZBuffer + TabOffLine[y];
            for (bx = 0; bx < full; bx++) {
                const U16 *p = z + (bx << DYN_BLOCK_SHIFT);
                const U16 a = p[0] < p[1] ? p[0] : p[1], b = p[2] < p[3] ? p[2] : p[3];
                const U16 c = p[0] > p[1] ? p[0] : p[1], d = p[2] > p[3] ? p[2] : p[3];
                const U16 lo = a < b ? a : b, hi = c > d ? c : d;
                if (lo < mn[bx])
                    mn[bx] = lo;
                if (hi > mx[bx])
                    mx[bx] = hi;
            }
        }
        if (full < s_bw) { /* a last block narrower than four */
            U32 lo = 0xFFFF, hi = 0;
            for (y = y0; y <= y1; y++)
                for (x = full << DYN_BLOCK_SHIFT; x < s_w; x++) {
                    const U32 v = PtrZBuffer[TabOffLine[y] + (U32)x];
                    lo = v < lo ? v : lo;
                    hi = v > hi ? v : hi;
                }
            mn[full] = (U16)lo;
            mx[full] = (U16)hi;
        }
    }
    s_zValid = 1;
}

/* A light's pool over the pixels of `win` whose depth the Z-buffer holds: the
   level at each, from its distance in space, falling off to nothing at the
   reach and fading in the fog. `background`: only where the frame shows the
   background (Log as Screen), the rest having no depth there.

   Block by block: a block whose depths lie within a 64th of each other is a
   piece of one surface, and the light is worked out once at its middle (the
   pool changes by less than a sixteenth of a level across it, which the
   dither spreads); a block across an edge, pixel by pixel. */
static void DynLight_AddPool(T_DYN_ACC *acc, const T_DYN_LIGHT *l, const T_DYN_BOX *win, S32 background,
                             T_DYN_BOX *u) {
    const float r2 = l->reach * l->reach, inv2 = 1.0f / r2;
    const U8 *log = (const U8 *)Log, *scr = (const U8 *)Screen;
    T_DYN_VIEW v;
    T_DYN_BOX b, t = {0, 0, -1, -1};
    S32 bx, by, zbLo, zbHi;
    if (l->level <= 0 || !DynLight_LightBox(l, l->reach, win, &b))
        return;
    DynLight_View(&v);
    DynLight_ZBlocks();
    if (!DynLight_ZRange(&v, l->z, l->reach, &zbLo, &zbHi))
        return;
    for (by = b.y0 / DYN_BLOCK; by <= b.y1 / DYN_BLOCK; by++) {
        const S32 y0 = by * DYN_BLOCK < b.y0 ? b.y0 : by * DYN_BLOCK;
        const S32 y1 = by * DYN_BLOCK + DYN_BLOCK - 1 > b.y1 ? b.y1 : by * DYN_BLOCK + DYN_BLOCK - 1;
        const U16 *mn = s_zmin + (size_t)by * s_bw, *mx = s_zmax + (size_t)by * s_bw;
        S32 x0 = 0x7FFFFFFF, x1 = -1, y;
        for (y = y0; y <= y1; y++) {
            S32 a0 = b.x0, a1 = b.x1;
            if (DynLight_Span(l, r2, -(float)(y - YCentre) * v.invLy, &a0, &a1)) {
                if (a0 < x0)
                    x0 = a0;
                if (a1 > x1)
                    x1 = a1;
            }
        }
        if (x1 < x0)
            continue;
        for (bx = x0 / DYN_BLOCK; bx <= x1 / DYN_BLOCK; bx++) {
            const S32 zmin = mn[bx], zmax = mx[bx];
            const S32 xa = bx * DYN_BLOCK < x0 ? x0 : bx * DYN_BLOCK;
            const S32 xb = bx * DYN_BLOCK + DYN_BLOCK - 1 > x1 ? x1 : bx * DYN_BLOCK + DYN_BLOCK - 1;
            S32 x;
            if (zmax < zbLo || zmin > zbHi)
                continue;
            if (zmax != 0xFFFF && zmax - zmin <= (zmin >> 6) + 1) {
                /* one surface: the middle of the block, for the whole block
                   (Apply keeps to the box, and to the background) */
                const float kx = 0.5f * (s_kx[xa] + s_kx[xb]);
                const float ky = -(0.5f * (float)(y0 + y1) - (float)YCentre) * v.invLy;
                const S32 val = DynLight_At(l, &v, kx, ky, 0.5f * (float)(zmin + zmax), r2, inv2);
                U8 *cell = acc->blk + (size_t)by * s_bw + bx;
                if (val > 0)
                    *cell = (U8)(*cell + val > 255 ? 255 : *cell + val);
            } else {
                for (y = y0; y <= y1; y++) {
                    const U32 row = TabOffLine[y];
                    const float ky = -(float)(y - YCentre) * v.invLy;
                    for (x = xa; x <= xb; x++) {
                        const U32 i = row + (U32)x;
                        const S32 zb = PtrZBuffer[i];
                        S32 val, sum;
                        if (zb == 0xFFFF || zb < zbLo || zb > zbHi)
                            continue;
                        if (background && log[i] != scr[i])
                            continue;
                        val = DynLight_At(l, &v, s_kx[x], ky, (float)zb, r2, inv2);
                        if (val <= 0)
                            continue;
                        sum = val + acc->pix[i];
                        acc->pix[i] = (U8)(sum > 255 ? 255 : sum);
                    }
                }
            }
        }
        if (DynLight_BoxEmpty(&t)) {
            t.x0 = x0;
            t.x1 = x1;
            t.y0 = y0;
        }
        if (x0 < t.x0)
            t.x0 = x0;
        if (x1 > t.x1)
            t.x1 = x1;
        t.y1 = y1;
    }
    DynLight_Union(u, &t);
}

/* Where an emitter is on screen and how wide it shows (`scale` emitter radii);
   0 behind the camera, or hidden: the surface at its centre lies nearer than
   its own centre by more than its diameter. */
static S32 DynLight_Emitter(const T_DYN_LIGHT *l, S32 scale, S32 *sx, S32 *sy, S32 *rs) {
    const float zs = (float)Fill_Z_Fog_Far * (1.0f / 65536.0f);
    S32 r;
    if (l->emitR <= 0.0f || l->z <= (float)NearClip)
        return 0;
    *sx = XCentre + (S32)(l->x * (float)LFactorX / l->z);
    *sy = YCentre - (S32)(l->y * (float)LFactorY / l->z);
    r = (S32)((float)scale * l->emitR * (float)LFactorX / l->z);
    if (r > (s_w < s_h ? s_w : s_h) / 4)
        r = (s_w < s_h ? s_w : s_h) / 4;
    if (r < 2)
        return 0;
    *rs = r;
    if (*sx >= 0 && *sx < s_w && *sy >= 0 && *sy < s_h) {
        const U16 zb = PtrZBuffer[TabOffLine[*sy] + (U32)*sx];
        if (zb != 0xFFFF && (float)zb * zs < l->z - 2.0f * l->emitR)
            return 0;
    }
    return 1;
}

/* Level 1's halo: the background about an emitter, at its depth or behind it,
   brightened in a disc around it on screen, as a lamp's globe or the ball
   seems to in a dark street. */
static void DynLight_AddHalo(T_DYN_ACC *acc, const T_DYN_LIGHT *l, const T_DYN_BOX *win, S32 background,
                             T_DYN_BOX *u) {
    const float zs = (float)Fill_Z_Fog_Far * (1.0f / 65536.0f);
    const U8 *log = (const U8 *)Log, *scr = (const U8 *)Screen;
    const float front = l->z - 2.0f * l->emitR;
    S32 sx, sy, rs, rs2, x, y, level;
    float inv2;
    T_DYN_BOX b;
    if (l->halo <= 0 || !DynLight_Emitter(l, DYN_HALO_SCALE, &sx, &sy, &rs))
        return;
    level = (l->halo * DynLight_FogKeep((S32)l->z, Fill_Z_Fog_Near, Fill_Z_Fog_Far)) >> 8;
    if (level <= 0)
        return;
    rs2 = rs * rs;
    inv2 = 1.0f / (float)rs2;
    b.x0 = sx - rs;
    b.y0 = sy - rs;
    b.x1 = sx + rs;
    b.y1 = sy + rs;
    DynLight_Clip(&b, win);
    if (DynLight_BoxEmpty(&b))
        return;
    for (y = b.y0; y <= b.y1; y++) {
        const U32 row = TabOffLine[y];
        const S32 dy2 = (y - sy) * (y - sy);
        const S32 half = (S32)sqrtf((float)(rs2 - dy2));
        const S32 x0 = sx - half > b.x0 ? sx - half : b.x0, x1 = sx + half < b.x1 ? sx + half : b.x1;
        for (x = x0; x <= x1; x++) {
            const U32 i = row + (U32)x;
            const S32 r2 = (x - sx) * (x - sx) + dy2;
            const U16 zb = PtrZBuffer[i];
            S32 v;
            if (r2 >= rs2)
                continue;
            if (zb != 0xFFFF && (float)zb * zs < front) /* in front of it */
                continue;
            if (background && log[i] != scr[i])
                continue;
            v = DynLight_FalloffF((float)r2, inv2, level) + acc->pix[i];
            acc->pix[i] = (U8)(v > 255 ? 255 : v);
        }
    }
    DynLight_Union(u, &b);
}

/* Each pixel of the box up its own ramp by its level, rounded by the dither;
   `background`: only where the frame shows the background (a block's level
   covers the whole block). */
static void DynLight_Apply(U8 *dst, const T_DYN_ACC *a, S32 background) {
    const U8 *log = (const U8 *)Log, *scr = (const U8 *)Screen;
    const T_DYN_BOX *b = &a->box;
    S32 x, y;
    for (y = b->y0; y <= b->y1; y++) {
        const U32 row = TabOffLine[y];
        const U8 *blk = a->blk + (size_t)(y >> DYN_BLOCK_SHIFT) * s_bw;
        const U8 *pix = a->pix + row;
        const U8 *bayer = DynLightBayer + ((y & 3) << 2);
        U8 *d = dst + row;
        for (x = b->x0; x <= b->x1; x++) {
            S32 v = pix[x] + blk[x >> DYN_BLOCK_SHIFT], lvl;
            if (!v)
                continue;
            if (background && log[row + x] != scr[row + x])
                continue;
            lvl = ((v > 255 ? 255 : v) + bayer[x & 3]) >> 4;
            if (lvl > 15)
                lvl = 15;
            if (lvl)
                d[x] = s_surf[(lvl << 8) | d[x]];
        }
    }
}

void DynLight_BakeBackground(void) {
    T_DYN_BOX win, u = {0, 0, -1, -1};
    S32 k, y;
    s_gathering = 0; /* the redraw's cubes are all drawn */
    s_staticValid = 0;
    if (!DynLight || !s_nbStatic || !DynLight_Night())
        return;
    if (!DynLight_Buffers() || !DynLight_Surf())
        return;
    DynLight_Columns();
    DynLight_Clear(&s_staticAcc);
    win.x0 = 0;
    win.y0 = 0;
    win.x1 = s_w - 1;
    win.y1 = s_h - 1;
    for (k = 0; k < s_nbStatic; k++) {
        DynLight_AddPool(&s_staticAcc, &s_static[k], &win, 0, &u);
        if (DynLight == 1) /* level 2 glows at present instead */
            DynLight_AddHalo(&s_staticAcc, &s_static[k], &win, 0, &u);
    }
    s_staticAcc.box = u;
    if (DynLight_BoxEmpty(&u))
        return;
    for (y = u.y0; y <= u.y1; y++) {
        const size_t i = TabOffLine[y] + (size_t)u.x0, n = (size_t)(u.x1 - u.x0 + 1);
        memcpy(s_base + i, (const U8 *)Log + i, n);
        memset(s_lit + i, 1, n); /* nothing is drawn over the background yet */
    }
    DynLight_Apply((U8 *)Log, &s_staticAcc, 0);
    for (y = u.y0; y <= u.y1; y++)
        memcpy(s_ref + TabOffLine[y] + u.x0, (const U8 *)Log + TabOffLine[y] + u.x0, (size_t)(u.x1 - u.x0 + 1));
    s_staticValid = 1;
}

static S32 DynLight_StaticBox(S32 x0, S32 y0, S32 x1, S32 y1, T_DYN_BOX *b) {
    if (!s_staticValid)
        return 0;
    if (s_w != (S32)ModeDesiredX || s_h != (S32)ModeDesiredY) {
        s_staticValid = 0;
        return 0;
    }
    b->x0 = x0;
    b->y0 = y0;
    b->x1 = x1;
    b->y1 = y1;
    DynLight_Clip(b, &s_staticAcc.box);
    return !DynLight_BoxEmpty(b);
}

void DynLight_Unlight(S32 x0, S32 y0, S32 x1, S32 y1) {
    U8 *log = (U8 *)Log;
    T_DYN_BOX b;
    S32 x, y;
    if (!DynLight || !DynLight_StaticBox(x0, y0, x1, y1, &b))
        return;
    for (y = b.y0; y <= b.y1; y++) {
        const U32 row = TabOffLine[y];
        for (x = b.x0; x <= b.x1; x++)
            if (s_lit[row + x])
                log[row + x] = s_base[row + x];
    }
}

void DynLight_Refreshed(S32 x0, S32 y0, S32 x1, S32 y1) {
    U8 *log = (U8 *)Log;
    T_DYN_BOX b;
    S32 x, y;
    if (!DynLight || !DynLight_StaticBox(x0, y0, x1, y1, &b))
        return;
    for (y = b.y0; y <= b.y1; y++) {
        const U32 row = TabOffLine[y];
        for (x = b.x0; x <= b.x1; x++) {
            const U32 i = row + (U32)x;
            const S32 v = DynLight_AccAt(&s_staticAcc, i, x, y);
            S32 lvl;
            /* The lamps' pixels hold the background as drawn (DynLight_Unlight
               put it back before the redraw): lit again, whatever it is now.
               Under a baked object, only what the redraw changed. */
            if (!s_lit[i]) {
                if (log[i] == s_ref[i])
                    continue;
                s_lit[i] = 1;
            }
            s_base[i] = log[i];
            lvl = v ? DynLight_Level(v, x, y) : 0;
            if (lvl)
                log[i] = s_surf[(lvl << 8) | log[i]];
            s_ref[i] = log[i];
        }
    }
}

void DynLight_Baking(S32 x0, S32 y0, S32 x1, S32 y1) {
    const U8 *log = (const U8 *)Log, *scr = (const U8 *)Screen;
    T_DYN_BOX b;
    S32 x, y;
    if (!DynLight)
        return;
    if (CubeMode != CUBE_EXTERIEUR) {
        /* Indoors the object's pixels show no brick from now on. */
        if (!s_isoValid || s_w != (S32)ModeDesiredX || s_h != (S32)ModeDesiredY)
            return;
        b.x0 = x0;
        b.y0 = y0;
        b.x1 = x1;
        b.y1 = y1;
        DynLight_Clip(&b, &s_isoClip);
        for (y = b.y0; y <= b.y1; y++) {
            const U32 row = TabOffLine[y];
            for (x = b.x0; x <= b.x1; x++)
                if (log[row + x] != scr[row + x])
                    s_isoS[row + x] = DYN_ISO_NONE;
        }
        return;
    }
    if (!DynLight_StaticBox(x0, y0, x1, y1, &b))
        return;
    for (y = b.y0; y <= b.y1; y++) {
        const U32 row = TabOffLine[y];
        for (x = b.x0; x <= b.x1; x++) {
            const U32 i = row + (U32)x;
            if (log[i] != scr[i]) { /* the object's pixel: the lamps leave it */
                s_lit[i] = 0;
                s_base[i] = log[i];
                s_ref[i] = log[i];
            }
        }
    }
}

/*══════════════════════════════════════════════════════════════════════════*
    Interiors
 *══════════════════════════════════════════════════════════════════════════*/

/* The face and the depth of each pixel of a brick's 48 x 38 box
   (DynLight_IsoT3), whatever the brick, the camera and the resolution: the
   depth as its part of DynLight_IsoS2, (T3 + 3) / 6, to which a cell adds
   256(cx + cz) (1536 is 6 x 256). */
static U16 s_isoT6[DYN_ISO_H][DYN_ISO_W];
static U8 s_isoFaceOf[DYN_ISO_H][DYN_ISO_W];
static S32 s_isoTables = 0;

static void DynLight_IsoTables(void) {
    S32 u, v, face;
    if (s_isoTables)
        return;
    for (v = 0; v < DYN_ISO_H; v++)
        for (u = 0; u < DYN_ISO_W; u++) {
            s_isoT6[v][u] = (U16)((DynLight_IsoT3(u, v, &face) + 3) / 6);
            s_isoFaceOf[v][u] = (U8)face;
        }
    s_isoTables = 1;
}

/* GrilleBrickDrawn while the room is drawn: the brick's opaque pixels, read
   from its mask (the CopyMask layout: per row a count, then runs to skip and
   to write, the first a skip), inside the clip AffGraph drew it in. The
   bricks come in the painter's order, so each pixel ends with the brick
   that shows there. */
static void DynLight_IsoBrick(S32 brick, S32 xs, S32 ys, S32 x, S32 y, S32 z) {
    const U8 *m;
    S32 dy, hx, hy, r, base, x0, x1;
    const S32 cy0 = ClipYMin, cy1 = ClipYMax;
    (void)y;
    if (!s_isoBuilding || BufferMaskBrick == NULL || brick < 0)
        return;
    base = 256 * (x + z);
    /* The columns a pixel may take: the clip AffGraph drew in, and the box. */
    x0 = ClipXMin > xs ? ClipXMin : xs;
    x1 = ClipXMax < xs + DYN_ISO_W - 1 ? ClipXMax : xs + DYN_ISO_W - 1;
    m = BufferMaskBrick + ((const U32 *)BufferMaskBrick)[brick];
    dy = m[1];
    hx = m[2];
    hy = m[3];
    m += 4;
    for (r = 0; r < dy; r++) {
        const S32 py = ys + hy + r, v = hy + r;
        const S32 rowIn = py >= cy0 && py <= cy1 && v >= 0 && v < DYN_ISO_H;
        U16 *ps = rowIn ? s_isoS + TabOffLine[py] : NULL;
        U8 *pf = rowIn ? s_isoF + TabOffLine[py] : NULL;
        const U16 *t6 = rowIn ? s_isoT6[v] : NULL;
        const U8 *fc = rowIn ? s_isoFaceOf[v] : NULL;
        S32 n = *m++, px = xs + hx;
        while (n > 0) {
            S32 run, a, b, q;
            px += *m++; /* to skip */
            if (--n == 0)
                break;
            run = *m++; /* to write */
            n--;
            if (rowIn) {
                a = px > x0 ? px : x0;
                b = px + run - 1 < x1 ? px + run - 1 : x1;
                for (q = a; q <= b; q++) {
                    ps[q] = (U16)(base + t6[q - xs]); /* DynLight_IsoS2 */
                    pf[q] = fc[q - xs];
                }
            }
            px += run;
        }
    }
}

void DynLight_IsoBegin(void) {
    S32 y;
    s_isoValid = 0;
    s_isoBuilding = 0;
    GrilleBrickDrawn = NULL;
    if (!DynLight || CubeMode != CUBE_INTERIEUR || !DynLight_Buffers())
        return;
    DynLight_IsoTables();
    for (y = 0; y < s_h; y++)
        memset(s_isoS + TabOffLine[y], 0xFF, sizeof(U16) * (size_t)s_w);
    s_isoCamX = CameraX;
    s_isoCamY = CameraY;
    s_isoCamZ = CameraZ;
    s_isoXC = XCentre;
    s_isoYC = YCentre;
    s_isoClip.x0 = ClipXMin < 0 ? 0 : ClipXMin;
    s_isoClip.y0 = ClipYMin < 0 ? 0 : ClipYMin;
    s_isoClip.x1 = ClipXMax > s_w - 1 ? s_w - 1 : ClipXMax;
    s_isoClip.y1 = ClipYMax > s_h - 1 ? s_h - 1 : ClipYMax;
    s_nbStatic = 0;        /* the lamps are the exteriors' */
    s_tintStaticValid = 0; /* and so is their tint */
    s_isoBuilding = 1;
    GrilleBrickDrawn = DynLight_IsoBrick;
}

void DynLight_IsoEnd(void) {
    const U8 *log = (const U8 *)Log;
    S32 x, y, sum = 0, count = 0, ambient;
    GrilleBrickDrawn = NULL;
    if (!s_isoBuilding)
        return;
    s_isoBuilding = 0;
    s_isoValid = 1;
    /* How dark the room is: the mean luma of its bricks, a pixel in sixteen. */
    if (PtrPalCurrent != NULL)
        for (y = s_isoClip.y0; y <= s_isoClip.y1; y += 4) {
            const U32 row = TabOffLine[y];
            for (x = s_isoClip.x0; x <= s_isoClip.x1; x += 4)
                if (s_isoS[row + x] != DYN_ISO_NONE) {
                    sum += DynLight_Luma(PtrPalCurrent + 3 * log[row + x]);
                    count++;
                }
        }
    ambient = count ? sum / count : 255;
    s_isoScale = DynLight_IsoStrength(ambient);
}

/* The interior was drawn with this camera, at this size: a camera moved
   since (a modal recentring it, say) waits for the next full redraw. */
static S32 DynLight_IsoReady(void) {
    return s_isoValid && s_w == (S32)ModeDesiredX && s_h == (S32)ModeDesiredY && CameraX == s_isoCamX &&
           CameraY == s_isoCamY && CameraZ == s_isoCamZ && XCentre == s_isoXC && YCentre == s_isoYC;
}

static void DynLight_IsoColumns(void) {
    S32 x;
    for (x = 0; x < s_w; x++)
        s_isoD[x] = (64.0f * (float)(x - s_isoXC) - 32.0f) / 3.0f + (float)(s_isoCamX - s_isoCamZ);
}

/* The world point a background pixel shows, and its face; 0 where no brick
   does. `rowY` is the row's DynLight_IsoRowY. */
static float DynLight_IsoRowY(S32 y) {
    return (float)s_isoCamY - (6.0f * (float)(s_isoCamX + s_isoCamZ) + 256.0f * (float)(y - s_isoYC)) / 15.0f;
}

static S32 DynLight_IsoAt(U32 i, S32 x, float rowY, float *X, float *Y, float *Z) {
    const U16 s2 = s_isoS[i];
    float S;
    if (s2 == DYN_ISO_NONE)
        return 0;
    S = 2.0f * (float)s2;
    *X = 0.5f * (S + s_isoD[x]);
    *Z = 0.5f * (S - s_isoD[x]);
    *Y = rowY + 0.4f * S;
    return 1;
}

/* The light at a background point indoors: the falloff, times how much the
   face takes of it (DynLight_IsoLambert). 0..level. */
static S32 DynLight_IsoAtLight(const T_DYN_LIGHT *l, S32 level, float r2, float inv2, U8 face, float X, float Y,
                               float Z) {
    const float dx = l->x - X, dy = l->y - Y, dz = l->z - Z;
    const float d2 = dx * dx + dy * dy + dz * dz;
    float dot;
    if (d2 >= r2)
        return 0;
    dot = face == DYN_ISO_TOP ? dy : (face == DYN_ISO_PX ? dx : dz);
    return (DynLight_FalloffF(d2, inv2, level) * DynLight_IsoLambert(dot, sqrtf(d2))) >> 8;
}

/* A light's sphere on screen indoors, clipped to `win`; 0 when nothing of it
   shows. Its centre where LongProjectPointIso puts it, give or take the
   pixel the bricks sit apart from the objects. */
static S32 DynLight_IsoLightBox(const T_DYN_LIGHT *l, float r, const T_DYN_BOX *win, T_DYN_BOX *b) {
    float sx, sy;
    DynLight_IsoProject(l->x, l->y, l->z, s_isoCamX, s_isoCamY, s_isoCamZ, s_isoXC, s_isoYC, &sx, &sy);
    b->x0 = (S32)(sx - r * DYN_ISO_RX) - 2;
    b->x1 = (S32)(sx + r * DYN_ISO_RX) + 2;
    b->y0 = (S32)(sy - r * DYN_ISO_RY) - 2;
    b->y1 = (S32)(sy + r * DYN_ISO_RY) + 2;
    DynLight_Clip(b, win);
    return !DynLight_BoxEmpty(b);
}

/* DynLight_AddPool indoors: the pixels whose brick lies within the light's
   reach, lit as the face they show takes it. The room's void (colour 0)
   and what no brick drew are left alone. */
static void DynLight_IsoAddPool(T_DYN_ACC *acc, const T_DYN_LIGHT *l, const T_DYN_BOX *win, S32 background,
                                T_DYN_BOX *u) {
    const float r2 = l->reach * l->reach, inv2 = 1.0f / r2;
    const U8 *log = (const U8 *)Log, *scr = (const U8 *)Screen;
    /* The sphere's (X + Z) / 2, which a pixel's kept value is compared with
       before any arithmetic. */
    const float half = 0.5f * (l->x + l->z), span = 0.7072f * l->reach + 1.0f;
    const S32 sLo = half - span < 0.0f ? 0 : (S32)(half - span), sHi = (S32)(half + span) + 1;
    T_DYN_BOX b;
    S32 x, y;
    if (l->level <= 0 || !DynLight_IsoLightBox(l, l->reach, win, &b))
        return;
    for (y = b.y0; y <= b.y1; y++) {
        const U32 row = TabOffLine[y];
        const float rowY = DynLight_IsoRowY(y);
        for (x = b.x0; x <= b.x1; x++) {
            const U32 i = row + (U32)x;
            const S32 s2 = s_isoS[i]; /* DYN_ISO_NONE lies above any sHi */
            float X, Y, Z;
            S32 val, sum;
            if (s2 < sLo || s2 > sHi || !log[i] || !DynLight_IsoAt(i, x, rowY, &X, &Y, &Z))
                continue;
            if (background && log[i] != scr[i])
                continue;
            val = DynLight_IsoAtLight(l, l->level, r2, inv2, s_isoF[i], X, Y, Z);
            if (val <= 0)
                continue;
            sum = val + acc->pix[i];
            acc->pix[i] = (U8)(sum > 255 ? 255 : sum);
        }
    }
    DynLight_Union(u, &b);
}

/* DynLight_Emitter indoors: where the light's sprite is (as AffScene puts it),
   how wide it shows, and 0 when a brick stands between it and the viewer at
   its centre, nearer by more than its diameter. `nearKey` is its own
   DynLight_IsoNear. */
#define DYN_ISO_NEAR_PER_UNIT 1.625f /* DynLight_IsoNear's step for a world unit along the view */
static S32 DynLight_IsoEmitter(const T_DYN_LIGHT *l, S32 scale, S32 *sx, S32 *sy, S32 *rs, float *nearKey) {
    float fx, fy, X, Y, Z;
    S32 r;
    if (l->emitR <= 0.0f)
        return 0;
    DynLight_IsoProject(l->x, l->y, l->z, s_isoCamX, s_isoCamY, s_isoCamZ, s_isoXC, s_isoYC, &fx, &fy);
    *sx = (S32)floorf(fx);
    *sy = (S32)floorf(fy);
    r = (S32)((float)scale * l->emitR * DYN_ISO_RX);
    if (r > (s_w < s_h ? s_w : s_h) / 4)
        r = (s_w < s_h ? s_w : s_h) / 4;
    if (r < 2)
        return 0;
    *rs = r;
    *nearKey = DynLight_IsoNear(l->x, l->y, l->z);
    if (*sx >= 0 && *sx < s_w && *sy >= 0 && *sy < s_h) {
        const U32 i = TabOffLine[*sy] + (U32)*sx;
        if (DynLight_IsoAt(i, *sx, DynLight_IsoRowY(*sy), &X, &Y, &Z) &&
            DynLight_IsoNear(X, Y, Z) > *nearKey + 2.0f * l->emitR * DYN_ISO_NEAR_PER_UNIT)
            return 0;
    }
    return 1;
}

/* Level 1's halo indoors: the bricks about the light, at its depth or
   behind it, brightened in a disc around it on screen. */
static void DynLight_IsoAddHalo(T_DYN_ACC *acc, const T_DYN_LIGHT *l, const T_DYN_BOX *win, S32 background,
                                T_DYN_BOX *u) {
    const U8 *log = (const U8 *)Log, *scr = (const U8 *)Screen;
    S32 sx, sy, rs, rs2, x, y;
    float nearKey, front, inv2;
    T_DYN_BOX b;
    if (l->halo <= 0 || !DynLight_IsoEmitter(l, DYN_HALO_SCALE, &sx, &sy, &rs, &nearKey))
        return;
    front = nearKey + 2.0f * l->emitR * DYN_ISO_NEAR_PER_UNIT;
    rs2 = rs * rs;
    inv2 = 1.0f / (float)rs2;
    b.x0 = sx - rs;
    b.y0 = sy - rs;
    b.x1 = sx + rs;
    b.y1 = sy + rs;
    DynLight_Clip(&b, win);
    if (DynLight_BoxEmpty(&b))
        return;
    for (y = b.y0; y <= b.y1; y++) {
        const U32 row = TabOffLine[y];
        const float rowY = DynLight_IsoRowY(y);
        const S32 dy2 = (y - sy) * (y - sy);
        const S32 half = (S32)sqrtf((float)(rs2 - dy2));
        const S32 x0 = sx - half > b.x0 ? sx - half : b.x0, x1 = sx + half < b.x1 ? sx + half : b.x1;
        for (x = x0; x <= x1; x++) {
            const U32 i = row + (U32)x;
            const S32 r2 = (x - sx) * (x - sx) + dy2;
            float X, Y, Z;
            S32 v;
            /* Bricks only, at the light's depth or behind it: the void and a
               baked object are left as drawn. */
            if (r2 >= rs2 || !log[i] || !DynLight_IsoAt(i, x, rowY, &X, &Y, &Z) || DynLight_IsoNear(X, Y, Z) > front)
                continue;
            if (background && log[i] != scr[i])
                continue;
            v = DynLight_FalloffF((float)r2, inv2, l->halo) + acc->pix[i];
            acc->pix[i] = (U8)(v > 255 ? 255 : v);
        }
    }
    DynLight_Union(u, &b);
}

/*══════════════════════════════════════════════════════════════════════════*
    The colour stage (level 2)
 *══════════════════════════════════════════════════════════════════════════*/

/* The scene as AffScene drew it, before the incrusts: the stage lights a
   presented pixel only where it still shows that, inside the frame's window.
   The tint is kept a quarter of the frame each way: per cell, how far it is
   lit (0..255) and by which of a few light colours. The lamps' tint holds
   until the next full redraw; a frame with moving lights adds theirs to a
   copy. The glows are kept as where and how wide they show. */
#define DYN_GLOWS (DYN_STATIC_LIGHTS + DYN_MOVING_LIGHTS)
#define DYN_TINT_SLOTS 4 /* light colours a frame tints with; a fifth tints nothing */
typedef struct {
    S32 sx, sy, rs, glow;
    S32 front; /* s_glowZ's value at the emitter's near side: what is nearer is not glowed over */
    U8 colour;
} T_DYN_GLOW;
typedef struct {
    U16 *t;        /* how far each cell is lit, summed */
    U8 *best;      /* its strongest light's share */
    U8 *slot;      /* and that light's colour slot */
    T_DYN_BOX box; /* in cells */
    U8 slotColour[DYN_TINT_SLOTS];
    S32 nbSlots;
} T_DYN_TINT;
static U8 *s_snap = NULL;
/* What the glows may cover, inside their discs, taken before the moving
   lights change the frame: how far the background lies, 0 where the frame
   shows an actor or a sprite (or, indoors, the void or a baked object).
   Outdoors the Z-buffer's value; indoors 0xFFFF - DynLight_IsoNear / 2. The
   larger, the farther, either way. */
static U16 *s_glowZ = NULL;
static T_DYN_TINT s_tintStatic, s_tintFrame;
static const T_DYN_TINT *s_tintShown = NULL; /* the one the present reads */
static S32 s_cw = 0, s_ch = 0;
static T_DYN_GLOW s_glow[DYN_GLOWS];
static S32 s_nbGlow = 0;
static T_DYN_BOX s_snapWin = {0, 0, -1, -1};
static S32 s_snapValid = 0, s_snapW = 0, s_snapH = 0;
/* The present's row: each cell's level in 4ths and its slot. */
static S32 *s_rowT = NULL;
static U8 *s_rowSlot = NULL;
/* The conversion of each index tinted at each of 16 levels by each slot's
   colour, for the palette of the present. */
static U32 s_tintLut[DYN_TINT_SLOTS][17][256];

static void DynLight_FreeTint(T_DYN_TINT *t) {
    free(t->t);
    free(t->best);
    free(t->slot);
    t->t = NULL;
    t->best = t->slot = NULL;
    t->box.x0 = t->box.y0 = 0;
    t->box.x1 = t->box.y1 = -1;
    t->nbSlots = 0;
}

static S32 DynLight_AllocTint(T_DYN_TINT *t, size_t cells) {
    t->t = (U16 *)calloc(cells, sizeof(U16));
    t->best = (U8 *)calloc(cells, 1);
    t->slot = (U8 *)calloc(cells, 1);
    t->box.x0 = t->box.y0 = 0;
    t->box.x1 = t->box.y1 = -1;
    t->nbSlots = 0;
    return t->t != NULL && t->best != NULL && t->slot != NULL;
}

static void DynLight_FreeColour(void) {
    free(s_snap);
    free(s_glowZ);
    s_glowZ = NULL;
    DynLight_FreeTint(&s_tintStatic);
    DynLight_FreeTint(&s_tintFrame);
    free(s_rowT);
    free(s_rowSlot);
    s_snap = NULL;
    s_rowT = NULL;
    s_rowSlot = NULL;
    s_tintShown = NULL;
    s_tintStaticValid = 0;
    s_snapW = s_snapH = 0;
    s_snapValid = 0;
}

static S32 DynLight_ColourBuffers(void) {
    const S32 cw = s_w / 4 + 2, ch = s_h / 4 + 2;
    const size_t cells = (size_t)cw * ch;
    S32 ok;
    if (s_snap != NULL && s_snapW == s_w && s_snapH == s_h)
        return 1;
    DynLight_FreeColour();
    s_snap = (U8 *)malloc((size_t)s_w * s_h);
    s_glowZ = (U16 *)malloc(sizeof(U16) * (size_t)s_w * s_h);
    ok = DynLight_AllocTint(&s_tintStatic, cells);
    ok = DynLight_AllocTint(&s_tintFrame, cells) && ok;
    s_rowT = (S32 *)malloc(sizeof(S32) * (size_t)cw);
    s_rowSlot = (U8 *)malloc((size_t)cw);
    if (!ok || s_snap == NULL || s_glowZ == NULL || s_rowT == NULL || s_rowSlot == NULL) {
        DynLight_FreeColour();
        return 0;
    }
    s_cw = cw;
    s_ch = ch;
    s_snapW = s_w;
    s_snapH = s_h;
    return 1;
}

static void DynLight_ClearTint(T_DYN_TINT *t) {
    S32 y;
    if (!DynLight_BoxEmpty(&t->box))
        for (y = t->box.y0; y <= t->box.y1; y++) {
            const size_t i = (size_t)y * s_cw + t->box.x0, n = (size_t)(t->box.x1 - t->box.x0 + 1);
            memset(t->t + i, 0, n * sizeof(U16));
            memset(t->best + i, 0, n);
            memset(t->slot + i, 0, n);
        }
    t->box.x0 = t->box.y0 = 0;
    t->box.x1 = t->box.y1 = -1;
    t->nbSlots = 0;
}

/* `dst` as `src`, `dst`'s own cleared first. */
static void DynLight_CopyTint(T_DYN_TINT *dst, const T_DYN_TINT *src) {
    S32 y;
    DynLight_ClearTint(dst);
    if (!DynLight_BoxEmpty(&src->box))
        for (y = src->box.y0; y <= src->box.y1; y++) {
            const size_t i = (size_t)y * s_cw + src->box.x0, n = (size_t)(src->box.x1 - src->box.x0 + 1);
            memcpy(dst->t + i, src->t + i, n * sizeof(U16));
            memcpy(dst->best + i, src->best + i, n);
            memcpy(dst->slot + i, src->slot + i, n);
        }
    dst->box = src->box;
    memcpy(dst->slotColour, src->slotColour, sizeof(dst->slotColour));
    dst->nbSlots = src->nbSlots;
}

/* The slot of a light colour, taken on first use; -1 past the last. */
static S32 DynLight_Slot(T_DYN_TINT *t, U8 colour) {
    S32 k;
    for (k = 0; k < t->nbSlots; k++)
        if (t->slotColour[k] == colour)
            return k;
    if (t->nbSlots >= DYN_TINT_SLOTS)
        return -1;
    t->slotColour[t->nbSlots] = colour;
    return t->nbSlots++;
}

/* A light's tint over the cells its sphere covers, from the depth at each
   cell's middle pixel. The tint reaches less far than the light: the colour
   gathers near the light, and the stage converts fewer pixels twice. */
#define DYN_TINT_REACH 0.75f
/* DynLight_AddTint indoors: each cell's middle pixel's brick, lit as the
   face it shows takes the light. */
static void DynLight_IsoAddTint(T_DYN_TINT *tt, const T_DYN_LIGHT *l, const T_DYN_BOX *win) {
    const float reach = l->reach * DYN_TINT_REACH;
    const float r2 = reach * reach, inv2 = 1.0f / r2;
    T_DYN_BOX b, c;
    S32 cx, cy, slot;
    /* Only a light that shows takes one of the colours. */
    if (!DynLight_IsoLightBox(l, reach, win, &b))
        return;
    slot = DynLight_Slot(tt, l->colour);
    if (slot < 0)
        return;
    c.x0 = b.x0 >> 2;
    c.y0 = b.y0 >> 2;
    c.x1 = (b.x1 >> 2) + 1;
    c.y1 = (b.y1 >> 2) + 1;
    if (c.x1 > s_cw - 1)
        c.x1 = s_cw - 1;
    if (c.y1 > s_ch - 1)
        c.y1 = s_ch - 1;
    for (cy = c.y0; cy <= c.y1; cy++) {
        const S32 py = cy * 4 + 2 > s_h - 1 ? s_h - 1 : cy * 4 + 2;
        const float rowY = DynLight_IsoRowY(py);
        for (cx = c.x0; cx <= c.x1; cx++) {
            const S32 px = cx * 4 + 2 > s_w - 1 ? s_w - 1 : cx * 4 + 2;
            const U32 i = TabOffLine[py] + (U32)px;
            float X, Y, Z;
            size_t ci;
            S32 t;
            if (!DynLight_IsoAt(i, px, rowY, &X, &Y, &Z))
                continue;
            t = DynLight_IsoAtLight(l, l->tint, r2, inv2, s_isoF[i], X, Y, Z);
            if (t <= 0)
                continue;
            if (t > 255)
                t = 255;
            ci = (size_t)cy * s_cw + cx;
            tt->t[ci] = (U16)(tt->t[ci] + t > 255 ? 255 : tt->t[ci] + t);
            if (t > tt->best[ci]) {
                tt->best[ci] = (U8)t;
                tt->slot[ci] = (U8)slot;
            }
        }
    }
    DynLight_Union(&tt->box, &c);
}

static void DynLight_AddTint(T_DYN_TINT *tt, const T_DYN_LIGHT *l, const T_DYN_BOX *win) {
    const float reach = l->reach * DYN_TINT_REACH;
    const float r2 = reach * reach, inv2 = 1.0f / r2;
    T_DYN_VIEW v;
    T_DYN_BOX b, c;
    S32 cx, cy, slot, zbLo, zbHi;
    if (l->tint <= 0)
        return;
    if (s_iso) {
        DynLight_IsoAddTint(tt, l, win);
        return;
    }
    if (!DynLight_LightBox(l, reach, win, &b))
        return;
    slot = DynLight_Slot(tt, l->colour);
    if (slot < 0)
        return;
    DynLight_View(&v);
    DynLight_ZBlocks();
    if (!DynLight_ZRange(&v, l->z, reach, &zbLo, &zbHi))
        return;
    c.x0 = b.x0 >> 2;
    c.y0 = b.y0 >> 2;
    c.x1 = (b.x1 >> 2) + 1;
    c.y1 = (b.y1 >> 2) + 1;
    if (c.x1 > s_cw - 1)
        c.x1 = s_cw - 1;
    if (c.y1 > s_ch - 1)
        c.y1 = s_ch - 1;
    for (cy = c.y0; cy <= c.y1; cy++) {
        S32 py = cy * 4 + 2;
        float ky;
        if (py > s_h - 1)
            py = s_h - 1;
        ky = -(float)(py - YCentre) * v.invLy;
        for (cx = c.x0; cx <= c.x1; cx++) {
            S32 px = cx * 4 + 2, t;
            U16 zb;
            float d, dx, dy, dz, d2;
            size_t i;
            if (px > s_w - 1)
                px = s_w - 1;
            /* The cells are the depth blocks: one whose depths the sphere's
               miss is passed over without reading the Z-buffer. */
            if (cx < s_bw && cy < s_bh) {
                const size_t bi = (size_t)cy * s_bw + cx;
                if (s_zmax[bi] < zbLo || s_zmin[bi] > zbHi)
                    continue;
            }
            zb = PtrZBuffer[TabOffLine[py] + (U32)px];
            if (zb == 0xFFFF)
                continue;
            d = (float)zb * v.zs;
            dz = d - l->z;
            dx = s_kx[px] * d - l->x;
            dy = ky * d - l->y;
            d2 = dx * dx + dy * dy + dz * dz;
            if (d2 >= r2)
                continue;
            t = (S32)((float)DynLight_FalloffF(d2, inv2, l->tint) * DynLight_Keep(&v, d));
            if (t <= 0)
                continue;
            if (t > 255)
                t = 255;
            i = (size_t)cy * s_cw + cx;
            tt->t[i] = (U16)(tt->t[i] + t > 255 ? 255 : tt->t[i] + t);
            if (t > tt->best[i]) {
                tt->best[i] = (U8)t;
                tt->slot[i] = (U8)slot;
            }
        }
    }
    DynLight_Union(&tt->box, &c);
}

/* A glow about an emitter that shows, and what it may cover: the
   background at the emitter's depth or behind it, never an actor or a nearer
   surface in front of it. A lamp's glow goes when an actor hides its globe;
   a moving light's centre is its own sprite, which the test leaves out. */
/* DynLight_AddGlow indoors. What the glow may cover is kept as how far a
   brick lies along the view, 0xFFFF - DynLight_IsoNear/2, so that, as
   outdoors, the larger the farther; 0 on an actor, on the room's void and
   on an object baked into the background, which the lights leave as drawn. */
static void DynLight_IsoAddGlow(const T_DYN_LIGHT *l, const T_DYN_BOX *win) {
    const U8 *log = (const U8 *)Log, *scr = (const U8 *)Screen;
    T_DYN_GLOW *g;
    T_DYN_BOX b;
    S32 sx, sy, rs, x, y;
    float nearKey, front;
    if (l->glow <= 0 || s_nbGlow >= DYN_GLOWS || !DynLight_IsoEmitter(l, DYN_GLOW_SCALE, &sx, &sy, &rs, &nearKey))
        return;
    front = 65535.0f - 0.5f * (nearKey + 2.0f * l->emitR * DYN_ISO_NEAR_PER_UNIT);
    g = &s_glow[s_nbGlow++];
    g->sx = sx;
    g->sy = sy;
    g->rs = rs;
    g->glow = l->glow;
    g->front = front <= 0.0f ? 0 : (front >= 65535.0f ? 0xFFFF : (S32)front);
    g->colour = l->colour;
    b.x0 = sx - rs;
    b.y0 = sy - rs;
    b.x1 = sx + rs;
    b.y1 = sy + rs;
    DynLight_Clip(&b, win);
    for (y = b.y0; y <= b.y1; y++) {
        const U32 row = TabOffLine[y];
        const float rowY = DynLight_IsoRowY(y);
        const S32 half = (S32)sqrtf((float)(rs * rs - (y - sy) * (y - sy)));
        const S32 x0 = sx - half > b.x0 ? sx - half : b.x0, x1 = sx + half < b.x1 ? sx + half : b.x1;
        U16 *gz = s_glowZ + (size_t)y * s_w;
        for (x = x0; x <= x1; x++) {
            float X, Y, Z, far;
            if (log[row + x] != scr[row + x] || !log[row + x] || !DynLight_IsoAt(row + (U32)x, x, rowY, &X, &Y, &Z))
                gz[x] = 0;
            else {
                far = 65535.0f - 0.5f * DynLight_IsoNear(X, Y, Z);
                gz[x] = (U16)(far < 1.0f ? 1 : (far > 65535.0f ? 65535 : (S32)far));
            }
        }
    }
}

static void DynLight_AddGlow(const T_DYN_LIGHT *l, S32 lamp, const T_DYN_BOX *win) {
    const U8 *log = (const U8 *)Log, *scr = (const U8 *)Screen;
    const float zs = (float)Fill_Z_Fog_Far * (1.0f / 65536.0f);
    const float front = (l->z - 2.0f * l->emitR) / zs;
    T_DYN_GLOW *g;
    T_DYN_BOX b;
    S32 sx, sy, rs, glow, x, y;
    if (s_iso) {
        DynLight_IsoAddGlow(l, win);
        return;
    }
    if (l->glow <= 0 || s_nbGlow >= DYN_GLOWS || !DynLight_Emitter(l, DYN_GLOW_SCALE, &sx, &sy, &rs))
        return;
    if (lamp && sx >= win->x0 && sx <= win->x1 && sy >= win->y0 && sy <= win->y1 &&
        log[TabOffLine[sy] + (U32)sx] != scr[TabOffLine[sy] + (U32)sx])
        return;
    glow = (l->glow * DynLight_FogKeep((S32)l->z, Fill_Z_Fog_Near, Fill_Z_Fog_Far)) >> 8;
    if (glow <= 0)
        return;
    g = &s_glow[s_nbGlow++];
    g->sx = sx;
    g->sy = sy;
    g->rs = rs;
    g->glow = glow;
    g->front = front <= 0.0f ? 0 : (front >= 65535.0f ? 0xFFFF : (S32)front);
    g->colour = l->colour;
    b.x0 = sx - rs;
    b.y0 = sy - rs;
    b.x1 = sx + rs;
    b.y1 = sy + rs;
    DynLight_Clip(&b, win);
    for (y = b.y0; y <= b.y1; y++) { /* the disc's span of each row, as the stage reads it */
        const U32 row = TabOffLine[y];
        const S32 half = (S32)sqrtf((float)(rs * rs - (y - sy) * (y - sy)));
        const S32 x0 = sx - half > b.x0 ? sx - half : b.x0, x1 = sx + half < b.x1 ? sx + half : b.x1;
        U16 *gz = s_glowZ + (size_t)y * s_w;
        for (x = x0; x <= x1; x++)
            gz[x] = log[row + x] == scr[row + x] ? PtrZBuffer[row + x] : 0;
    }
}

/* Whether the static lights (the lamps outdoors) light this frame: outdoors
   at night once baked; indoors there are none. */
static S32 DynLight_StaticOn(void) {
    return !s_iso && DynLight_Night() && s_staticValid;
}

/* Level 2's glows, from the frame before the moving lights change it. */
static void DynLight_Glows(const T_DYN_BOX *win) {
    const S32 night = DynLight_StaticOn();
    S32 k;
    s_nbGlow = 0;
    if (!DynLight_ColourBuffers())
        return;
    if (night)
        for (k = 0; k < s_nbStatic; k++)
            DynLight_AddGlow(&s_static[k], 1, win);
    for (k = 0; k < s_nbMoving; k++)
        DynLight_AddGlow(&s_moving[k], 0, win);
}

/* Keeps what the colour stage reads: the scene, the tint of the lamps (at
   night) and of the moving lights. */
static void DynLight_ColourFrame(const T_DYN_BOX *win) {
    const S32 night = DynLight_StaticOn();
    S32 k, y;
    s_snapValid = 0;
    if (!DynLight_ColourBuffers())
        return;
    for (y = 0; y < s_h; y++)
        memcpy(s_snap + (size_t)y * s_w, (const U8 *)Log + TabOffLine[y], (size_t)s_w);
    if (!s_tintStaticValid) {
        /* The lamps hold still until the next full redraw: their tint covers
           the whole frame, as their bake does, since the cinema bars can
           retract before then; the stage clips it to each frame's window. */
        T_DYN_BOX full;
        full.x0 = 0;
        full.y0 = 0;
        full.x1 = s_w - 1;
        full.y1 = s_h - 1;
        DynLight_ClearTint(&s_tintStatic);
        if (night)
            for (k = 0; k < s_nbStatic; k++)
                DynLight_AddTint(&s_tintStatic, &s_static[k], &full);
        s_tintStaticValid = 1;
    }
    if (!s_nbMoving)
        s_tintShown = &s_tintStatic;
    else {
        DynLight_CopyTint(&s_tintFrame, &s_tintStatic);
        for (k = 0; k < s_nbMoving; k++)
            DynLight_AddTint(&s_tintFrame, &s_moving[k], win);
        s_tintShown = &s_tintFrame;
    }
    s_snapWin = *win;
    s_snapValid = 1;
}

void DynLight_PresentStage(U32 *argb, U32 pitch, U32 w, U32 h, const U8 *pal) {
    const U8 *phys = (const U8 *)Phys;
    const T_DYN_TINT *tint = s_tintShown;
    S32 x, y, k, same = 0, total = 0;
    if (DynLight != 2 || !s_snapValid || argb == NULL || pal == NULL || phys == NULL)
        return;
    if ((S32)w != s_snapW || (S32)h != s_snapH || (S32)ModeResX != s_snapW || (S32)ModeResY != s_snapH)
        return;

    /* Is the scene on screen? A menu, the holomap, a film: most of the frame
       differs from the scene kept, and the stage stands down. */
    for (y = 0; y < (S32)h; y += 8) {
        const U8 *p = phys + TabOffPhysLine[y], *q = s_snap + (size_t)y * w;
        for (x = 0; x < (S32)w; x += 4) {
            same += p[x] == q[x];
            total++;
        }
    }
    if (same * 2 < total)
        return;

    /* The tint: each cell's level, interpolated between the cells' middle
       pixels, rounded to 16 by the dither, picks the index's conversion
       tinted to that level in the slot's colour. */
    if (tint != NULL && !DynLight_BoxEmpty(&tint->box)) {
        const T_DYN_BOX *tb = &tint->box;
        const S32 px0 = tb->x0 * 4 - 2 < s_snapWin.x0 ? s_snapWin.x0 : tb->x0 * 4 - 2;
        const S32 py0 = tb->y0 * 4 - 2 < s_snapWin.y0 ? s_snapWin.y0 : tb->y0 * 4 - 2;
        const S32 px1 = tb->x1 * 4 + 5 > s_snapWin.x1 ? s_snapWin.x1 : tb->x1 * 4 + 5;
        const S32 py1 = tb->y1 * 4 + 5 > s_snapWin.y1 ? s_snapWin.y1 : tb->y1 * 4 + 5;
        const S32 c0 = px0 >> 2 > 0 ? (px0 >> 2) - 1 : 0;
        const S32 c1 = (px1 >> 2) + 1 < s_cw - 1 ? (px1 >> 2) + 1 : s_cw - 1;
        S32 s, lv, i;
        for (s = 0; s < tint->nbSlots; s++) {
            const U8 *lc = pal + 3 * tint->slotColour[s];
            for (lv = 1; lv <= 16; lv++) {
                S32 delta[3];
                U32 *lut = s_tintLut[s][lv];
                DynLight_TintDelta(lc, lv * 16, delta);
                for (i = 0; i < 256; i++) {
                    const U8 *c = pal + 3 * i;
                    lut[i] = 0xFF000000u | (DynLight_Scale(c[0], 128 + delta[0]) << 16) |
                             (DynLight_Scale(c[1], 128 + delta[1]) << 8) | DynLight_Scale(c[2], 128 + delta[2]);
                }
            }
        }
        for (y = py0; y <= py1; y++) {
            const S32 fy = y - 2 < 0 ? 0 : y - 2;
            const S32 cyA = fy >> 2, wy = fy & 3;
            const S32 cyB = cyA + 1 < s_ch ? cyA + 1 : cyA;
            const U16 *ta = tint->t + (size_t)cyA * s_cw, *tbl = tint->t + (size_t)cyB * s_cw;
            const U8 *sa = tint->slot + (size_t)cyA * s_cw, *sb = tint->slot + (size_t)cyB * s_cw;
            const U8 *p = phys + TabOffPhysLine[y], *q = s_snap + (size_t)y * w;
            const U8 *bayer = DynLightBayer + ((y & 3) << 2);
            U32 *d = (U32 *)((U8 *)argb + (size_t)y * pitch);
            S32 any = 0;
            for (k = c0; k <= c1; k++) {
                s_rowT[k] = ta[k] * (4 - wy) + tbl[k] * wy; /* in 4ths */
                /* the slot of the lit one when the other is not */
                s_rowSlot[k] = wy < 2 ? (ta[k] ? sa[k] : sb[k]) : (tbl[k] ? sb[k] : sa[k]);
                any |= s_rowT[k];
            }
            if (!any)
                continue;
            /* Cell by cell: pixels 4c+2..4c+5 lie between the middles of
               cells c and c+1 (the first two pixels of the row take cell 0's). */
            for (k = (px0 - 2) >> 2 < c0 ? c0 : (px0 - 2) >> 2; k <= c1; k++) {
                const S32 kb = k + 1 <= c1 ? k + 1 : k;
                const S32 tA = s_rowT[k], tB = s_rowT[kb];
                const U32(*lutA)[256] = s_tintLut[s_rowSlot[k]], (*lutB)[256] = s_tintLut[s_rowSlot[kb]];
                S32 xs = k * 4 + 2, xe = k * 4 + 5;
                if (k == 0)
                    xs = 0;
                if (xs < px0)
                    xs = px0;
                if (xe > px1)
                    xe = px1;
                if (xs > xe)
                    break;
                if (!tA && !tB)
                    continue;
                for (x = xs; x <= xe; x++) {
                    const S32 wx = x < 2 ? 0 : (x - 2) & 3;
                    const S32 t = (tA * (4 - wx) + tB * wx) >> 4; /* 0..255 */
                    const S32 lvl = (t + bayer[x & 3]) >> 4;
                    if (lvl && p[x] == q[x])
                        d[x] = (!tB ? lutA : (!tA ? lutB : (wx < 2 ? lutA : lutB)))[lvl][p[x]];
                }
            }
        }
    }

    /* The glows, in the colour of the palette the frame was converted with:
       a fade or a lightning flash takes them along. */
    for (k = 0; k < s_nbGlow; k++) {
        const T_DYN_GLOW *g = &s_glow[k];
        const U8 *rgb = pal + 3 * g->colour;
        const S32 rs2 = g->rs * g->rs;
        const float inv2 = 1.0f / (float)rs2;
        const S32 y0 = g->sy - g->rs < s_snapWin.y0 ? s_snapWin.y0 : g->sy - g->rs;
        const S32 y1 = g->sy + g->rs > s_snapWin.y1 ? s_snapWin.y1 : g->sy + g->rs;
        for (y = y0; y <= y1; y++) {
            const S32 dy2 = (y - g->sy) * (y - g->sy);
            const S32 half = (S32)sqrtf((float)(rs2 - dy2));
            const S32 x0 = g->sx - half < s_snapWin.x0 ? s_snapWin.x0 : g->sx - half;
            const S32 x1 = g->sx + half > s_snapWin.x1 ? s_snapWin.x1 : g->sx + half;
            const U8 *p = phys + TabOffPhysLine[y], *q = s_snap + (size_t)y * w;
            const U16 *gz = s_glowZ + (size_t)y * w;
            U32 *d = (U32 *)((U8 *)argb + (size_t)y * pitch);
            for (x = x0; x <= x1; x++) {
                const S32 r2 = (x - g->sx) * (x - g->sx) + dy2;
                S32 f;
                U32 c;
                if (r2 >= rs2 || p[x] != q[x] || gz[x] < g->front)
                    continue;
                f = DynLight_FalloffF((float)r2, inv2, g->glow);
                if (f <= 0)
                    continue;
                c = d[x];
                d[x] = (c & 0xFF000000u) | (DynLight_Screen((c >> 16) & 0xFF, (rgb[0] * f) >> 8) << 16) |
                       (DynLight_Screen((c >> 8) & 0xFF, (rgb[1] * f) >> 8) << 8) |
                       DynLight_Screen(c & 0xFF, (rgb[2] * f) >> 8);
            }
        }
    }
}

/*══════════════════════════════════════════════════════════════════════════*
    The bodies
 *══════════════════════════════════════════════════════════════════════════*/

/* A light as the bodies see it: relative to the camera, in the camera's
   axes (as PosXWr..), with z toward the far side as the body renderer counts
   it, and its reach squared. */
typedef struct {
    float x, y, z;
    float r2;
    S32 level;
} T_DYN_BODYLIGHT;
#define DYN_BODY_LIGHTS (DYN_STATIC_LIGHTS + DYN_MOVING_LIGHTS)
static T_DYN_BODYLIGHT s_body[DYN_BODY_LIGHTS];
static S32 s_nbBody = 0; /* the static ones first, then the moving ones */
static S32 s_nbBodyStatic = 0;
static S32 s_bodyBackground = 0; /* the body being drawn is baked into the background */
static void DynLight_ObjLight(const T_BODY_HEADER *body, const TYPE_MAT *mats, S32 nbMats, S32 posX, S32 posY,
                              S32 posZ, U16 *lights);

/* AFF_OBJ.CPP's groups, which it keeps to itself: each group's pivot group,
   pivot point, points and face normals, in order. */
typedef struct {
    U16 OrgGroupe;
    U16 OrgPoint;
    U16 NbPts;
    U16 NbNorm;
} T_DYN_GROUPE;

#define DYN_BODY_POINTS 550 /* AFF_OBJ.CPP's MAX_NB_POINTS and MAX_NB_POLYS */
static float s_vpos[DYN_BODY_POINTS][3];
static S16 s_faceAt[DYN_BODY_POINTS];

static void DynLight_BodyAdd(float x, float y, float z, float reach, S32 level) {
    T_DYN_BODYLIGHT *b;
    if (s_nbBody >= DYN_BODY_LIGHTS || level <= 0)
        return;
    b = &s_body[s_nbBody++];
    b->x = x;
    b->y = y;
    b->z = z;
    b->r2 = reach * reach;
    b->level = level;
}

/* A light as the bodies see it, from the frame's: outdoors its camera space
   with the depth turned back (the renderer's z grows away from the eye's
   side the other way), indoors its world position less the camera. */
static void DynLight_BodyFrom(const T_DYN_LIGHT *l) {
    if (s_iso)
        DynLight_BodyAdd(l->x - (float)CameraXr, l->y - (float)CameraYr, l->z - (float)CameraZr, l->reach,
                         l->level);
    else
        DynLight_BodyAdd(l->x, l->y, -l->z, l->reach, l->level);
}

void DynLight_BeginBodies(void) {
    S32 k;
    ObjLightHook = NULL;
    s_nbBody = s_nbBodyStatic = 0;
    if (!DynLight)
        return;
    s_iso = CubeMode != CUBE_EXTERIEUR;
    if (DynLight_StaticOn())
        for (k = 0; k < s_nbStatic; k++)
            DynLight_BodyFrom(&s_static[k]);
    s_nbBodyStatic = s_nbBody;
    DynLight_GatherMoving(); /* the moving pass gathers them again, the same, after the bodies */
    for (k = 0; k < s_nbMoving; k++)
        DynLight_BodyFrom(&s_moving[k]);
    if (s_nbBody)
        ObjLightHook = DynLight_ObjLight;
}

void DynLight_EndBodies(void) {
    ObjLightHook = NULL;
}

void DynLight_BodyBackground(S32 background) {
    s_bodyBackground = background;
}

/* ObjLightHook: the lights near the body raise the shade of each point, by
   its normal, then of each face normal, at the point of the first flat
   polygon that uses it. The body's points are rotated again here, from its
   own data and matrices: the renderer's rotated points hold a sort key in
   place of their depth indoors. Nothing but `lights` is written. */
static void DynLight_ObjLight(const T_BODY_HEADER *body, const TYPE_MAT *mats, S32 nbMats, S32 posX, S32 posY,
                              S32 posZ, U16 *lights) {
    const U8 *base = (const U8 *)body;
    const T_OBJ_POINT *pts = (const T_OBJ_POINT *)(base + body->OffPoints);
    const TYPE_VT16 *nrm = (const TYPE_VT16 *)(base + body->OffNormales);
    const TYPE_VT16 *nf = (const TYPE_VT16 *)(base + body->OffNormFaces);
    const T_DYN_GROUPE *grp = (const T_DYN_GROUPE *)(base + body->OffGroupes);
    const S32 nbLights = s_bodyBackground ? s_nbBodyStatic : s_nbBody;
    float rel[DYN_BODY_LIGHTS][4];
    S32 level[DYN_BODY_LIGHTS];
    float size;
    S32 k, n = 0, g, i, faceBase;
    if (body->NbPoints <= 0 || body->NbPoints > DYN_BODY_POINTS || body->NbNormFaces > DYN_BODY_POINTS ||
        nbMats <= 0 || nbMats > 30)
        return;
    /* The body's reach: its box, doubled for the limbs its animation swings. */
    size = (float)(abs(body->XMin) + abs(body->XMax) + abs(body->YMin) + abs(body->YMax) + abs(body->ZMin) +
                   abs(body->ZMax));
    for (k = 0; k < nbLights; k++) {
        const float dx = s_body[k].x - (float)posX, dy = s_body[k].y - (float)posY, dz = s_body[k].z - (float)posZ;
        const float r = sqrtf(s_body[k].r2) + size;
        if (dx * dx + dy * dy + dz * dz >= r * r)
            continue;
        rel[n][0] = dx;
        rel[n][1] = dy;
        rel[n][2] = dz;
        rel[n][3] = s_body[k].r2;
        level[n] = s_body[k].level;
        n++;
    }
    if (!n)
        return;

    /* The points, group by group, each through its group's matrix. */
    i = 0;
    for (g = 0; g < nbMats && i < body->NbPoints; g++) {
        const TYPE_MAT *m = &mats[g];
        const S32 count = nbMats == 1 ? body->NbPoints : grp[g].NbPts;
        S32 j;
        for (j = 0; j < count && i < body->NbPoints; j++, i++) {
            const float px = pts[i].X, py = pts[i].Y, pz = pts[i].Z;
            float *v = s_vpos[i];
            v[0] = m->F.M11 * px + m->F.M12 * py + m->F.M13 * pz + m->F.TX;
            v[1] = m->F.M21 * px + m->F.M22 * py + m->F.M23 * pz + m->F.TY;
            v[2] = m->F.M31 * px + m->F.M32 * py + m->F.M33 * pz + m->F.TZ;
            if (i < body->NbNormales) {
                const float nx = nrm[i].X, ny = nrm[i].Y, nz = nrm[i].Z;
                lights[i] = DynLight_BodyShade(lights[i], v, m->F.M11 * nx + m->F.M12 * ny + m->F.M13 * nz,
                                               m->F.M21 * nx + m->F.M22 * ny + m->F.M23 * nz,
                                               m->F.M31 * nx + m->F.M32 * ny + m->F.M33 * nz, rel, level, n);
            }
        }
    }
    if (body->NbNormFaces <= 0)
        return;

    /* The face normals, which sit at ListLights + NbPoints (ObjectDisplay) or
       + NbNormales (BodyDisplay): each at the first point of the first flat
       polygon that reads it (a polygon's normal index, at byte 10 of every
       record, is absolute). The polygon blocks as DynLight_TopColour walks
       them. */
    faceBase = nbMats == 1 ? body->NbNormales : body->NbPoints;
    for (k = 0; k < body->NbNormFaces; k++)
        s_faceAt[k] = -1;
    {
        const U8 *p = base + body->OffPolys, *end = base + body->OffLines;
        while (p + 8 <= end) {
            const U16 type = ((const U16 *)p)[0], nb = ((const U16 *)p)[1];
            const S32 quad = (type & (1 << 15)) != 0, kind = type & 0xFF;
            const S32 rec = (type & (1 << 14)) ? 16 : (kind <= 7 ? 12 : (quad ? 32 : 24));
            const S32 flat = kind == 1 || kind == 9 || kind == 13 || kind == 17 || kind == 21;
            S32 r;
            p += 8;
            for (r = 0; r < nb && p + rec <= end; r++, p += rec) {
                const S32 f = (S32)((const U16 *)p)[5] - faceBase, p1 = ((const U16 *)p)[0];
                if (flat && f >= 0 && f < body->NbNormFaces && s_faceAt[f] < 0 && p1 < body->NbPoints)
                    s_faceAt[f] = (S16)p1;
            }
        }
    }
    i = 0;
    for (g = 0; g < nbMats && i < body->NbNormFaces; g++) {
        const TYPE_MAT *m = &mats[g];
        const S32 count = nbMats == 1 ? body->NbNormFaces : grp[g].NbNorm;
        S32 j;
        for (j = 0; j < count && i < body->NbNormFaces; j++, i++) {
            const float nx = nf[i].X, ny = nf[i].Y, nz = nf[i].Z;
            if (s_faceAt[i] < 0)
                continue;
            lights[faceBase + i] = DynLight_BodyShade(
                lights[faceBase + i], s_vpos[s_faceAt[i]], m->F.M11 * nx + m->F.M12 * ny + m->F.M13 * nz,
                m->F.M21 * nx + m->F.M22 * ny + m->F.M23 * nz, m->F.M31 * nx + m->F.M32 * ny + m->F.M33 * nz, rel,
                level, n);
        }
    }
}

/*══════════════════════════════════════════════════════════════════════════*
    The frame's pass
 *══════════════════════════════════════════════════════════════════════════*/

/* gfx_dynlight_debug: a cross on each light, in the palette's brightest colour. */
static void DynLight_Crosses(const T_DYN_LIGHT *list, S32 n, const T_DYN_BOX *win) {
    U8 *log = (U8 *)Log;
    S32 k, i, best = 0, bestL = -1;
    for (i = 0; i < 256; i++)
        if (DynLight_Luma(PtrPalCurrent + 3 * i) > bestL) {
            bestL = DynLight_Luma(PtrPalCurrent + 3 * i);
            best = i;
        }
    for (k = 0; k < n; k++) {
        const T_DYN_LIGHT *l = &list[k];
        S32 sx, sy;
        if (s_iso) {
            float fx, fy;
            DynLight_IsoProject(l->x, l->y, l->z, s_isoCamX, s_isoCamY, s_isoCamZ, s_isoXC, s_isoYC, &fx, &fy);
            sx = (S32)floorf(fx);
            sy = (S32)floorf(fy);
        } else {
            if (l->z <= (float)NearClip)
                continue;
            sx = XCentre + (S32)(l->x * (float)LFactorX / l->z);
            sy = YCentre - (S32)(l->y * (float)LFactorY / l->z);
        }
        if (sx - 6 < win->x0 || sx + 6 > win->x1 || sy - 6 < win->y0 || sy + 6 > win->y1)
            continue;
        for (i = -6; i <= 6; i++) {
            log[TabOffLine[sy] + (U32)(sx + i)] = (U8)best;
            log[TabOffLine[sy + i] + (U32)sx] = (U8)best;
        }
        BoxMovingAdd(sx - 6, sy - 6, sx + 6, sy + 6);
    }
}

void DynLight_MovingPass(void) {
    T_DYN_BOX win, u = {0, 0, -1, -1};
    S32 k;
    s_snapValid = 0;
    if (!DynLight)
        return;
    s_iso = CubeMode != CUBE_EXTERIEUR;
    if (s_iso) {
        /* Indoors the room's bricks give the positions (DynLight_IsoBegin):
           the exteriors' Z-buffer memory holds the bricks themselves. */
        s_staticValid = 0;
        s_zValid = 0;
        if (!DynLight_IsoReady() || !DynLight_Surf())
            return;
        DynLight_IsoColumns();
    } else {
        if (!DynLight_Buffers() || !DynLight_Surf())
            return;
        DynLight_Columns();
    }
    win.x0 = ClipWindowXMin;
    win.y0 = ClipWindowYMin;
    win.x1 = ClipWindowXMax;
    win.y1 = ClipWindowYMax;
    if (win.x0 < 0)
        win.x0 = 0;
    if (win.y0 < 0)
        win.y0 = 0;
    if (win.x1 > s_w - 1)
        win.x1 = s_w - 1;
    if (win.y1 > s_h - 1)
        win.y1 = s_h - 1;
    if (CinemaMode) {
        /* The bars end at rows 0..39 and h-40..h-1 (FixeCinemaMode): a box
           reaching there while they slide in would be cleaned from the
           background after they land, leaving a strip of scene in them. */
        if (win.y0 < 40)
            win.y0 = 40;
        if (win.y1 > s_h - 41)
            win.y1 = s_h - 41;
    }
    if (DynLight_BoxEmpty(&win))
        return;

    DynLight_GatherMoving();
    DynLight_Clear(&s_movAcc);
    for (k = 0; k < s_nbMoving; k++) {
        if (s_iso) {
            DynLight_IsoAddPool(&s_movAcc, &s_moving[k], &win, 1, &u);
            if (DynLight == 1)
                DynLight_IsoAddHalo(&s_movAcc, &s_moving[k], &win, 1, &u);
        } else {
            DynLight_AddPool(&s_movAcc, &s_moving[k], &win, 1, &u);
            if (DynLight == 1)
                DynLight_AddHalo(&s_movAcc, &s_moving[k], &win, 1, &u);
        }
    }
    s_movAcc.box = u;
    if (DynLight == 2)
        DynLight_Glows(&win); /* while the frame's background still equals the background copy */
    if (!DynLight_BoxEmpty(&u)) {
        DynLight_Apply((U8 *)Log, &s_movAcc, 1);
        BoxMovingAdd(u.x0, u.y0, u.x1, u.y1);
    }

    if (DynLight == 2)
        DynLight_ColourFrame(&win);

    if (DynLightDebug) {
        DynLight_Crosses(s_static, s_nbStatic, &win);
        DynLight_Crosses(s_moving, s_nbMoving, &win);
    }
}
