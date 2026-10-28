#include "DYNLIGHT.H"
#include "C_EXTERN.H" /* FirstTime, CubeMode, ListExtra, ListObjet, PtrPalCurrent, IdxPalette, sprites */
#include "FLOW.H"     /* ListPartFlow: the magic ball's burst */
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

/* A light of the frame, in camera space: x right, y up, z depth, world units,
   as the exterior projection sees it (Xp = XCentre + x*LFactorX/z). */
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

/* The colour a sprite gives, its brightest pixel, per palette. HQR_Get touches
   the cache's order, so each sprite is read once per palette. */
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
            S32 i;
            for (i = 0; i < n; i++) {
                S32 l;
                if (!px[i])
                    continue;
                l = DynLight_Luma(PtrPalCurrent + 3 * px[i]);
                if (l > bestL) {
                    bestL = l;
                    best = px[i];
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
static const T_DYN_KIND DynKindBurst = {2000, 128, 0, 0, 0, 160};
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
    DynLight_ToCam(wx, wy, wz, l);
    if (l->z + k->reach <= (float)NearClip || l->z - k->reach >= (float)Fill_Z_Fog_Far)
        return;
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
   so a light just behind the camera still lights the ground in view), the
   flows alive, the hero on fire. */
static void DynLight_GatherMoving(void) {
    const S32 day = DynLight_Night() ? 256 : DYN_DAY_SCALE;
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
        DynLight_AddMoving(sx / live, sy / live, sz / live, &DynKindBurst, (day * live) / f->NbDot, colour);
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
    if (s_staticAcc.pix == NULL || s_staticAcc.blk == NULL || s_movAcc.pix == NULL || s_movAcc.blk == NULL ||
        s_ref == NULL || s_base == NULL || s_lit == NULL || s_kx == NULL || s_zmin == NULL || s_zmax == NULL) {
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

void DynLight_Baked(S32 x0, S32 y0, S32 x1, S32 y1) {
    const U8 *scr = (const U8 *)Screen;
    T_DYN_BOX b;
    S32 x, y;
    if (!DynLight || !DynLight_StaticBox(x0, y0, x1, y1, &b))
        return;
    for (y = b.y0; y <= b.y1; y++) {
        const U32 row = TabOffLine[y];
        for (x = b.x0; x <= b.x1; x++) {
            const U32 i = row + (U32)x;
            if (scr[i] != s_ref[i]) { /* the object's pixel: the lamps leave it */
                s_lit[i] = 0;
                s_base[i] = scr[i];
                s_ref[i] = scr[i];
            }
        }
    }
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
    S32 front; /* the Z-buffer value of the emitter's near side: what is nearer is not glowed over */
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
/* What the glows may cover, inside their discs: the Z-buffer's value where
   the frame shows the background, 0 where it shows an actor or a sprite,
   taken before the moving lights change the frame. */
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
static void DynLight_AddTint(T_DYN_TINT *tt, const T_DYN_LIGHT *l, const T_DYN_BOX *win) {
    const float reach = l->reach * DYN_TINT_REACH;
    const float r2 = reach * reach, inv2 = 1.0f / r2;
    T_DYN_VIEW v;
    T_DYN_BOX b, c;
    S32 cx, cy, slot, zbLo, zbHi;
    if (l->tint <= 0 || !DynLight_LightBox(l, reach, win, &b))
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
static void DynLight_AddGlow(const T_DYN_LIGHT *l, S32 lamp, const T_DYN_BOX *win) {
    const U8 *log = (const U8 *)Log, *scr = (const U8 *)Screen;
    const float zs = (float)Fill_Z_Fog_Far * (1.0f / 65536.0f);
    const float front = (l->z - 2.0f * l->emitR) / zs;
    T_DYN_GLOW *g;
    T_DYN_BOX b;
    S32 sx, sy, rs, glow, x, y;
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

/* Level 2's glows, from the frame before the moving lights change it. */
static void DynLight_Glows(const T_DYN_BOX *win) {
    const S32 night = DynLight_Night() && s_staticValid;
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
    const S32 night = DynLight_Night() && s_staticValid;
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
        if (l->z <= (float)NearClip)
            continue;
        sx = XCentre + (S32)(l->x * (float)LFactorX / l->z);
        sy = YCentre - (S32)(l->y * (float)LFactorY / l->z);
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
    if (CubeMode != CUBE_EXTERIEUR) { /* no depth indoors: the light waits for the next exterior */
        s_staticValid = 0;
        s_zValid = 0; /* the Z-buffer's memory holds the bricks indoors */
        return;
    }
    if (!DynLight_Buffers() || !DynLight_Surf())
        return;
    DynLight_Columns();
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
        DynLight_AddPool(&s_movAcc, &s_moving[k], &win, 1, &u);
        if (DynLight == 1)
            DynLight_AddHalo(&s_movAcc, &s_moving[k], &win, 1, &u);
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
