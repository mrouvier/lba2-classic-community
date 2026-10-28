/* Host test for dynamic lighting's arithmetic (SOURCES/DYNLIGHT.H).
 *
 * The lamps are found in the light the 1997 artists baked into each cube: a
 * bright round patch under each street lamp. The finder has to see such a
 * patch and nothing else (a lit slope, a bright edge), put its centre where
 * the patch is and measure how far it reaches. The surface table has to keep
 * every colour in its own 16-shade ramp, leave it as it is at level 0, and
 * brighten it more at each level up to the top of the ramp. The falloff has to
 * be full at the light and nothing at its reach.
 *
 * A street lamp is a pool that is filled, ringed with dark ground and centred
 * on the decor; its colour is its body's highest polygon's. The passes find a
 * light's pixels through the screen box and the row spans of its sphere, which
 * must hold every pixel whose ray meets it; the fog, the dither and the colour
 * stage's tint and glow have their own small arithmetic. */
#include "DYNLIGHT.H"

#include "test_harness.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static U8 g_map[DYN_LIGHT_GRID * DYN_LIGHT_GRID];

static void flat(U8 v) {
    memset(g_map, v, sizeof(g_map));
}

/* A painted pool: `peak` at the centre, fading to the ground over `r` cells. */
static void paint_pool(S32 cx, S32 cz, S32 r, S32 peak, S32 ground) {
    for (S32 z = 0; z < DYN_LIGHT_GRID; z++)
        for (S32 x = 0; x < DYN_LIGHT_GRID; x++) {
            const S32 d2 = (x - cx) * (x - cx) + (z - cz) * (z - cz);
            if (d2 <= r * r) {
                const S32 v = peak - (peak - ground) * d2 / (r * r + 1);
                g_map[z * DYN_LIGHT_GRID + x] = (U8)((g_map[z * DYN_LIGHT_GRID + x] & 0xF0) | v);
            }
        }
}

static void test_a_pool_is_found_at_its_centre(void) {
    T_DYN_POOL pool;
    flat(0x37); /* water depth 3 in the high nibble, light 7 */
    paint_pool(20, 30, 3, 14, 7);
    ASSERT_TRUE(DynLight_PoolAt(g_map, 20, 30, &pool));
    ASSERT_TRUE(pool.x16 >= 20 * 16 - 4 && pool.x16 <= 20 * 16 + 4);
    ASSERT_TRUE(pool.z16 >= 30 * 16 - 4 && pool.z16 <= 30 * 16 + 4);
    ASSERT_EQ_INT(14, pool.peak);
    ASSERT_EQ_INT(7, pool.ground);
    ASSERT_TRUE(pool.radius16 >= 16 && pool.radius16 <= 4 * 16);
    /* A decor a cell off the centre still finds it. */
    ASSERT_TRUE(DynLight_PoolAt(g_map, 21, 29, &pool));
}

static void test_no_pool_on_plain_or_dim_ground(void) {
    T_DYN_POOL pool;
    flat(7);
    ASSERT_EQ_INT(0, DynLight_PoolAt(g_map, 20, 20, &pool));
    /* A lit patch that does not reach the peak. */
    paint_pool(20, 20, 3, 11, 7);
    ASSERT_EQ_INT(0, DynLight_PoolAt(g_map, 20, 20, &pool));
    /* A bright slope: the ground around is as bright, so it does not stand out. */
    flat(13);
    paint_pool(20, 20, 3, 15, 13);
    ASSERT_EQ_INT(0, DynLight_PoolAt(g_map, 20, 20, &pool));
}

static void test_the_map_edge_is_read_safely(void) {
    T_DYN_POOL pool;
    flat(7);
    paint_pool(0, 64, 3, 14, 7);
    ASSERT_TRUE(DynLight_PoolAt(g_map, 0, 64, &pool));
    ASSERT_EQ_INT(0, DynLight_PoolAt(g_map, 64, 0, &pool));
}

/* A palette of 16 ramps of 16 steps, each brighter than the one before. */
static void ramp_palette(U8 *pal) {
    for (S32 c = 0; c < 256; c++) {
        const S32 step = c & 15, ramp = c >> 4;
        pal[c * 3 + 0] = (U8)(step * 16);
        pal[c * 3 + 1] = (U8)(step * 12 + ramp);
        pal[c * 3 + 2] = (U8)(step * 8 + ramp * 4);
    }
}

static void test_the_surface_table_keeps_the_ramp(void) {
    static U8 pal[768], lut[16 * 256];
    ramp_palette(pal);
    DynLight_BuildSurf(pal, lut);
    for (S32 c = 0; c < 256; c++) {
        ASSERT_EQ_INT(c, lut[c]); /* level 0 is the identity */
        for (S32 k = 1; k < 16; k++) {
            const S32 e = lut[k * 256 + c];
            ASSERT_EQ_INT(c & 0xF0, e & 0xF0);                      /* in its ramp */
            ASSERT_TRUE((e & 15) >= (c & 15));                      /* never darker */
            ASSERT_TRUE((e & 15) >= (lut[(k - 1) * 256 + c] & 15)); /* brighter with the level */
        }
    }
    /* A mid colour climbs its ramp at the top level. */
    ASSERT_TRUE((lut[15 * 256 + 0x45] & 15) > 5);
    /* The clear colour, an interior's void, stays black. */
    for (S32 k = 0; k < 16; k++)
        ASSERT_EQ_INT(0, lut[k * 256]);
}

/* The palette's odd first ramp: greys, a dark red, a brighter red, a violet
   and a yellow side by side, as the interiors' palette has them. */
static void test_a_lit_colour_keeps_its_hue(void) {
    static U8 pal[768], lut[16 * 256];
    ramp_palette(pal);
    pal[4 * 3 + 0] = 115, pal[4 * 3 + 1] = 0, pal[4 * 3 + 2] = 0;        /* dark red */
    pal[5 * 3 + 0] = 150, pal[5 * 3 + 1] = 10, pal[5 * 3 + 2] = 10;      /* red */
    pal[6 * 3 + 0] = 190, pal[6 * 3 + 1] = 30, pal[6 * 3 + 2] = 20;      /* bright red */
    pal[7 * 3 + 0] = 230, pal[7 * 3 + 1] = 220, pal[7 * 3 + 2] = 0;      /* yellow */
    pal[8 * 3 + 0] = 124, pal[8 * 3 + 1] = 0, pal[8 * 3 + 2] = 220;      /* violet */
    pal[15 * 3 + 0] = 252, pal[15 * 3 + 1] = 252, pal[15 * 3 + 2] = 252; /* white */
    DynLight_BuildSurf(pal, lut);
    for (S32 k = 1; k < 16; k++) {
        const S32 e = lut[k * 256 + 4];
        ASSERT_TRUE(e != 7 && e != 8); /* never the yellow nor the violet beside it */
    }
    ASSERT_TRUE(lut[8 * 256 + 4] != 4); /* and it does climb */
    /* A plain ramp climbs as before. */
    ASSERT_TRUE((lut[15 * 256 + 0x45] & 15) > 5);
}

static void test_the_falloff(void) {
    ASSERT_EQ_INT(240, DynLight_Falloff(0, 1000000, 240));
    ASSERT_EQ_INT(0, DynLight_Falloff(1000000, 1000000, 240));
    ASSERT_EQ_INT(0, DynLight_Falloff(2000000, 1000000, 240));
    /* Half the reach squared: a quarter of the light. */
    ASSERT_EQ_INT(60, DynLight_Falloff(500000, 1000000, 240));
    ASSERT_TRUE(DynLight_Falloff(100000, 1000000, 240) > DynLight_Falloff(400000, 1000000, 240));
}

/* A Citadel lamp: a disc of 14 over ground 7, 2.5 cells across, the decor at
   its centre. */
static void lamp_ground(S32 cx, S32 cz) {
    flat(7);
    for (S32 z = 0; z < DYN_LIGHT_GRID; z++)
        for (S32 x = 0; x < DYN_LIGHT_GRID; x++) {
            const S32 d2 = (x - cx) * (x - cx) + (z - cz) * (z - cz);
            if (d2 <= 6) /* 2.5 cells */
                g_map[z * DYN_LIGHT_GRID + x] = 14;
            else if (d2 <= 9)
                g_map[z * DYN_LIGHT_GRID + x] = 10;
        }
}

static void test_a_lamp_is_a_filled_ringed_pool_under_the_decor(void) {
    T_DYN_POOL pool;
    lamp_ground(30, 30);
    ASSERT_TRUE(DynLight_LampAt(g_map, 30 * DYN_LIGHT_CELL, 30 * DYN_LIGHT_CELL, &pool));
    ASSERT_EQ_INT(14, pool.peak);
    ASSERT_EQ_INT(7, pool.ground);
    /* A decor beside the pool (a bench a cell and a half off) is not the lamp. */
    ASSERT_EQ_INT(0, DynLight_LampAt(g_map, 30 * DYN_LIGHT_CELL + 768, 30 * DYN_LIGHT_CELL + 768, &pool));
    /* A bright plateau: the ring is not dark. */
    flat(7);
    paint_pool(30, 30, 9, 14, 13);
    ASSERT_EQ_INT(0, DynLight_LampAt(g_map, 30 * DYN_LIGHT_CELL, 30 * DYN_LIGHT_CELL, &pool));
    /* A ring of light around a dark middle: the pool is not filled. */
    lamp_ground(30, 30);
    for (S32 z = 29; z <= 31; z++)
        for (S32 x = 29; x <= 31; x++)
            if (x != 30 || z != 30)
                g_map[z * DYN_LIGHT_GRID + x] = 7;
    ASSERT_EQ_INT(0, DynLight_LampAt(g_map, 30 * DYN_LIGHT_CELL, 30 * DYN_LIGHT_CELL, &pool));
}

/* A body of `np` points and the polygon blocks written after it. */
static S32 build_body(U8 *buf, const T_OBJ_POINT *pts, S32 np, const U8 *polys, S32 polysSize) {
    T_BODY_HEADER *b = (T_BODY_HEADER *)buf;
    memset(buf, 0, sizeof(T_BODY_HEADER));
    b->NbPoints = np;
    b->OffPoints = (S32)sizeof(T_BODY_HEADER);
    memcpy(buf + b->OffPoints, pts, sizeof(T_OBJ_POINT) * np);
    b->OffPolys = b->OffPoints + (S32)sizeof(T_OBJ_POINT) * np;
    memcpy(buf + b->OffPolys, polys, (size_t)polysSize);
    b->OffLines = b->OffPolys + polysSize;
    return b->OffLines;
}

static void put16(U8 *p, U16 v) {
    memcpy(p, &v, 2); /* the CPU's order, as the fixup leaves a body */
}

static void test_the_globe_is_the_highest_polygon(void) {
    static U8 buf[1024], polys[256];
    const T_OBJ_POINT pts[6] = {{0, 0, 0, 0}, {10, 0, 0, 0}, {0, 0, 10, 0}, {0, 2500, 0, 0}, {10, 2788, 0, 0}, {0, 2500, 10, 0}};
    S32 n = 0, size;
    /* A block of two plain triangles (type 1: 12 bytes each): the post's
       foot, colour 224, and the globe, colour 108 (Couleur 620). */
    put16(polys + n, 1);
    put16(polys + n + 2, 2);
    n += 8;
    put16(polys + n, 0), put16(polys + n + 2, 1), put16(polys + n + 4, 2), put16(polys + n + 8, 4320);
    n += 12;
    put16(polys + n, 3), put16(polys + n + 2, 4), put16(polys + n + 4, 5), put16(polys + n + 8, 620);
    n += 12;
    /* A block of one textured quad (32 bytes), lower than the globe. */
    put16(polys + n, (U16)((1 << 15) | 10));
    put16(polys + n + 2, 1);
    n += 8;
    put16(polys + n, 0), put16(polys + n + 2, 1), put16(polys + n + 4, 2), put16(polys + n + 6, 0);
    put16(polys + n + 8, 2529);
    n += 32;
    size = build_body(buf, pts, 6, polys, n);
    ASSERT_TRUE(size > 0);
    ASSERT_EQ_INT(108, DynLight_TopColour(buf));
    /* A header out of order reads nothing. */
    ((T_BODY_HEADER *)buf)->OffLines = ((T_BODY_HEADER *)buf)->OffPolys - 4;
    ASSERT_EQ_INT(-1, DynLight_TopColour(buf));
    /* No polygon: no colour. */
    ((T_BODY_HEADER *)buf)->OffLines = ((T_BODY_HEADER *)buf)->OffPolys;
    ASSERT_EQ_INT(-1, DynLight_TopColour(buf));
}

/* Whether the ray (kx, ky, 1) meets the sphere, by its nearest point. */
static S32 ray_meets(float kx, float ky, float cx, float cy, float cz, float r) {
    const double vv = (double)kx * kx + (double)ky * ky + 1.0;
    const double t = ((double)kx * cx + (double)ky * cy + cz) / vv;
    const double dx = t * kx - cx, dy = t * ky - cy, dz = t - cz;
    return t > 0.0 && dx * dx + dy * dy + dz * dz < (double)r * r;
}

static void test_a_sphere_on_screen(void) {
    const float fx = 1600.0f, fy = 1600.0f;
    T_DYN_BOX box;
    S32 full;
    /* In front: the box holds its centre and its four extremes. */
    ASSERT_TRUE(DynLight_SphereBox(2000.0f, -1000.0f, 9000.0f, 1500.0f, fx, fy, 960, 540, 3000.0f, &box, &full));
    ASSERT_EQ_INT(0, full);
    {
        const S32 sx = 960 + (S32)(2000.0f * fx / 9000.0f), sy = 540 - (S32)(-1000.0f * fy / 9000.0f);
        ASSERT_TRUE(box.x0 <= sx && sx <= box.x1 && box.y0 <= sy && sy <= box.y1);
        ASSERT_TRUE(box.x0 <= 960 + (S32)(500.0f * fx / 9000.0f));
        ASSERT_TRUE(box.x1 >= 960 + (S32)(3500.0f * fx / 9000.0f));
        ASSERT_TRUE(box.y1 >= 540 + (S32)(2500.0f * fy / 9000.0f));
    }
    /* Wholly nearer than the near plane: nothing. Across it: the screen. */
    ASSERT_EQ_INT(0, DynLight_SphereBox(0.0f, 0.0f, 1000.0f, 1500.0f, fx, fy, 960, 540, 3000.0f, &box, &full));
    ASSERT_TRUE(DynLight_SphereBox(0.0f, 0.0f, 3500.0f, 1500.0f, fx, fy, 960, 540, 3000.0f, &box, &full));
    ASSERT_EQ_INT(1, full);
}

static void test_the_rows_a_sphere_covers(void) {
    const float cx = 1500.0f, cy = -800.0f, cz = 8000.0f, r = 2500.0f;
    float k0, k1;
    S32 row, col, inside = 0;
    /* Every column whose ray meets the sphere lies in its row's span. */
    for (row = -40; row <= 40; row++) {
        const float ky = (float)row / 100.0f;
        const S32 any = DynLight_RowSpan(cx, cy, cz, r * r, ky, &k0, &k1);
        for (col = -100; col <= 100; col++) {
            const float kx = (float)col / 100.0f;
            if (ray_meets(kx, ky, cx, cy, cz, r)) {
                inside++;
                ASSERT_TRUE(any);
                ASSERT_TRUE(kx >= k0 - 1e-4f && kx <= k1 + 1e-4f);
            }
        }
    }
    ASSERT_TRUE(inside > 100);
    /* A row above the sphere: none. */
    ASSERT_EQ_INT(0, DynLight_RowSpan(cx, cy, cz, r * r, 0.5f, &k0, &k1));
    /* The camera inside: every column. */
    ASSERT_TRUE(DynLight_RowSpan(100.0f, 0.0f, 500.0f, r * r, 0.0f, &k0, &k1));
    ASSERT_TRUE(k0 < -1e29f && k1 > 1e29f);
}

static void test_the_falloff_in_floating_point(void) {
    static const S32 levels[] = {16, 128, 224, 255};
    for (S32 k = 0; k < 4; k++)
        for (S32 d2 = 0; d2 < 1100000; d2 += 7919) {
            const S32 i = DynLight_Falloff(d2, 1000000, levels[k]);
            const S32 f = DynLight_FalloffF((float)d2, 1.0f / 1000000.0f, levels[k]);
            ASSERT_TRUE(f >= i - 3 && f <= i + 3);
            ASSERT_TRUE(f >= 0);
        }
}

static void test_the_fog_takes_the_light(void) {
    ASSERT_EQ_INT(256, DynLight_FogKeep(5000, 8000, 35700));
    ASSERT_EQ_INT(256, DynLight_FogKeep(8000, 8000, 35700));
    ASSERT_EQ_INT(0, DynLight_FogKeep(35700, 8000, 35700));
    ASSERT_EQ_INT(0, DynLight_FogKeep(50000, 8000, 35700));
    ASSERT_EQ_INT(128, DynLight_FogKeep(21850, 8000, 35700));
    ASSERT_EQ_INT(0, DynLight_FogKeep(9000, 8000, 8000)); /* no fog range: nothing past it */
}

static void test_the_dither_rounds_a_level(void) {
    S32 lit = 0;
    for (S32 y = 0; y < 4; y++)
        for (S32 x = 0; x < 4; x++) {
            ASSERT_EQ_INT(0, DynLight_Level(0, x, y));
            ASSERT_EQ_INT(15, DynLight_Level(255, x, y));
            ASSERT_EQ_INT(3, DynLight_Level(48, x, y));
            lit += DynLight_Level(8, x, y);
        }
    ASSERT_EQ_INT(8, lit); /* half a level: half the pixels of a 4x4 */
}

static void test_the_tint_and_the_glow(void) {
    const U8 grey[3] = {120, 120, 120}, warm[3] = {200, 172, 108}, black[3] = {0, 0, 0};
    S32 d[3];
    DynLight_TintDelta(grey, 256, d);
    ASSERT_EQ_INT(d[0], d[1]);
    ASSERT_EQ_INT(d[1], d[2]);
    ASSERT_EQ_INT(DYN_TINT_LIFT, d[0]); /* a white light only brightens */
    DynLight_TintDelta(warm, 256, d);
    ASSERT_TRUE(d[0] > d[1] && d[1] > d[2]); /* red up, blue down */
    ASSERT_TRUE(d[2] < 0);
    DynLight_TintDelta(warm, 0, d);
    ASSERT_TRUE(d[0] == 0 && d[1] == 0 && d[2] == 0);
    DynLight_TintDelta(black, 256, d);
    ASSERT_TRUE(d[0] == 0 && d[1] == 0 && d[2] == 0);
    ASSERT_EQ_INT(200, (S32)DynLight_Scale(200, 128));
    ASSERT_EQ_INT(255, (S32)DynLight_Scale(200, 256));
    ASSERT_EQ_INT(100, (S32)DynLight_Scale(200, 64));
    ASSERT_EQ_INT(0, (S32)DynLight_Scale(200, -5));
    ASSERT_EQ_INT(100, (S32)DynLight_Screen(100, 0));
    ASSERT_EQ_INT(255, (S32)DynLight_Screen(255, 255));
    ASSERT_TRUE(DynLight_Screen(0, 255) >= 254);
    ASSERT_TRUE(DynLight_Screen(200, 128) < DynLight_Screen(100, 128) + 100); /* less where already bright */
}

/* A full brick's silhouette in its 48 x 38 box, as every brick of the game
   fits in it. */
static int iso_inside(S32 u, S32 v) {
    const float du = (float)u - 23.5f;
    const float a = du < 0 ? -du : du;
    return u >= 0 && u <= 47 && a <= 2.0f * (float)(v + 1) && a + 2.0f * (float)v <= 76.0f;
}

static void test_a_brick_shows_three_faces(void) {
    S32 count[3] = {0, 0, 0}, u, v, face;
    for (v = 0; v < DYN_ISO_H; v++)
        for (u = 0; u < DYN_ISO_W; u++)
            if (iso_inside(u, v)) {
                DynLight_IsoT3(u, v, &face);
                count[face]++;
            }
    ASSERT_EQ_INT(576, count[DYN_ISO_TOP]); /* the diamond */
    ASSERT_EQ_INT(360, count[DYN_ISO_PX]);  /* the right half below it */
    ASSERT_EQ_INT(360, count[DYN_ISO_PZ]);  /* the left half */
    /* The top's middle, and a side's foot. */
    DynLight_IsoT3(23, 11, &face);
    ASSERT_EQ_INT(DYN_ISO_TOP, face);
    DynLight_IsoT3(40, 30, &face);
    ASSERT_EQ_INT(DYN_ISO_PX, face);
    DynLight_IsoT3(6, 30, &face);
    ASSERT_EQ_INT(DYN_ISO_PZ, face);
}

/* The opaque pixels of a box (x1, y1, z1) in the far corner of its cell: the
   pixels whose ray meets it before leaving the cell. */
static S32 iso_box_pixels(double x1, double y1, double z1, U8 *pu, U8 *pv) {
    S32 u, v, n = 0;
    for (v = 0; v < DYN_ISO_H; v++)
        for (u = 0; u < DYN_ISO_W; u++) {
            const double d = 64.0 * u - 1504.0, b = 256.0 * (v - 14);
            const double lo = (d < 0 ? -d : d) > b / 2.0 ? (d < 0 ? -d : d) : b / 2.0;
            double t = 6.0 * x1 - d;
            if (6.0 * z1 + d < t)
                t = 6.0 * z1 + d;
            if ((b + 15.0 * y1) / 2.0 < t)
                t = (b + 15.0 * y1) / 2.0;
            if (t >= lo && iso_inside(u, v)) {
                pu[n] = (U8)u;
                pv[n] = (U8)v;
                n++;
            }
        }
    return n;
}

/* A brick that fills its cell keeps the cell's surface, pixel for pixel; a
   slab, a bench or a table that does not is found from its outline; a slope
   cuts the cell by its plane. */
static void test_a_brick_shows_the_surface_it_draws(void) {
    static U8 pu[DYN_ISO_W * DYN_ISO_H], pv[DYN_ISO_W * DYN_ISO_H];
    static const double boxes[3][3] = {{512.0, 128.0, 512.0}, {256.0, 192.0, 384.0}, {384.0, 96.0, 192.0}};
    T_DYN_ISO_SURF s;
    S32 n, u, v, f0, f1, i, mismatch = 0;
    double t;

    n = iso_box_pixels(512.0, 256.0, 512.0, pu, pv);
    ASSERT_EQ_INT(1296, n); /* 576 + 360 + 360 */
    DynLight_IsoFitBrick(pu, pv, n, 1, &s);
    ASSERT_TRUE(s.x1 == 512.0 && s.y1 == 256.0 && s.z1 == 512.0);
    for (i = 0; i < n; i++) {
        const S32 t3 = DynLight_IsoT3(pu[i], pv[i], &f0);
        t = DynLight_IsoSurfT3(pu[i], pv[i], &s, &f1);
        mismatch += t != (double)t3 || f1 != f0;
    }
    ASSERT_EQ_INT(0, mismatch);

    for (i = 0; i < 3; i++) {
        n = iso_box_pixels(boxes[i][0], boxes[i][1], boxes[i][2], pu, pv);
        DynLight_IsoFitBrick(pu, pv, n, 1, &s);
        ASSERT_TRUE(fabs(s.x1 - boxes[i][0]) <= 24.0);
        ASSERT_TRUE(fabs(s.y1 - boxes[i][1]) <= 12.0);
        ASSERT_TRUE(fabs(s.z1 - boxes[i][2]) <= 24.0);
        /* A pixel of the box's top lies on its top. */
        DynLight_IsoSurfT3(pu[0], pv[0], &s, &f1);
        ASSERT_EQ_INT(DYN_ISO_TOP, f1);
    }

    /* Shape 5 falls toward +x, facing the eye: the top's middle shows its
       plane, x + 2y = 512, deeper than the box's top and inside the cell. */
    n = iso_box_pixels(512.0, 256.0, 512.0, pu, pv);
    DynLight_IsoFitBrick(pu, pv, n, 5, &s);
    ASSERT_EQ_INT(5, s.col);
    t = DynLight_IsoSurfT3(23, 11, &s, &f1);
    ASSERT_EQ_INT(DYN_ISO_S2 + 3, f1);
    ASSERT_TRUE(t < (double)DynLight_IsoT3(23, 11, &f0));
    {
        const double d = 64.0 * 23 - 1504.0, b = 256.0 * (11 - 14);
        const double x = (t / 3.0 + d / 3.0) / 2.0, y = (2.0 * t - b) / 15.0;
        ASSERT_NEAR_F(512.0, x + 2.0 * y, 1e-6);
        ASSERT_TRUE(x > 0.0 && x < 512.0 && y > 0.0 && y < 256.0);
    }
    ASSERT_TRUE(DynLight_IsoDot(DYN_ISO_S2 + 3, 1.0f, 0.0f, 0.0f) > 0.0f);
    ASSERT_TRUE(DynLight_IsoDot(DYN_ISO_S2 + 3, 0.0f, 1.0f, 0.0f) > 0.0f);

    /* A ray that misses the surface keeps the cell's point. */
    for (v = 0; v < DYN_ISO_H; v++)
        for (u = 0; u < DYN_ISO_W; u++)
            if (iso_inside(u, v)) {
                t = DynLight_IsoSurfT3(u, v, &s, &f1);
                ASSERT_TRUE(t <= (double)DynLight_IsoT3(u, v, &f0) + DYN_ISO_EPS);
            }
}

/* The largest of a stream are kept, largest first: a later, smaller one does
   not push out a larger one once the list is full, and what is kept beside
   each count moves with it. */
static void test_the_largest_are_kept(void) {
    /* eight 16s first (at 0, 1, 3, 6, 7, 9, 11, 12), then smaller ones, more
       16s, and a small one last */
    static const S32 counts[24] = {16, 16, 3, 16, 9, 1, 16, 16, 2, 16, 12, 16, 16, 5, 16, 16, 1, 7, 16, 4, 16, 15, 14, 2};
    static const S32 first[8] = {0, 1, 3, 6, 7, 9, 11, 12};
    S32 n[8], tag[8], nb = 0, k, j;
    for (k = 0; k < 24; k++) {
        j = DynLight_KeepLargest(n, &nb, 8, counts[k]);
        if (j < 0)
            continue;
        memmove(&tag[j + 1], &tag[j], sizeof(S32) * (size_t)(nb - 1 - j));
        tag[j] = k;
    }
    ASSERT_EQ_INT(8, nb);
    for (k = 0; k < 8; k++) {
        ASSERT_EQ_INT(16, n[k]);
        ASSERT_EQ_INT(first[k], tag[k]); /* ties in the order they came */
    }
    /* a short stream keeps everything, largest first */
    nb = 0;
    for (k = 2; k < 6; k++)
        DynLight_KeepLargest(n, &nb, 8, counts[k]);
    ASSERT_EQ_INT(4, nb);
    ASSERT_EQ_INT(16, n[0]);
    ASSERT_EQ_INT(9, n[1]);
    ASSERT_EQ_INT(3, n[2]);
    ASSERT_EQ_INT(1, n[3]);
}

/* A sprite's runs (skip, copy, repeat) land in its box at its corner. */
static void test_a_sprite_is_laid_into_its_box(void) {
    static const U8 spr[] = {5, 2, 10, 20,      /* 5 x 2 at (10, 20) */
                             3, 0x00,           /* row 0: skip 1, */
                             0x40, 7,           /* copy 1 (7), */
                             0x81, 9,           /* 9 twice */
                             1, 0x42, 1, 2, 3}; /* row 1: copy 3 (1, 2, 3) */
    static U8 idx[DYN_ISO_W * DYN_ISO_H], opq[DYN_ISO_W * DYN_ISO_H];
    const S32 n = DynLight_SpriteCanvas(spr, idx, opq);
    ASSERT_EQ_INT(6, n);
    ASSERT_EQ_INT(0, opq[20 * DYN_ISO_W + 10]);
    ASSERT_EQ_INT(7, idx[20 * DYN_ISO_W + 11]);
    ASSERT_EQ_INT(9, idx[20 * DYN_ISO_W + 12]);
    ASSERT_EQ_INT(9, idx[20 * DYN_ISO_W + 13]);
    ASSERT_EQ_INT(3, idx[21 * DYN_ISO_W + 12]);
    ASSERT_EQ_INT(1, opq[21 * DYN_ISO_W + 10]);
}

/* A lamp: a round white spot, hotter at its core, ringed with its glass's
   colour, inside a dim brick. A white wall's flat sheen is none. */
static void test_a_painted_lamp_is_found(void) {
    static U8 pal[768], idx[DYN_ISO_W * DYN_ISO_H], opq[DYN_ISO_W * DYN_ISO_H];
    T_DYN_LAMP_BLOB b;
    S32 u, v;
    memset(pal, 0, sizeof(pal));
    pal[3 * 1] = pal[3 * 1 + 1] = pal[3 * 1 + 2] = 90;  /* 1: the brick, grey */
    pal[3 * 2] = pal[3 * 2 + 1] = pal[3 * 2 + 2] = 255; /* 2: the core, white */
    pal[3 * 3] = pal[3 * 3 + 1] = pal[3 * 3 + 2] = 228; /* 3: the spot's rim */
    pal[3 * 4] = 40;                                    /* 4: the glass, cyan */
    pal[3 * 4 + 1] = pal[3 * 4 + 2] = 210;
    for (v = 0; v < DYN_ISO_H; v++)
        for (u = 0; u < DYN_ISO_W; u++) {
            const S32 du = u - 24, dv = v - 18, d2 = du * du + dv * dv;
            opq[v * DYN_ISO_W + u] = 1;
            idx[v * DYN_ISO_W + u] = (U8)(d2 <= 9 ? 2 : (d2 <= 25 ? 3 : (d2 <= 49 ? 4 : 1)));
        }
    ASSERT_EQ_INT(1, DynLight_LampBlob(idx, opq, DYN_ISO_W * DYN_ISO_H, pal, &b));
    ASSERT_NEAR_F(24.0f, b.cu, 0.01f);
    ASSERT_NEAR_F(18.0f, b.cv, 0.01f);
    ASSERT_EQ_INT(81, b.area); /* the 9 x 9 disc of d^2 <= 25 */
    ASSERT_EQ_INT(4, b.colour);

    /* The same spot all core: no shading, a sheen. */
    for (u = 0; u < DYN_ISO_W * DYN_ISO_H; u++)
        if (idx[u] == 3)
            idx[u] = 2;
    ASSERT_EQ_INT(0, DynLight_LampBlob(idx, opq, DYN_ISO_W * DYN_ISO_H, pal, &b));
    /* A long white stripe: not round. */
    for (u = 0; u < DYN_ISO_W * DYN_ISO_H; u++)
        idx[u] = 1;
    for (v = 16; v <= 20; v++)
        for (u = 4; u <= 44; u++)
            idx[v * DYN_ISO_W + u] = (U8)(v == 18 ? 2 : 3);
    ASSERT_EQ_INT(0, DynLight_LampBlob(idx, opq, DYN_ISO_W * DYN_ISO_H, pal, &b));
}

/* The lava's field: the top of a cell amid a lake takes DYN_LAVA_ONE, none
   past its reach, a face turned away from the lake less than the floor
   beside it, and a point past the room's last corner reads inside the
   field. */
static void test_the_lava_lights_by_its_field(void) {
    float *acc = (float *)calloc((size_t)DYN_LAVA_CORNERS * 3, sizeof(float));
    U16 *field = (U16 *)malloc(sizeof(U16) * DYN_LAVA_CORNERS * 3);
    S32 x, z, k;
    ASSERT_TRUE(acc != NULL && field != NULL);
    if (acc == NULL || field == NULL)
        return;
    for (z = 0; z < 64; z++)
        for (x = 0; x < 64; x++)
            DynLight_LavaAdd(acc, (float)(512 * x + 256), (float)(256 + DYN_LAVA_LIFT), (float)(512 * z + 256));
    /* the corner of a cell's top amid it (DYN_LAVA_LAKE is taken at the
       top's middle, half a cell off) */
    ASSERT_NEAR_F(DYN_LAVA_LAKE, acc[((32 * DYN_LAVA_GY + 1) * DYN_LAVA_GX + 32) * 3], 0.03f);
    for (k = 0; k < DYN_LAVA_CORNERS * 3; k++) {
        const float f = acc[k] * ((float)DYN_LAVA_ONE / DYN_LAVA_LAKE);
        field[k] = (U16)(f >= 65535.0f ? 65535 : (S32)(f + 0.5f));
    }
    ASSERT_NEAR_F((float)DYN_LAVA_ONE, (float)DynLight_LavaAt(field, 16384.0f, 256.0f, 16384.0f, 0), 8.0f);
    ASSERT_TRUE(DynLight_LavaAt(field, 16384.0f, 2560.0f, 16384.0f, 0) == 0); /* beyond its reach */
    /* past the last corners, and below the first */
    ASSERT_TRUE(DynLight_LavaAt(field, 40000.0f, 9000.0f, -100.0f, 2) >= 0);
    ASSERT_TRUE(DynLight_LavaAt(field, -100.0f, -100.0f, 40000.0f, 1) >= 0);

    /* A lake that ends at z = 8192. */
    memset(acc, 0, sizeof(float) * DYN_LAVA_CORNERS * 3);
    for (z = 0; z < 16; z++)
        for (x = 0; x < 64; x++)
            DynLight_LavaAdd(acc, (float)(512 * x + 256), (float)(256 + DYN_LAVA_LIFT), (float)(512 * z + 256));
    for (k = 0; k < DYN_LAVA_CORNERS * 3; k++) {
        const float f = acc[k] * ((float)DYN_LAVA_ONE / DYN_LAVA_LAKE);
        field[k] = (U16)(f >= 65535.0f ? 65535 : (S32)(f + 0.5f));
    }
    /* Just past it, a face toward +z, turned away from it, takes less than
       the floor there. */
    ASSERT_TRUE(DynLight_LavaAt(field, 16384.0f, 300.0f, 8192.0f + 256.0f, 2) <
                DynLight_LavaAt(field, 16384.0f, 300.0f, 8192.0f + 256.0f, 0));
    free(acc);
    free(field);
}

/* Every pixel of a cell's silhouette, kept and read back, lands on the face
   it shows, inside the cell, and projects back to its pixel: for cells and
   cameras all over a room, at three sizes. */
static void test_a_background_pixel_reads_back_its_point(void) {
    static const S32 sizes[3][2] = {{640, 480}, {1920, 1080}, {1366, 768}};
    U32 seed = 12345;
    S32 n, checked = 0;
    for (n = 0; n < 300; n++) {
        const S32 W = sizes[n % 3][0], H = sizes[n % 3][1];
        S32 cx, cy, cz, sx, sy, sz, u, v;
        seed = seed * 1103515245u + 12345u;
        cx = (S32)((seed >> 8) % 64);
        seed = seed * 1103515245u + 12345u;
        cz = (S32)((seed >> 8) % 64);
        seed = seed * 1103515245u + 12345u;
        cy = (S32)((seed >> 8) % 25);
        seed = seed * 1103515245u + 12345u;
        sx = cx - 6 + (S32)((seed >> 8) % 13);
        sz = cz - 6 + (S32)((seed >> 12) % 13);
        sy = cy - 3 + (S32)((seed >> 16) % 7);
        {
            const S32 camX = sx * 512, camY = sy * 256, camZ = sz * 512, xc = W / 2 - 9, yc = H / 2;
            const S32 xs = 24 * ((cx - sx) - (cz - sz)) + W / 2 - 32; /* Map2Screen */
            const S32 ys = 12 * ((cx - sx) + (cz - sz)) - 15 * (cy - sy) + H / 2 - 14;
            for (v = 0; v < DYN_ISO_H; v++)
                for (u = 0; u < DYN_ISO_W; u++) {
                    float X, Y, Z, px, py;
                    S32 face, t3;
                    if (!iso_inside(u, v))
                        continue;
                    t3 = DynLight_IsoT3(u, v, &face);
                    DynLight_IsoPoint(DynLight_IsoS2(cx, cz, t3), xs + u, ys + v, camX, camY, camZ, xc, yc, &X,
                                      &Y, &Z);
                    if (face == DYN_ISO_TOP)
                        ASSERT_TRUE(Y > 256.0f * (cy + 1) - 2.0f && Y < 256.0f * (cy + 1) + 2.0f);
                    else if (face == DYN_ISO_PX)
                        ASSERT_TRUE(X > 512.0f * (cx + 1) - 2.0f && X < 512.0f * (cx + 1) + 2.0f);
                    else
                        ASSERT_TRUE(Z > 512.0f * (cz + 1) - 2.0f && Z < 512.0f * (cz + 1) + 2.0f);
                    ASSERT_TRUE(X > 512.0f * cx - 2.0f && X < 512.0f * (cx + 1) + 2.0f);
                    ASSERT_TRUE(Z > 512.0f * cz - 2.0f && Z < 512.0f * (cz + 1) + 2.0f);
                    ASSERT_TRUE(Y > 256.0f * cy - 2.0f && Y < 256.0f * (cy + 1) + 2.0f);
                    /* An object at that point is drawn half a pixel left of
                       the brick's pixel and a pixel below its top edge. */
                    DynLight_IsoProject(X, Y, Z, camX, camY, camZ, xc, yc, &px, &py);
                    ASSERT_TRUE(px > (float)(xs + u) - 1.0f && px < (float)(xs + u) + 0.01f);
                    ASSERT_TRUE(py > (float)(ys + v) + 0.5f && py < (float)(ys + v) + 1.5f);
                    checked++;
                }
        }
    }
    ASSERT_EQ_INT(300 * 1296, checked);
}

static void test_the_view_and_the_faces_take_the_light(void) {
    /* Nearer the viewer along (1, 0.8, 1) is larger. */
    ASSERT_TRUE(DynLight_IsoNear(1000.0f, 0.0f, 1000.0f) > DynLight_IsoNear(500.0f, 0.0f, 500.0f));
    ASSERT_TRUE(DynLight_IsoNear(0.0f, 1000.0f, 0.0f) > DynLight_IsoNear(0.0f, 0.0f, 0.0f));
    /* A face toward the light takes it all, one turned away none. */
    ASSERT_EQ_INT(256, DynLight_IsoLambert(100.0f, 100.0f));
    ASSERT_EQ_INT(0, DynLight_IsoLambert(-100.0f, 100.0f));
    ASSERT_TRUE(DynLight_IsoLambert(0.0f, 100.0f) > 0 && DynLight_IsoLambert(0.0f, 100.0f) < 80);
    ASSERT_EQ_INT(256, DynLight_IsoLambert(0.0f, 0.0f)); /* at the light */
    /* A dark room takes the light whole, a bright one half. */
    ASSERT_EQ_INT(256, DynLight_IsoStrength(20));
    ASSERT_EQ_INT(128, DynLight_IsoStrength(200));
    ASSERT_EQ_INT(192, DynLight_IsoStrength(80));
}

static void test_a_body_shade_takes_the_light_it_faces(void) {
    const float v[3] = {0.0f, 0.0f, 0.0f};
    const float rel[2][4] = {{0.0f, 500.0f, 0.0f, 2000.0f * 2000.0f}, {0.0f, -500.0f, 0.0f, 2000.0f * 2000.0f}};
    const S32 level[2] = {176, 176};
    /* A unit normal up: the light above raises it, the one below does not. */
    const U16 up = DynLight_BodyShade(1000, v, 0.0f, 16384.0f, 0.0f, rel, level, 1);
    ASSERT_TRUE(up > 1000);
    ASSERT_EQ_INT(1000, DynLight_BodyShade(1000, v, 0.0f, 16384.0f, 0.0f, rel + 1, level, 1));
    /* Facing it full on, at 500 of a 2000 reach: (15/16)^2 of 176 sixteenths, half of it in rows. */
    ASSERT_TRUE(up - 1000 > 1200 && up - 1000 < 1500);
    /* Never past the top the normal's length allows, never lowered. */
    ASSERT_EQ_INT(3840, DynLight_BodyShade(3800, v, 0.0f, 16384.0f, 0.0f, rel, level, 1));
    ASSERT_EQ_INT(1920, DynLight_BodyShade(1900, v, 0.0f, 8192.0f, 0.0f, rel, level, 1)); /* half a unit normal */
    ASSERT_EQ_INT(4000, DynLight_BodyShade(4000, v, 0.0f, 16384.0f, 0.0f, rel, level, 1));
    /* Out of reach, or a null normal: as it was. */
    ASSERT_EQ_INT(1000, DynLight_BodyShade(1000, v, 0.0f, 0.0f, 0.0f, rel, level, 1));
    {
        const float far[1][4] = {{0.0f, 2500.0f, 0.0f, 2000.0f * 2000.0f}};
        ASSERT_EQ_INT(1000, DynLight_BodyShade(1000, v, 0.0f, 16384.0f, 0.0f, far, level, 1));
    }
}

int main(void) {
    RUN_TEST(test_a_pool_is_found_at_its_centre);
    RUN_TEST(test_no_pool_on_plain_or_dim_ground);
    RUN_TEST(test_the_map_edge_is_read_safely);
    RUN_TEST(test_the_surface_table_keeps_the_ramp);
    RUN_TEST(test_a_lit_colour_keeps_its_hue);
    RUN_TEST(test_the_falloff);
    RUN_TEST(test_a_lamp_is_a_filled_ringed_pool_under_the_decor);
    RUN_TEST(test_the_globe_is_the_highest_polygon);
    RUN_TEST(test_a_sphere_on_screen);
    RUN_TEST(test_the_rows_a_sphere_covers);
    RUN_TEST(test_the_falloff_in_floating_point);
    RUN_TEST(test_the_fog_takes_the_light);
    RUN_TEST(test_the_dither_rounds_a_level);
    RUN_TEST(test_the_tint_and_the_glow);
    RUN_TEST(test_a_brick_shows_three_faces);
    RUN_TEST(test_a_brick_shows_the_surface_it_draws);
    RUN_TEST(test_the_largest_are_kept);
    RUN_TEST(test_a_sprite_is_laid_into_its_box);
    RUN_TEST(test_a_painted_lamp_is_found);
    RUN_TEST(test_the_lava_lights_by_its_field);
    RUN_TEST(test_a_background_pixel_reads_back_its_point);
    RUN_TEST(test_the_view_and_the_faces_take_the_light);
    RUN_TEST(test_a_body_shade_takes_the_light_it_faces);
    TEST_SUMMARY();
    return test_failures != 0;
}
