#ifndef DYNLIGHT_H
#define DYNLIGHT_H

#include <SYSTEM/ADELINE_TYPES.H>
#include <OBJECT/AFF_OBJ.H> /* T_BODY_HEADER, T_OBJ_POINT: a lamp's globe colour */

#include <math.h> /* sqrt: the rows a light reaches */

struct T_SETTING; /* SETTINGS.H; a pointer to a table needs no more */

/* Dynamic lighting.
 *
 * The 1997 engine has no light in the scene: one direction lights the 3D
 * bodies of a scene, the terrain's light is baked into the island's data,
 * the sprites (the magic ball, the sparks, the flames) are drawn as they are,
 * and every colour is an index into a 256-entry palette. DynLight adds point
 * lights in two layers:
 *
 *   1, Dynamic: in the palette. The street lamps of an island lit at night
 *      cast a pool on the ground and the walls around them, baked into the
 *      background on each full redraw with a halo about each globe, and the
 *      magic ball, the spells and the flames light what they pass over,
 *      every frame, outdoors and indoors. A palette pixel can only climb its
 *      own 16-shade ramp, so these pools are brightness, not colour.
 *   2, Colour: the same pools, and a stage at present time, where the frame
 *      is turned from palette indices into true colour: the lights tint what
 *      they light in their own colour, and a glow in that colour surrounds
 *      the globes and the moving lights in place of the palette's halo.
 *
 * Off, the default, every hook returns before it reads anything (but the two
 * that note which island and cube are loaded) and the frames are
 * byte-identical to the original. Not recorded: the passes read
 * the scene and write pixels, never game state, and draw nothing from the
 * shared random stream. */

/* 0: the 1997 light. 1: dynamic, in the palette. 2: and colour at present. */
extern S32 DynLight;

/* The arithmetic, over plain values and free of engine state, so the host test
   in tests/dynlight covers it without booting anything. */

/* An exterior cube's baked light: 65x65 vertices, 512 world units apart, the
   light in the low nibble of each byte (the high nibble is the water depth
   gameplay reads, never touched here). */
#define DYN_LIGHT_GRID 65
#define DYN_LIGHT_CELL 512

/* A lamp's pool in the baked light: the bright round patch the 1997 artists
   painted under each street lamp. Found where the light under a cell reaches
   DYN_LIGHT_POOL_PEAK and stands DYN_LIGHT_POOL_LIFT above the ground around
   it, a ring 5 to 6 cells out; on the Citadel every street lamp stands on one
   (14 against 7) and no other decor does but those set beside a lamp. */
#define DYN_LIGHT_POOL_PEAK 13
#define DYN_LIGHT_POOL_LIFT 5

typedef struct {
    S32 x16, z16; /* the pool's centre, in sixteenths of a cell */
    S32 peak;     /* its brightest baked light, 0..15 */
    S32 ground;   /* the ground's light around it */
    S32 radius16; /* how far it reaches, in sixteenths of a cell */
} T_DYN_POOL;

static inline S32 DynLight_Cell(const U8 *map, S32 x, S32 z) {
    x = (x < 0) ? 0 : (x >= DYN_LIGHT_GRID ? DYN_LIGHT_GRID - 1 : x);
    z = (z < 0) ? 0 : (z >= DYN_LIGHT_GRID ? DYN_LIGHT_GRID - 1 : z);
    return map[z * DYN_LIGHT_GRID + x] & 15;
}

/* Whether a pool lies under cell (x, z) of a cube's baked light `map`, and if
   so where its centre is and how far it reaches: the centre is the
   light-weighted mean of the cells within 4 that stand above the ground, the
   reach the radius of a disc of as many cells. */
static inline S32 DynLight_PoolAt(const U8 *map, S32 x, S32 z, T_DYN_POOL *pool) {
    S32 ring[64];
    S32 n = 0, peak = 0, i, j, a, b, ground;
    S32 sw = 0, sx = 0, sz = 0, cells = 0;

    for (j = -1; j <= 1; j++)
        for (i = -1; i <= 1; i++)
            if (DynLight_Cell(map, x + i, z + j) > peak)
                peak = DynLight_Cell(map, x + i, z + j);
    if (peak < DYN_LIGHT_POOL_PEAK)
        return 0;

    for (j = -6; j <= 6; j++)
        for (i = -6; i <= 6; i++)
            if (i * i + j * j >= 25 && i * i + j * j <= 36 && n < 64)
                ring[n++] = DynLight_Cell(map, x + i, z + j);
    for (a = 1; a < n; a++) /* insertion sort: a few dozen values */
        for (b = a; b > 0 && ring[b - 1] > ring[b]; b--) {
            const S32 t = ring[b];
            ring[b] = ring[b - 1];
            ring[b - 1] = t;
        }
    ground = ring[n / 2];
    if (peak - ground < DYN_LIGHT_POOL_LIFT)
        return 0;

    for (j = -4; j <= 4; j++)
        for (i = -4; i <= 4; i++) {
            const S32 w = DynLight_Cell(map, x + i, z + j) - ground - 1;
            if (i * i + j * j > 16 || w <= 0)
                continue;
            sw += w;
            sx += w * (x + i);
            sz += w * (z + j);
            cells++;
        }
    pool->x16 = (sx * 16 + sw / 2) / sw;
    pool->z16 = (sz * 16 + sw / 2) / sw;
    pool->peak = peak;
    pool->ground = ground;
    /* A disc of `cells` cells: r = sqrt(cells / pi), in sixteenths. */
    {
        S32 r = 16;
        while (r * r * 355 < cells * 256 * 113) /* r^2 < cells/pi, in sixteenths squared */
            r++;
        pool->radius16 = r;
    }
    return 1;
}

/* A palette entry's luma, 0..255, from 8-bit RGB. */
static inline S32 DynLight_Luma(const U8 *rgb) {
    return (rgb[0] * 77 + rgb[1] * 150 + rgb[2] * 29) >> 8;
}

/* A colour's hue in degrees (0..360) and its chroma (max - min channel). */
static inline S32 DynLight_Hue(const U8 *rgb, S32 *chroma) {
    const S32 r = rgb[0], g = rgb[1], b = rgb[2];
    const S32 mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
    const S32 mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
    const S32 c = mx - mn;
    S32 h;
    *chroma = c;
    if (c == 0)
        return 0;
    if (mx == r)
        h = (60 * (g - b)) / c;
    else if (mx == g)
        h = 120 + (60 * (b - r)) / c;
    else
        h = 240 + (60 * (r - g)) / c;
    return h < 0 ? h + 360 : h;
}

/* Whether a lit colour may become entry e of its ramp. A ramp is the
   shading of one material, and climbs it as the artists drew it, but the
   palette's first and last ramps are not: they hold primaries and odd
   colours side by side (a dark red next to a violet, an orange next to a
   cyan, then whites). There a colour with a hue climbs only to entries of a
   hue within DYN_LIGHT_HUE_SPAN, or to a near white; a grey, whose hue means
   nothing, climbs anywhere. */
#define DYN_LIGHT_HUE_SPAN 30 /* degrees */
#define DYN_LIGHT_GREY 24     /* chroma under which a colour has no hue to keep */
static inline S32 DynLight_SameHue(const U8 *pal, S32 c, S32 e) {
    S32 cc, ce, hc, he, d;
    if (e == c || ((c >> 4) != 0 && (c >> 4) != 15))
        return 1;
    hc = DynLight_Hue(pal + c * 3, &cc);
    if (cc < DYN_LIGHT_GREY)
        return 1;
    he = DynLight_Hue(pal + e * 3, &ce);
    if (ce < DYN_LIGHT_GREY + 8)
        return DynLight_Luma(pal + e * 3) >= 160; /* a near white: a hot colour whitens */
    d = hc > he ? hc - he : he - hc;
    if (d > 180)
        d = 360 - d;
    return d <= DYN_LIGHT_HUE_SPAN;
}

/* The surface table: SurfLUT[k][c] is colour c lit at level k (0..15), the
   entry of c's own 16-shade ramp, at or above c's step and of its hue
   (DynLight_SameHue), whose luma is closest to luma(c) brightened by k/15 of
   a gain and a lift. Level 0 leaves every colour as it is, and every level
   keeps a colour in its ramp, so the fog and shade tables that index by ramp
   still read it right. Colour 0, the clear colour (the void about an
   interior's room), is left black. `pal` is 768 bytes of 8-bit RGB. */
#define DYN_LIGHT_GAIN 256 /* luma * (1 + gain/256 * k/15) */
#define DYN_LIGHT_LIFT 40  /* + lift * k/15 */
static inline void DynLight_BuildSurf(const U8 *pal, U8 *lut) {
    S32 k, c, s;
    for (c = 0; c < 256; c++)
        lut[c] = (U8)c;
    for (k = 1; k < 16; k++) {
        lut[k * 256] = 0;
        for (c = 1; c < 256; c++) {
            const S32 luma = DynLight_Luma(pal + c * 3);
            const S32 target = luma + (luma * DYN_LIGHT_GAIN * k) / (256 * 15) + (DYN_LIGHT_LIFT * k) / 15;
            S32 best = c, bestErr = 0x7FFFFFFF;
            for (s = c & 15; s < 16; s++) {
                const S32 e = (c & 0xF0) | s;
                const S32 d = DynLight_Luma(pal + e * 3) - target;
                const S32 err = d < 0 ? -d : d;
                if (err < bestErr && DynLight_SameHue(pal, c, e)) {
                    bestErr = err;
                    best = e;
                }
            }
            lut[k * 256 + c] = (U8)best;
        }
    }
}

/* A point light's level at a point of the scene, squared distance d2 from it,
   for a light reaching r2 (squared) at full strength `level` (0..255, in
   sixteenths of a table level): (1 - d2/r2)^2 of it, nothing past the reach. */
static inline S32 DynLight_Falloff(S32 d2, S32 r2, S32 level) {
    S32 a;
    if (d2 >= r2 || r2 <= 0)
        return 0;
    a = (S32)(((S64)(r2 - d2) << 8) / r2); /* 0..256 */
    return (level * ((a * a) >> 8)) >> 8;
}

/* A street lamp, told from the other decors standing on a bright patch: the
   pool under it has to be filled (DYN_LAMP_FILL_MIN percent of the vertices
   within 2.5 cells stand 4 above the ground), ringed with dark ground
   (DYN_LAMP_RING_DARK_MIN percent of the ring 5 to 6 cells out stand within 2
   of it) and centred on the decor (within DYN_LAMP_OFF_MAX16 sixteenths of a
   cell). On the 14 islands this keeps the Citadel's nine street lamps and no
   other decor: a bright plateau fails the ring, a patch beside a lamp fails
   the centre, a lit slope fails the fill. `x`, `z` are the decor's cube-local
   world position. */
#define DYN_LAMP_FILL_MIN 85
#define DYN_LAMP_RING_DARK_MIN 75
#define DYN_LAMP_OFF_MAX16 12
static inline S32 DynLight_LampAt(const U8 *map, S32 x, S32 z, T_DYN_POOL *pool) {
    const S32 gx16 = x >> 5, gz16 = z >> 5; /* the decor, in sixteenths of a cell */
    const S32 cx = (x + DYN_LIGHT_CELL / 2) >> 9, cz = (z + DYN_LIGHT_CELL / 2) >> 9;
    S32 i, j, tot = 0, lit = 0, rtot = 0, rdark = 0, dx, dz;
    if (!DynLight_PoolAt(map, cx, cz, pool))
        return 0;
    for (j = -3; j <= 3; j++)
        for (i = -3; i <= 3; i++) {
            dx = (cx + i) * 16 - gx16;
            dz = (cz + j) * 16 - gz16;
            if (dx * dx + dz * dz > 40 * 40) /* 2.5 cells */
                continue;
            tot++;
            if (DynLight_Cell(map, cx + i, cz + j) >= pool->ground + 4)
                lit++;
        }
    if (lit * 100 < tot * DYN_LAMP_FILL_MIN)
        return 0;
    for (j = -6; j <= 6; j++)
        for (i = -6; i <= 6; i++)
            if (i * i + j * j >= 25 && i * i + j * j <= 36) {
                rtot++;
                if (DynLight_Cell(map, cx + i, cz + j) <= pool->ground + 2)
                    rdark++;
            }
    if (rdark * 100 < rtot * DYN_LAMP_RING_DARK_MIN)
        return 0;
    dx = pool->x16 - gx16;
    dz = pool->z16 - gz16;
    return dx * dx + dz * dz <= DYN_LAMP_OFF_MAX16 * DYN_LAMP_OFF_MAX16;
}

/* The colour of a body's highest polygon, judged by its highest vertex: a
   street lamp's globe (108 on the Citadel's). -1 when the body has none. The
   polygon blocks are walked as the renderer walks them: an 8-byte header
   {type, count, offset of the next}, then `count` records of 16 bytes for the
   environment-mapped types, 12 for the plain ones (type & 0xFF up to 7), 32 or
   24 for the textured quads and triangles; each record starts with its vertex
   indices and has its colour at byte 8, the filler reading its low byte. The
   body is in the CPU's order, as HQR_Get's fixup leaves it, and its offsets
   are trusted as the renderer trusts them: only a header out of order is
   refused, and a vertex index past the points is passed over. */
static inline S32 DynLight_TopColour(const U8 *body) {
    const T_BODY_HEADER *b = (const T_BODY_HEADER *)body;
    const T_OBJ_POINT *pts;
    const U8 *p, *end;
    S32 bestY = -0x7FFFFFFF, colour = -1;
    if (b->OffPoints < (S32)sizeof(T_BODY_HEADER) || b->NbPoints < 0 || b->OffPolys < b->OffPoints ||
        b->OffLines < b->OffPolys)
        return -1;
    pts = (const T_OBJ_POINT *)(body + b->OffPoints);
    p = body + b->OffPolys;
    end = body + b->OffLines;
    while (p + 8 <= end) {
        const U16 type = ((const U16 *)p)[0], nb = ((const U16 *)p)[1];
        const S32 quad = (type & (1 << 15)) != 0;
        const S32 rec = (type & (1 << 14)) ? 16 : ((type & 0xFF) <= 7 ? 12 : (quad ? 32 : 24));
        S32 k, v;
        p += 8;
        for (k = 0; k < nb && p + rec <= end; k++, p += rec) {
            S32 y = -0x7FFFFFFF;
            for (v = 0; v < (quad ? 4 : 3); v++) {
                const S32 idx = ((const U16 *)p)[v];
                if (idx < b->NbPoints && pts[idx].Y > y)
                    y = pts[idx].Y;
            }
            if (y > bestY) {
                bestY = y;
                colour = ((const U16 *)p)[4] & 0xFF;
            }
        }
    }
    return colour;
}

/* DynLight_Falloff in floating point, for the passes that run per pixel:
   `inv2` is 1/r2. Within 3 sixteenths of the integer form, which rounds down
   at each of its three steps. */
static inline S32 DynLight_FalloffF(float d2, float inv2, S32 level) {
    const float a = 1.0f - d2 * inv2;
    return a <= 0.0f ? 0 : (S32)((float)level * a * a);
}

/* The columns of one screen row whose rays pass through a sphere: with the
   row's ray slope ky (y up, per unit of depth) and a column's kx, the ray
   (kx, ky, 1) meets the sphere of centre (cx, cy, cz) and squared radius r2
   when (|c|^2 - r2)(kx^2 + ky^2 + 1) < (c . (kx, ky, 1))^2, a quadratic in kx.
   0 when no column of the row does; otherwise [*k0, *k1] in kx, every column
   when the camera is inside the sphere or the sphere is as wide as the view.
   In doubles: the terms are the squares of squares of world distances. */
static inline S32 DynLight_RowSpan(float cx, float cy, float cz, float r2, float ky, float *k0, float *k1) {
    const double w = (double)cy * ky + cz;
    const double q = (double)cx * cx + (double)cy * cy + (double)cz * cz - r2;
    const double a = q - (double)cx * cx;
    const double b = -2.0 * cx * w;
    const double c = q * ((double)ky * ky + 1.0) - w * w;
    double disc, s;
    if (q <= 0.0 || a <= 0.0) {
        *k0 = -1e30f;
        *k1 = 1e30f;
        return 1;
    }
    disc = b * b - 4.0 * a * c;
    if (disc < 0.0)
        return 0;
    s = sqrt(disc);
    *k0 = (float)((-b - s) / (2.0 * a));
    *k1 = (float)((-b + s) / (2.0 * a));
    return 1;
}

/* The screen box a sphere covers, from its centre in camera space (x right, y
   up, z depth) and its radius, under the exterior projection: Xp = xc + x*fx/z,
   Yp = yc - y*fy/z. Conservative: the box of the corners of the sphere's
   bounding cube. 0 when the sphere lies wholly nearer than `nearZ`; when it
   reaches nearer, the box is the whole screen (`*full` set). */
typedef struct {
    S32 x0, y0, x1, y1; /* inclusive; empty when x1 < x0 */
} T_DYN_BOX;
static inline S32 DynLight_SphereBox(float x, float y, float z, float r, float fx, float fy, S32 xc, S32 yc,
                                     float nearZ, T_DYN_BOX *box, S32 *full) {
    float zn, zf, a, b, c, d;
    *full = 0;
    if (z + r <= nearZ)
        return 0;
    zn = z - r;
    zf = z + r;
    if (zn <= nearZ) {
        *full = 1;
        return 1;
    }
    a = (x - r) / zn;
    b = (x - r) / zf;
    c = (x + r) / zn;
    d = (x + r) / zf;
    box->x0 = xc + (S32)((a < b ? a : b) * fx) - 1;
    box->x1 = xc + (S32)((c > d ? c : d) * fx) + 1;
    a = (y + r) / zn;
    b = (y + r) / zf;
    c = (y - r) / zn;
    d = (y - r) / zf;
    box->y0 = yc - (S32)((a > b ? a : b) * fy) - 1;
    box->y1 = yc - (S32)((c < d ? c : d) * fy) + 1;
    return 1;
}

/* How much of a light survives the fog at depth d, 0..256: all of it before
   the fog starts, none at the far clip, linear between, as the fog bands of
   the fillers run. */
static inline S32 DynLight_FogKeep(S32 d, S32 fogNear, S32 fogFar) {
    if (d <= fogNear)
        return 256;
    if (d >= fogFar || fogFar <= fogNear)
        return 0;
    return (S32)(((S64)(fogFar - d) << 8) / (fogFar - fogNear));
}

/* The 4x4 ordered dither the passes round a pixel's level with. */
static const U8 DynLightBayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};

/* A level in sixteenths, rounded to a table level by the dither at (x, y). */
static inline S32 DynLight_Level(S32 v, S32 x, S32 y) {
    const S32 l = (v + DynLightBayer[((y & 3) << 2) | (x & 3)]) >> 4;
    return l > 15 ? 15 : l;
}

/* The colour stage's tint, a channel factor in 128ths (128 leaves it as it
   is) for a light of colour `rgb` lighting a pixel `t` of the way (0..256):
   the channels the colour is rich in rise, the others fall, around its luma,
   by DYN_TINT_CHROMA, and the whole by DYN_TINT_LIFT. */
#define DYN_TINT_CHROMA 160 /* in 128ths of the colour's own spread */
#define DYN_TINT_LIFT 20    /* in 128ths */
static inline void DynLight_TintDelta(const U8 *rgb, S32 t, S32 *delta) {
    S32 m = rgb[0] > rgb[1] ? rgb[0] : rgb[1], ch;
    S32 luma;
    if (rgb[2] > m)
        m = rgb[2];
    if (m <= 0) {
        delta[0] = delta[1] = delta[2] = 0;
        return;
    }
    luma = DynLight_Luma(rgb);
    for (ch = 0; ch < 3; ch++) {
        /* (channel - luma) / max: the colour's lean, in 256ths */
        const S32 lean = ((rgb[ch] - luma) << 8) / m;
        delta[ch] = (t * ((lean * DYN_TINT_CHROMA) / 256 + DYN_TINT_LIFT)) >> 8;
    }
}

/* A channel times a factor in 128ths, clamped to 255. */
static inline U32 DynLight_Scale(U32 c, S32 f) {
    const S32 v = ((S32)c * f) >> 7;
    return (U32)(v < 0 ? 0 : (v > 255 ? 255 : v));
}

/* A channel brightened toward white by `g` (0..255), a screen blend: the
   light adds less where the pixel is already bright, and never wraps. */
static inline U32 DynLight_Screen(U32 c, S32 g) {
    return c + (((255 - c) * (U32)g) >> 8);
}

/* Interiors. They are drawn from isometric bricks, with no Z-buffer (its
   memory holds the room's grid and bricks), so the background's position is
   rebuilt from the bricks themselves while they are drawn.

   The projection (LongProjectPointIso), with x' = X - CameraX and y', z'
   likewise: Xp = XCentre + (3(x'-z') >> 6), Yp = YCentre + 1 + ((6(x'+z') -
   15y') >> 8). It looks along (1, 0.8, 1): every point (5, 4, 5)k from a
   point lands on its pixel, and the one drawn there is the one with the
   largest X + 0.8Y + Z. A brick of cell (cx, cy, cz), 512 x 256 x 512 world
   units, is drawn with its sprite's corner at the projection of the cell's
   low corner less (23, 15) (Map2Screen), and every opaque pixel of every
   brick of the game lies inside the cell's silhouette: its top face, its
   face toward +x (the sprite's right half) or toward +z (its left half). So
   a pixel's position is its cell's plus where it sits in the 48 x 38 sprite,
   whatever the camera and the resolution.

   DynLight_IsoT3: the face a pixel (u, v) of a cell's sprite shows, and
   T3 = 3((X + Z) - 512(cx + cz)) of its point, which grows toward the
   viewer. The eye's ray enters the cell through the face whose plane it
   crosses last of the three on its way in, the one with the least T3:
   T3 = min(128(v + 1), 3072 - |64u - 1504|). */
#define DYN_ISO_TOP 0 /* the face toward +y */
#define DYN_ISO_PX 1  /* toward +x */
#define DYN_ISO_PZ 2  /* toward +z */
#define DYN_ISO_W 48  /* a brick sprite's box, from its corner */
#define DYN_ISO_H 38
static inline S32 DynLight_IsoT3(S32 u, S32 v, S32 *face) {
    const S32 d = 64 * u - 1504;
    const S32 side = 3072 - (d < 0 ? -d : d);
    const S32 top = 128 * (v + 1);
    if (top <= side) {
        *face = DYN_ISO_TOP;
        return top;
    }
    *face = u >= 24 ? DYN_ISO_PX : DYN_ISO_PZ;
    return side;
}

/* What a background pixel keeps of its point: (X + Z) / 2, rounded, which
   fits 16 bits (X and Z run 0..32768). */
static inline U16 DynLight_IsoS2(S32 cx, S32 cz, S32 t3) {
    return (U16)((1536 * (cx + cz) + t3 + 3) / 6);
}
#define DYN_ISO_NONE 0xFFFF /* no brick there */

/* The world point the background pixel (px, py) shows, from its kept
   (X + Z) / 2 and the camera and centre it was drawn with: X - Z from the
   column (the brick's pixel centre, half a pixel left of an object's), Y
   from the row and X + Z. */
static inline void DynLight_IsoPoint(S32 s2, S32 px, S32 py, S32 camX, S32 camY, S32 camZ, S32 xc, S32 yc,
                                     float *X, float *Y, float *Z) {
    const float S = 2.0f * (float)s2;
    const float D = (64.0f * (float)(px - xc) - 32.0f) / 3.0f + (float)(camX - camZ);
    *X = 0.5f * (S + D);
    *Z = 0.5f * (S - D);
    *Y = (float)camY + (6.0f * (S - (float)(camX + camZ)) - 256.0f * (float)(py - yc)) / 15.0f;
}

/* Where a world point is drawn, as LongProjectPointIso puts it, unrounded. */
static inline void DynLight_IsoProject(float X, float Y, float Z, S32 camX, S32 camY, S32 camZ, S32 xc, S32 yc,
                                       float *sx, float *sy) {
    const float x = X - (float)camX, y = Y - (float)camY, z = Z - (float)camZ;
    *sx = (float)xc + 3.0f * (x - z) / 64.0f;
    *sy = (float)yc + 1.0f + (6.0f * (x + z) - 15.0f * y) / 256.0f;
}

/* How near the viewer a point is: larger is nearer, along the view; a step
   of one world unit toward the viewer adds about 1.625. */
static inline float DynLight_IsoNear(float X, float Y, float Z) {
    return X + 0.8f * Y + Z;
}

/* How strong the lights are in an interior, in 256ths, from the mean luma of
   its bricks: full in a room as dark as a night outdoors (48 and under),
   half in the brightest (112 and over), between in proportion. Interiors
   have no time of day; their darkness is their own. */
static inline S32 DynLight_IsoStrength(S32 ambient) {
    if (ambient <= 48)
        return 256;
    if (ambient >= 112)
        return 128;
    return 256 - (ambient - 48) * 2;
}

/* A sphere of radius r on screen: an ellipse of half-width 3r/sqrt(2)/32 and
   half-height r sqrt(297)/256 about the projection of its centre. */
#define DYN_ISO_RX 0.06630f
#define DYN_ISO_RY 0.06732f

/* How much of a point light a face takes: the cosine between its normal and
   the light, wrapped by DYN_ISO_WRAP so the light rounds the edges a little;
   a face turned away, such as a wall's visible side lit from the room behind
   it, takes none. `dot` is the normal's dot with the point-to-light vector,
   `dist` that vector's length. 0..256. */
#define DYN_ISO_WRAP 0.3f
static inline S32 DynLight_IsoLambert(float dot, float dist) {
    const float c = dist > 0.0f ? dot / dist : 1.0f;
    const float w = (c + DYN_ISO_WRAP) / (1.0f + DYN_ISO_WRAP);
    return w <= 0.0f ? 0 : (w >= 1.0f ? 256 : (S32)(w * 256.0f));
}

/* Engine hooks (DYNLIGHT.CPP). Each returns at once while DynLight is 0, but
   DynLight_CubeLoaded, which keeps the cube's number for when it is turned on.
   The lamps of a cube are found the first time it is drawn with the light on
   and kept until the next island. */
extern void DynLight_IslandLoaded(void);   /* LOADISLE.CPP, end of LoadIsland */
extern void DynLight_CubeLoaded(S32 cube); /* LOADISLE.CPP: LoadCube's cube, -1 for the open sea */
extern void DynLight_FrameBegin(void);     /* EXTFUNC.CPP, AffGrilleExt: a full redraw starts */
extern void DynLight_GatherCube(void);     /* TERRAIN.CPP: the cube just drawn, its camera in place */
extern void DynLight_BakeBackground(void); /* EXTFUNC.CPP: the lamps into the background */
/* A box of the background drawn again between full redraws (the shallows,
   the moving sea): DynLight_Unlight puts the background as it was before the
   lamps back into the frame first, so the redraw draws over unlit pixels;
   DynLight_Refreshed, before the box reaches the background copy, lights the
   lamps' pixels again, whatever the redraw left there. */
extern void DynLight_Unlight(S32 x0, S32 y0, S32 x1, S32 y1);
extern void DynLight_Refreshed(S32 x0, S32 y0, S32 x1, S32 y1);
/* A box a background object is about to be baked into the background copy
   from (OBJECT.CPP, before the copy, where the frame still tells the object
   from the background): the lamps leave the object's pixels as they are from
   then on, as on a full redraw, and indoors they show no brick any more. */
extern void DynLight_Baking(S32 x0, S32 y0, S32 x1, S32 y1);
/* INTEXT.CPP, RefreshGrille: an interior's bricks are about to be drawn, and
   have been: in between each brick drawn leaves its position (GRILLE.H). */
extern void DynLight_IsoBegin(void);
extern void DynLight_IsoEnd(void);
/* OBJECT.CPP, AffScene: the moving lights over the frame's background, after
   the scene and before the incrusts; at level 2 the frame the colour stage
   reads is kept here too. */
extern void DynLight_MovingPass(void);
/* The colour stage, at present time (SetPresentLightCallback): `argb` is the
   frame Phys was just converted to, `pitch` in bytes, `pal` the palette it was
   converted with. Pure: it reads what the last scene kept and writes `argb`. */
extern void DynLight_PresentStage(U32 *argb, U32 pitch, U32 w, U32 h, const U8 *pal);

/* The cfg key, the console cvar and the table, for the surfaces that serve
   every setting (CONFIG_FILE.CPP, CONSOLE_CMD.CPP). */
extern void DynLight_ReadConfig(void);
extern void DynLight_WriteConfig(void);
extern void DynLight_RegisterCvars(void);
extern const struct T_SETTING *DynLight_Settings(S32 *count);

#endif /* DYNLIGHT_H */
