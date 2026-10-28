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
 *      every frame, outdoors and indoors; the actors' bodies take them on
 *      their shading. A palette pixel can only climb its own 16-shade ramp,
 *      so these pools are brightness, not colour.
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

/* A brick that does not fill its cell (a bench, a table, a rail, a floor
   tile), or a cell cut by a slope or a stair (its collision shape, 2..13):
   the surface its pixels show. DynLight_IsoFitBrick fits a box to the
   brick's opaque pixels, by their extremes along the three directions that
   bound an isometric box's outline: u, 2v + u and 2v - u. A box's six
   extremes have five free values (it slides along the view, (5, 4, 5),
   without its outline changing), so the box stands on its cell's floor,
   then slides to the deepest place inside the cell; an outline edge held by
   no more than DYN_ISO_WEAK pixels is a corner, the face beyond it not
   drawn, and is loosened until the six extremes are a box's. A sprite drawn
   inside one face of its cell is that face (a floor tile thinner than
   DYN_ISO_THIN, a wall's side), and a plane within DYN_ISO_SNAP of the
   cell's is the cell's. Only the box's near planes (x1, y1, z1, cell-local)
   meet the eye, so they are what is kept. A whole brick keeps the whole
   cell, (512, 256, 512), and today's DynLight_IsoT3 exactly. */
#define DYN_ISO_S2 3     /* the slope faces of collision shapes 2..5, faces 3..6 */
#define DYN_ISO_EPS 1e-6 /* planes that meet on a pixel's ray: a tie, whatever the rounding */
#define DYN_ISO_WEAK 3
#define DYN_ISO_THIN 96.0
#define DYN_ISO_SNAP 32.0
typedef struct {
    double x1, y1, z1; /* the near planes, cell-local */
    S32 col;           /* the cell's collision shape: 1 a box, 2..13 a slope or a corner */
} T_DYN_ISO_SURF;

/* The six extremes of a brick's opaque pixels (pu[i], pv[i]): u, 2v + u and
   2v - u, least then greatest. */
static inline void DynLight_IsoExtremes(const U8 *pu, const U8 *pv, S32 n, S32 *ex) {
    S32 i;
    ex[0] = ex[2] = ex[4] = 0x7FFFFFFF;
    ex[1] = ex[3] = ex[5] = -0x7FFFFFFF;
    for (i = 0; i < n; i++) {
        const S32 u = pu[i], P = 2 * pv[i] + pu[i], Q = 2 * pv[i] - pu[i];
        if (u < ex[0])
            ex[0] = u;
        if (u > ex[1])
            ex[1] = u;
        if (P < ex[2])
            ex[2] = P;
        if (P > ex[3])
            ex[3] = P;
        if (Q < ex[4])
            ex[4] = Q;
        if (Q > ex[5])
            ex[5] = Q;
    }
}

/* How many pixels hold each extreme, within one level: a real edge has
   many, a corner few. */
static inline void DynLight_IsoSupports(const U8 *pu, const U8 *pv, S32 n, const S32 *ex, S32 *sup) {
    S32 i;
    for (i = 0; i < 6; i++)
        sup[i] = 0;
    for (i = 0; i < n; i++) {
        const S32 u = pu[i], P = 2 * pv[i] + pu[i], Q = 2 * pv[i] - pu[i];
        sup[0] += u <= ex[0] + 1;
        sup[1] += u >= ex[1] - 1;
        sup[2] += P <= ex[2] + 1;
        sup[3] += P >= ex[3] - 1;
        sup[4] += Q <= ex[4] + 1;
        sup[5] += Q >= ex[5] - 1;
    }
}

static inline void DynLight_IsoFitBrick(const U8 *pu, const U8 *pv, S32 n, S32 col, T_DYN_ISO_SURF *s) {
    static const S32 cellExt[6] = {0, 47, 22, 99, -25, 52}; /* the whole cell's extremes */
    static const double cell[3] = {512.0, 256.0, 512.0};
    S32 ex[6], e[6], sup[6], sup0[6], faces = 0, i, k;
    double a1, a2, b1, b2, g1, g2, bx[6], klo, khi, kk;
    s->x1 = 512.0;
    s->y1 = 256.0;
    s->z1 = 512.0;
    s->col = col;
    if (col >= 2 || n <= 0) /* a slope keeps the cell's box, cut by its planes */
        return;
    for (i = 0; i < n; i++) {
        S32 f;
        DynLight_IsoT3(pu[i], pv[i], &f);
        faces |= 1 << f;
    }
    DynLight_IsoExtremes(pu, pv, n, ex);
    DynLight_IsoSupports(pu, pv, n, ex, sup0);

    /* Loosen, never tighten, the least held edge until the six extremes are
       a box's: 2(uL + uH) = PL + PH - QL - QH, give or take a level. */
    for (i = 0; i < 6; i++) {
        e[i] = ex[i];
        sup[i] = sup0[i];
    }
    for (k = 0; k < 6; k++) {
        const S32 r = 2 * (e[0] + e[1]) - (e[2] + e[3] - e[4] - e[5]);
        S32 cand[3][4], nc = 0, best = -1, j;
        if (r >= -2 && r <= 2)
            break;
        if (r > 0) {
            const S32 c0[3][4] = {{sup[0], 0, -1, (r + 1) / 2}, {sup[3], 3, +1, r}, {sup[4], 4, -1, r}};
            for (j = 0; j < 3; j++)
                if (e[c0[j][1]] != cellExt[c0[j][1]]) {
                    cand[nc][0] = c0[j][0], cand[nc][1] = c0[j][1], cand[nc][2] = c0[j][2], cand[nc][3] = c0[j][3];
                    nc++;
                }
        } else {
            const S32 c0[3][4] = {{sup[1], 1, +1, (1 - r) / 2}, {sup[2], 2, -1, -r}, {sup[5], 5, +1, -r}};
            for (j = 0; j < 3; j++)
                if (e[c0[j][1]] != cellExt[c0[j][1]]) {
                    cand[nc][0] = c0[j][0], cand[nc][1] = c0[j][1], cand[nc][2] = c0[j][2], cand[nc][3] = c0[j][3];
                    nc++;
                }
        }
        if (!nc)
            break;
        for (j = 0; j < nc; j++) /* the least held, the first edge on a tie */
            if (best < 0 || cand[j][0] < cand[best][0] ||
                (cand[j][0] == cand[best][0] && cand[j][1] < cand[best][1]))
                best = j;
        {
            const S32 idx = cand[best][1], room = cellExt[idx] > e[idx] ? cellExt[idx] - e[idx] : e[idx] - cellExt[idx];
            e[idx] += cand[best][2] * (cand[best][3] < room ? cand[best][3] : room);
            sup[idx] = 1 << 30;
        }
    }

    /* The edges, half a level outside the extreme pixels; the box standing
       on the floor. */
    a1 = 64.0 * e[0] - 1536.0;  /* 3(x0 - z1) */
    a2 = 64.0 * e[1] - 1472.0;  /* 3(x1 - z0) */
    b1 = 128.0 * e[2] - 6656.0; /* 12x0 - 15y1 */
    b2 = 128.0 * e[3] - 6528.0; /* 12x1 - 15y0 */
    g1 = 128.0 * e[4] - 640.0;  /* 12z0 - 15y1 */
    g2 = 128.0 * e[5] - 512.0;  /* 12z1 - 15y0 */
    bx[1] = 0.0;
    bx[3] = b2 / 12.0;
    bx[5] = g2 / 12.0;
    bx[2] = bx[3] - a2 / 3.0;
    bx[0] = bx[5] + a1 / 3.0;
    bx[4] = ((12.0 * bx[0] - b1) + (12.0 * bx[2] - g1)) / 30.0;

    /* Slide along the view to the deepest place inside the cell; clamp what
       still sticks out. */
    klo = -bx[0] / 5.0;
    if (-bx[1] / 4.0 > klo)
        klo = -bx[1] / 4.0;
    if (-bx[2] / 5.0 > klo)
        klo = -bx[2] / 5.0;
    khi = (512.0 - bx[3]) / 5.0;
    if ((256.0 - bx[4]) / 4.0 < khi)
        khi = (256.0 - bx[4]) / 4.0;
    if ((512.0 - bx[5]) / 5.0 < khi)
        khi = (512.0 - bx[5]) / 5.0;
    kk = klo <= khi ? klo : 0.5 * (klo + khi);
    for (i = 0; i < 6; i++) {
        const double v = bx[i] + kk * (i % 3 == 1 ? 4.0 : 5.0);
        bx[i] = v < 0.0 ? 0.0 : (v > cell[i % 3] ? cell[i % 3] : v);
    }
    for (i = 0; i < 3; i++)
        if (bx[i] > bx[i + 3])
            bx[i] = bx[i + 3] = 0.5 * (bx[i] + bx[i + 3]);
    for (i = 0; i < 3; i++) { /* snap to the cell's planes */
        if (bx[i] < DYN_ISO_SNAP)
            bx[i] = 0.0;
        if (bx[i + 3] > cell[i] - DYN_ISO_SNAP)
            bx[i + 3] = cell[i];
    }

    /* A sprite inside one face of its cell is that face; a box face whose two
       outline edges are corners is not drawn, and the box goes on into the
       next cell. */
    if ((faces == (1 << DYN_ISO_TOP) && bx[4] - bx[1] < DYN_ISO_THIN) ||
        ((faces == (1 << DYN_ISO_PX) || faces == (1 << DYN_ISO_PZ)) &&
         !(sup0[2] > DYN_ISO_WEAK && sup0[4] > DYN_ISO_WEAK)))
        return;
    if (sup0[2] <= DYN_ISO_WEAK && sup0[4] <= DYN_ISO_WEAK)
        bx[4] = 256.0;
    if (sup0[1] <= DYN_ISO_WEAK && sup0[3] <= DYN_ISO_WEAK)
        bx[3] = 512.0;
    if (sup0[0] <= DYN_ISO_WEAK && sup0[5] <= DYN_ISO_WEAK)
        bx[5] = 512.0;
    s->x1 = bx[3];
    s->y1 = bx[4];
    s->z1 = bx[5];
}

/* Where a pixel's ray (d = 3(x - z), b = 6(x + z) - 15y, cell-local) meets a
   slope plane of collision shape 2..5, in T3 (ReajustPos's heights). */
static inline double DynLight_IsoSlopeT3(S32 col, double d, double b) {
    switch (col) {
    case 2:
        return (4.0 * b + 5.0 * d) / 3.0;
    case 3:
        return (4.0 * b - 5.0 * d) / 3.0;
    case 4:
        return (15360.0 + 4.0 * b + 5.0 * d) / 13.0;
    default:
        return (15360.0 + 4.0 * b - 5.0 * d) / 13.0;
    }
}

/* The whole cell cut by the planes of shapes `pa` and `pb` (0 for none): where
   the ray enters, its face, and whether it meets the solid at all. */
static inline double DynLight_IsoWedge(double d, double b, S32 pa, S32 pb, S32 *face, S32 *hit) {
    const double tx = 3072.0 - d, tz = 3072.0 + d, ty = (b + 3840.0) / 2.0;
    const double lo = (d < 0 ? -d : d) > b / 2.0 ? (d < 0 ? -d : d) : b / 2.0;
    double t;
    S32 f, k;
    if (ty <= tx + DYN_ISO_EPS && ty <= tz + DYN_ISO_EPS)
        t = ty, f = DYN_ISO_TOP;
    else if (tx <= tz + DYN_ISO_EPS)
        t = tx, f = DYN_ISO_PX;
    else
        t = tz, f = DYN_ISO_PZ;
    for (k = 0; k < 2; k++) {
        const S32 c = k ? pb : pa;
        double ts;
        if (!c)
            continue;
        ts = DynLight_IsoSlopeT3(c, d, b);
        if (ts < t - DYN_ISO_EPS)
            t = ts, f = DYN_ISO_S2 + c - 2;
    }
    *face = f;
    *hit = lo <= t + DYN_ISO_EPS;
    return t;
}

/* T3 and the face of pixel (u, v) of a brick of surface `s`: the fitted box's
   near planes, or the cell cut by its slope; the corners 6..9 are the union of
   two slopes, 10..13 their intersection (ReajustPos). A ray that misses the
   surface keeps the whole cell's (DynLight_IsoT3). */
static inline double DynLight_IsoSurfT3(S32 u, S32 v, const T_DYN_ISO_SURF *s, S32 *face) {
    static const S8 corner[14][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {1, 2, 3}, {1, 4, 5}, {1, 2, 4}, {1, 3, 5}, /* the larger of two */
                                     {2, 2, 3},
                                     {2, 4, 5},
                                     {2, 2, 4},
                                     {2, 3, 5}}; /* both planes at once */
    const double d = 64.0 * u - 1504.0, b = 256.0 * (v - 14);
    const double lo = (d < 0 ? -d : d) > b / 2.0 ? (d < 0 ? -d : d) : b / 2.0;
    double t;
    S32 f, hit;
    if (s->col >= 2 && s->col <= 5)
        t = DynLight_IsoWedge(d, b, s->col, 0, &f, &hit);
    else if (s->col >= 6 && s->col <= 13 && corner[s->col][0] == 2)
        t = DynLight_IsoWedge(d, b, corner[s->col][1], corner[s->col][2], &f, &hit);
    else if (s->col >= 6 && s->col <= 13) {
        S32 fa, fb, ha, hb;
        const double ta = DynLight_IsoWedge(d, b, corner[s->col][1], 0, &fa, &ha);
        const double tb = DynLight_IsoWedge(d, b, corner[s->col][2], 0, &fb, &hb);
        /* the hits, the larger; with none, the larger and no hit */
        const S32 aFirst = ta > tb + DYN_ISO_EPS || (ta >= tb - DYN_ISO_EPS && fa >= fb);
        if (ha && (!hb || aFirst))
            t = ta, f = fa, hit = 1;
        else if (hb)
            t = tb, f = fb, hit = 1;
        else if (aFirst)
            t = ta, f = fa, hit = 0;
        else
            t = tb, f = fb, hit = 0;
    } else {
        const double tx = 6.0 * s->x1 - d, tz = 6.0 * s->z1 + d, ty = (b + 15.0 * s->y1) / 2.0;
        if (ty <= tx + DYN_ISO_EPS && ty <= tz + DYN_ISO_EPS)
            t = ty, f = DYN_ISO_TOP;
        else if (tx <= tz + DYN_ISO_EPS)
            t = tx, f = DYN_ISO_PX;
        else
            t = tz, f = DYN_ISO_PZ;
        hit = t >= lo - DYN_ISO_EPS;
    }
    if (!hit)
        t = (double)DynLight_IsoT3(u, v, &f);
    *face = f;
    return t;
}

/* The normal of a face, dotted with a vector (dx, dy, dz): the top, +x, +z,
   and the slopes of shapes 2..5, (-1, 2, 0), (0, 2, -1), (0, 2, 1) and
   (1, 2, 0) over sqrt(5). */
static inline float DynLight_IsoDot(S32 face, float dx, float dy, float dz) {
    switch (face) {
    case DYN_ISO_TOP:
        return dy;
    case DYN_ISO_PX:
        return dx;
    case DYN_ISO_PZ:
        return dz;
    case DYN_ISO_S2:
        return (2.0f * dy - dx) * 0.4472136f;
    case DYN_ISO_S2 + 1:
        return (2.0f * dy - dz) * 0.4472136f;
    case DYN_ISO_S2 + 2:
        return (2.0f * dy + dz) * 0.4472136f;
    default:
        return (2.0f * dy + dx) * 0.4472136f;
    }
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

/* A body's shade at one point (ListLights: 8.8, the row of the 16-shade
   ramp and its fraction), raised by the point lights near it that its
   normal faces. `v` is the point and `n` its normal (the body's raw S16
   scale, 16384 for a unit normal), `rel` the lights about the body: x, y, z
   and the reach squared, with their levels in sixteenths of a row. A face
   toward a light at full level gains half the level in rows
   (DYN_BODY_GAIN); a shade is never lowered, and never raised past the top
   the scene's light gives that normal (|n| * 15360 / 65536, at most 15
   rows), so the fillers stay in their ramp and fog band. */
#define DYN_BODY_GAIN 2048.0f /* the shade gained is level * cos * |n| / (16 * this), 256ths of a row */
static inline U16 DynLight_BodyShade(U16 orig, const float *v, float nx, float ny, float nz, const float (*rel)[4],
                                     const S32 *level, S32 n) {
    const float nl2 = nx * nx + ny * ny + nz * nz;
    float add = 0.0f;
    S32 k, cap, val;
    if (nl2 <= 0.0f)
        return orig;
    for (k = 0; k < n; k++) {
        const float dx = rel[k][0] - v[0], dy = rel[k][1] - v[1], dz = rel[k][2] - v[2];
        const float d2 = dx * dx + dy * dy + dz * dz, dot = nx * dx + ny * dy + nz * dz;
        if (d2 >= rel[k][3] || dot <= 0.0f)
            continue;
        add += (float)DynLight_FalloffF(d2, 1.0f / rel[k][3], level[k]) * dot / (sqrtf(d2) * DYN_BODY_GAIN);
    }
    if (add < 1.0f)
        return orig;
    cap = (S32)(sqrtf(nl2) * (15360.0f / 65536.0f));
    if (cap > 3840)
        cap = 3840;
    val = (S32)orig + (S32)add;
    if (val > cap)
        val = cap;
    return (U16)(val > (S32)orig ? val : (S32)orig);
}

/* Keeping the largest of a stream, `max` at most: `n` holds the `*nb` kept,
   largest first, ties in the order they came. Returns the place `count`
   takes, the kept from there moved one down (the last dropped when full), or
   -1 when the list is full and it is no larger than its last. A caller that
   keeps something beside each count moves it the same way, its [j, *nb - 1)
   to [j + 1, *nb). */
static inline S32 DynLight_KeepLargest(S32 *n, S32 *nb, S32 max, S32 count) {
    S32 j;
    if (*nb >= max && count <= n[max - 1])
        return -1;
    j = *nb < max ? (*nb)++ : max - 1;
    for (; j > 0 && n[j - 1] < count; j--)
        n[j] = n[j - 1];
    n[j] = count;
    return j;
}

/* The static lights indoors. A room's bricks are drawn in advance, with the
   light the artists painted on them: the lava's glow, and the lamps, white
   spots ringed with their glass's colour. */

/* A brick's sprite (AffGraph's layout: dx, dy, a signed corner, then per row
   a count of runs: 00nnnnnn skips n + 1, 01nnnnnn copies n + 1 bytes,
   1xnnnnnn repeats the next byte n + 1 times) laid into its 48 x 38 box:
   `idx` its colours, `opq` 1 where it draws. Returns how many pixels it
   draws. */
static inline S32 DynLight_SpriteCanvas(const U8 *spr, U8 *idx, U8 *opq) {
    const S32 dy = spr[1], hx = (S8)spr[2], hy = (S8)spr[3];
    const U8 *p = spr + 4;
    S32 r, n = 0;
    for (r = 0; r < DYN_ISO_W * DYN_ISO_H; r++)
        idx[r] = opq[r] = 0;
    for (r = 0; r < dy; r++) {
        const S32 v = hy + r;
        S32 runs = *p++, u = hx;
        while (runs-- > 0) {
            const S32 c = *p++, op = c >> 6, cnt = (c & 63) + 1;
            S32 k;
            if (op == 0) {
                u += cnt;
                continue;
            }
            for (k = 0; k < cnt; k++, u++)
                if (u >= 0 && u < DYN_ISO_W && v >= 0 && v < DYN_ISO_H) {
                    const S32 i = v * DYN_ISO_W + u;
                    idx[i] = op == 1 ? p[k] : p[0];
                    n += !opq[i];
                    opq[i] = 1;
                }
            p += op == 1 ? cnt : 1;
        }
    }
    return n;
}

/* A box's colours, hashed (FNV-1a): a brick the lamp test takes wrongly is
   named by it, whatever its number in the room's bank. */
static inline U32 DynLight_CanvasHash(const U8 *idx, const U8 *opq) {
    U32 h = 2166136261u;
    S32 i;
    for (i = 0; i < DYN_ISO_W * DYN_ISO_H; i++) {
        h ^= opq[i] ? idx[i] : 0u;
        h *= 16777619u;
    }
    return h;
}

/* A lamp painted into a brick: a spot of bright pixels (luma at least
   DYN_LAMP_BRIGHT, 8-connected) that is small, round, solid, inside the
   sprite rather than on its outline, brighter at its core than at its rim
   (the shading that tells a lamp from a white wall's sheen), and white hot
   at its peak. Its colour is its glass's, the ring of pixels within three of
   it. Returns 0 when the sprite shows none, else its largest. */
#define DYN_LAMP_BRIGHT 220
#define DYN_LAMP_AREA_MIN 12
#define DYN_LAMP_AREA_MAX 420
#define DYN_LAMP_SHARE 0.45f   /* of the sprite's pixels, at most */
#define DYN_LAMP_ASPECT 1.8f   /* its ellipse's axes, at most */
#define DYN_LAMP_SOLID 0.65f   /* its area over its ellipse's, at least */
#define DYN_LAMP_OUTLINE 0.35f /* of its rim on the sprite's outline, at most */
#define DYN_LAMP_SHADE 12      /* its core's mean luma over its rim's, at least */
#define DYN_LAMP_PEAK 245
#define DYN_LAMP_RING 100 /* a ring pixel's luma, at least */
typedef struct {
    float cu, cv; /* its middle in the 48 x 38 box */
    S32 area;
    U8 colour; /* a palette index */
} T_DYN_LAMP_BLOB;

/* The palette entry nearest a colour, colour 0 (the clear colour) aside. */
static inline S32 DynLight_Nearest(const U8 *pal, float r, float g, float b) {
    S32 c, best = 1;
    float bestD = 1e30f;
    for (c = 1; c < 256; c++) {
        const float dr = pal[3 * c] - r, dg = pal[3 * c + 1] - g, db = pal[3 * c + 2] - b;
        const float d = dr * dr + dg * dg + db * db;
        if (d < bestD) {
            bestD = d;
            best = c;
        }
    }
    return best;
}

static inline S32 DynLight_LampBlob(const U8 *idx, const U8 *opq, S32 nOpaque, const U8 *pal,
                                    T_DYN_LAMP_BLOB *lamp) {
    enum { N = DYN_ISO_W * DYN_ISO_H };
    U8 luma[N], bright[N];
    S16 lab[N], stack[N], members[N];
    S32 i, nb = 0, found = 0, bestLab = -1;
    if (nOpaque <= 0)
        return 0;
    for (i = 0; i < N; i++) {
        luma[i] = opq[i] ? (U8)DynLight_Luma(pal + 3 * idx[i]) : 0;
        bright[i] = opq[i] && luma[i] >= DYN_LAMP_BRIGHT;
        lab[i] = -1;
    }
    for (i = 0; i < N; i++) {
        S32 sp = 0, a = 0, k, inner = 0, rim = 0, out = 0, sumIn = 0, sumRim = 0, peak = 0;
        double su = 0, sv = 0, suu, svv, suv, tr, det, disc, l1, l2, cu, cv;
        if (!bright[i] || lab[i] >= 0)
            continue;
        lab[i] = (S16)nb;
        stack[sp++] = (S16)i;
        while (sp) { /* the spot */
            const S32 p = stack[--sp], pu = p % DYN_ISO_W, pv = p / DYN_ISO_W;
            S32 du, dv;
            members[a++] = (S16)p;
            for (dv = -1; dv <= 1; dv++)
                for (du = -1; du <= 1; du++) {
                    const S32 qu = pu + du, qv = pv + dv, q = qv * DYN_ISO_W + qu;
                    if (qu < 0 || qu >= DYN_ISO_W || qv < 0 || qv >= DYN_ISO_H || !bright[q] || lab[q] >= 0)
                        continue;
                    lab[q] = (S16)nb;
                    stack[sp++] = (S16)q;
                }
        }
        nb++;
        if (a < DYN_LAMP_AREA_MIN || a > DYN_LAMP_AREA_MAX || (float)a > DYN_LAMP_SHARE * (float)nOpaque)
            continue;
        for (k = 0; k < a; k++) {
            su += members[k] % DYN_ISO_W;
            sv += members[k] / DYN_ISO_W;
        }
        cu = su / a;
        cv = sv / a;
        suu = svv = suv = 0;
        for (k = 0; k < a; k++) {
            const double du = members[k] % DYN_ISO_W - cu, dv = members[k] / DYN_ISO_W - cv;
            suu += du * du;
            svv += dv * dv;
            suv += du * dv;
        }
        suu = suu / a + 1.0 / 12.0; /* a pixel's own spread */
        svv = svv / a + 1.0 / 12.0;
        suv /= a;
        tr = suu + svv;
        det = suu * svv - suv * suv;
        disc = sqrt(tr * tr / 4.0 - det > 0.0 ? tr * tr / 4.0 - det : 0.0);
        l1 = tr / 2.0 + disc;
        l2 = tr / 2.0 - disc > 1e-6 ? tr / 2.0 - disc : 1e-6;
        if (sqrt(l1 / l2) > DYN_LAMP_ASPECT ||
            (double)a / (4.0 * 3.14159265358979 * sqrt(det > 1e-6 ? det : 1e-6)) < DYN_LAMP_SOLID)
            continue;
        for (k = 0; k < a; k++) {
            const S32 p = members[k], pu = p % DYN_ISO_W, pv = p / DYN_ISO_W;
            S32 du, dv, edge = 0, outside = 0;
            for (dv = -1; dv <= 1; dv++)
                for (du = -1; du <= 1; du++) {
                    const S32 qu = pu + du, qv = pv + dv;
                    const S32 in = qu >= 0 && qu < DYN_ISO_W && qv >= 0 && qv < DYN_ISO_H;
                    if (!du && !dv)
                        continue;
                    if (!in || lab[qv * DYN_ISO_W + qu] != lab[p])
                        edge = 1;
                    if (!in || !opq[qv * DYN_ISO_W + qu])
                        outside = 1;
                }
            if (edge) {
                rim++;
                sumRim += luma[p];
                out += outside;
            } else {
                inner++;
                sumIn += luma[p];
            }
            if (luma[p] > peak)
                peak = luma[p];
        }
        if ((float)out > DYN_LAMP_OUTLINE * (float)(rim > 1 ? rim : 1) || peak < DYN_LAMP_PEAK || !inner || !rim ||
            (double)sumIn / inner - (double)sumRim / rim < DYN_LAMP_SHADE)
            continue;
        if (!found || a > lamp->area) {
            found = 1;
            bestLab = lab[i];
            lamp->cu = (float)cu;
            lamp->cv = (float)cv;
            lamp->area = a;
        }
    }
    if (!found)
        return 0;
    {
        /* The ring: three steps of 8-neighbour growth about the spot. */
        U8 grow[N], next[N];
        S32 step, sr = 0, sg = 0, sbl = 0, cnt = 0;
        for (i = 0; i < N; i++)
            grow[i] = lab[i] == bestLab;
        for (step = 0; step < 3; step++) {
            for (i = 0; i < N; i++) {
                const S32 pu = i % DYN_ISO_W, pv = i / DYN_ISO_W;
                S32 du, dv, g = grow[i];
                for (dv = -1; dv <= 1 && !g; dv++)
                    for (du = -1; du <= 1 && !g; du++) {
                        const S32 qu = pu + du, qv = pv + dv;
                        g = qu >= 0 && qu < DYN_ISO_W && qv >= 0 && qv < DYN_ISO_H && grow[qv * DYN_ISO_W + qu];
                    }
                next[i] = (U8)g;
            }
            for (i = 0; i < N; i++)
                grow[i] = next[i];
        }
        for (i = 0; i < N; i++)
            if (grow[i] && lab[i] != bestLab && opq[i] && luma[i] >= DYN_LAMP_RING) {
                sr += pal[3 * idx[i]];
                sg += pal[3 * idx[i] + 1];
                sbl += pal[3 * idx[i] + 2];
                cnt++;
            }
        if (!cnt) /* no glass about it: the spot's own colour */
            for (i = 0; i < N; i++)
                if (lab[i] == bestLab) {
                    sr += pal[3 * idx[i]];
                    sg += pal[3 * idx[i] + 1];
                    sbl += pal[3 * idx[i] + 2];
                    cnt++;
                }
        lamp->colour = (U8)DynLight_Nearest(pal, (float)sr / cnt, (float)sg / cnt, (float)sbl / cnt);
    }
    return 1;
}

/* The lava's light. Every lava cell open to the air above is a light,
   DYN_LAVA_LIFT above the middle of its top (two cells' height, so the glow
   climbs the walls about it), and a room's lava is a field of them kept at the
   corners of its cells (every 512, 256 and 512 units), one value per face a
   pixel may show (the top, +x, +z): how much of the lava's light that face
   takes there, by the falloff and how squarely it faces each cell
   (DynLight_IsoLambert). DYN_LAVA_ONE is what the top of a cell in the middle
   of a lake takes, and lights DYN_LAVA_LEVEL; a pixel reads the corners about
   it. */
#define DYN_LAVA_GX 65
#define DYN_LAVA_GY 26
#define DYN_LAVA_GZ 65
#define DYN_LAVA_CORNERS (DYN_LAVA_GX * DYN_LAVA_GY * DYN_LAVA_GZ)
#define DYN_LAVA_REACH 2048.0f
#define DYN_LAVA_LIFT 512    /* above the lava's top */
#define DYN_LAVA_LAKE 8.855f /* the sum at a lake's middle, for this lift and reach */
#define DYN_LAVA_ONE 4096
#define DYN_LAVA_LEVEL 176

static inline float DynLight_LambertF(float dot, float dist) {
    const float c = dist > 0.0f ? dot / dist : 1.0f;
    const float w = (c + DYN_ISO_WRAP) / (1.0f + DYN_ISO_WRAP);
    return w <= 0.0f ? 0.0f : (w >= 1.0f ? 1.0f : w);
}

/* One lava light at (lx, ly, lz), into `acc` (3 floats per corner, the
   corners z, then y, then x major). */
static inline void DynLight_LavaAdd(float *acc, float lx, float ly, float lz) {
    const float r2 = DYN_LAVA_REACH * DYN_LAVA_REACH, inv2 = 1.0f / r2;
    S32 gx0 = (S32)ceilf((lx - DYN_LAVA_REACH) / 512.0f), gx1 = (S32)floorf((lx + DYN_LAVA_REACH) / 512.0f);
    S32 gy0 = (S32)ceilf((ly - DYN_LAVA_REACH) / 256.0f), gy1 = (S32)floorf((ly + DYN_LAVA_REACH) / 256.0f);
    S32 gz0 = (S32)ceilf((lz - DYN_LAVA_REACH) / 512.0f), gz1 = (S32)floorf((lz + DYN_LAVA_REACH) / 512.0f);
    S32 gx, gy, gz;
    gx0 = gx0 < 0 ? 0 : gx0;
    gy0 = gy0 < 0 ? 0 : gy0;
    gz0 = gz0 < 0 ? 0 : gz0;
    gx1 = gx1 > DYN_LAVA_GX - 1 ? DYN_LAVA_GX - 1 : gx1;
    gy1 = gy1 > DYN_LAVA_GY - 1 ? DYN_LAVA_GY - 1 : gy1;
    gz1 = gz1 > DYN_LAVA_GZ - 1 ? DYN_LAVA_GZ - 1 : gz1;
    for (gz = gz0; gz <= gz1; gz++)
        for (gy = gy0; gy <= gy1; gy++) {
            float *row = acc + ((size_t)(gz * DYN_LAVA_GY + gy) * DYN_LAVA_GX) * 3;
            const float dy = ly - 256.0f * (float)gy, dz = lz - 512.0f * (float)gz;
            for (gx = gx0; gx <= gx1; gx++) {
                const float dx = lx - 512.0f * (float)gx, d2 = dx * dx + dy * dy + dz * dz;
                float a, f, r;
                if (d2 >= r2)
                    continue;
                a = 1.0f - d2 * inv2;
                f = a * a;
                r = sqrtf(d2);
                row[gx * 3 + 0] += f * DynLight_LambertF(dy, r);
                row[gx * 3 + 1] += f * DynLight_LambertF(dx, r);
                row[gx * 3 + 2] += f * DynLight_LambertF(dz, r);
            }
        }
}

/* What a face (`ch`: 0 the top, 1 +x, 2 +z) at (X, Y, Z) takes of the lava,
   DYN_LAVA_ONE at a lake's middle: the corners about the point, weighed by
   how near it is to each. A point past the room's last corner reads it. */
static inline S32 DynLight_LavaAt(const U16 *field, float X, float Y, float Z, S32 ch) {
    float fx = X * (1.0f / 512.0f), fy = Y * (1.0f / 256.0f), fz = Z * (1.0f / 512.0f), tx, ty, tz, v;
    S32 ix, iy, iz;
    const U16 *c;
    const size_t sy = (size_t)DYN_LAVA_GX * 3, sz = (size_t)DYN_LAVA_GY * DYN_LAVA_GX * 3;
    fx = fx < 0.0f ? 0.0f : (fx > DYN_LAVA_GX - 1 ? (float)(DYN_LAVA_GX - 1) : fx);
    fy = fy < 0.0f ? 0.0f : (fy > DYN_LAVA_GY - 1 ? (float)(DYN_LAVA_GY - 1) : fy);
    fz = fz < 0.0f ? 0.0f : (fz > DYN_LAVA_GZ - 1 ? (float)(DYN_LAVA_GZ - 1) : fz);
    ix = (S32)fx > DYN_LAVA_GX - 2 ? DYN_LAVA_GX - 2 : (S32)fx;
    iy = (S32)fy > DYN_LAVA_GY - 2 ? DYN_LAVA_GY - 2 : (S32)fy;
    iz = (S32)fz > DYN_LAVA_GZ - 2 ? DYN_LAVA_GZ - 2 : (S32)fz;
    tx = fx - (float)ix;
    ty = fy - (float)iy;
    tz = fz - (float)iz;
    c = field + ((size_t)(iz * DYN_LAVA_GY + iy) * DYN_LAVA_GX + ix) * 3 + ch;
    v = (1.0f - tz) * ((1.0f - ty) * ((1.0f - tx) * c[0] + tx * c[3]) + ty * ((1.0f - tx) * c[sy] + tx * c[sy + 3])) +
        tz * ((1.0f - ty) * ((1.0f - tx) * c[sz] + tx * c[sz + 3]) +
              ty * ((1.0f - tx) * c[sz + sy] + tx * c[sz + sy + 3]));
    return (S32)(v + 0.5f);
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
/* OBJECT.CPP, AffScene: the bodies BaseSort draws take the point lights
   on their shades (ObjLightHook, AFF_OBJ.H) between these two; a body baked
   into the background (DynLight_BodyBackground set before it) takes only the
   static ones, which hold until the next full redraw as it does. */
extern void DynLight_BeginBodies(void);
extern void DynLight_EndBodies(void);
extern void DynLight_BodyBackground(S32 background);
/* OBJECT.CPP, a sprite actor between those two: before PtrAffGraph draws
   sprite `num` at (x, y), and after, with its middle in the world and
   whether it is being baked into the background (it then takes the static
   lights only). The pixels the draw changed take the light there. */
extern void DynLight_SpriteBegin(S32 num, S32 x, S32 y);
extern void DynLight_SpriteEnd(S32 wx, S32 wy, S32 wz, S32 background);
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
