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
    TEST_SUMMARY();
    return test_failures != 0;
}
