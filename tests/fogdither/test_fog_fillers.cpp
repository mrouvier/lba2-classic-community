/* Host test for the fillers the fog dither and the water redraw's texture
 * filter reach (LIB386/pol_work/POLYTZF.CPP, POLYGTEX.CPP, POLYTEXT.CPP), run as
 * the game runs them, on this host's C++ fillers.
 *
 * The equivalence suite (tests/pol_work) compares these fillers with the 1997
 * assembly at the defaults, where both options are off and their branches are
 * dead. What it cannot see is what the branches draw:
 *
 *   - the dither has to land at the pixel's place in the frame, the span's
 *     column phase and the line's row of the 4x4 matrix, while the band stays
 *     the one the depth gives, and with it off every pixel of the span takes
 *     the original's band;
 *   - the water's redraw goes through the no-Z-write fillers, which carry a
 *     copy of their Z-buffered twins' filter: filtered, the redraw has to put
 *     down the very bytes the frame drew, or the water shimmers every quarter
 *     second; outside the redraw (PolyTexFilterNZW 0) the same fillers draw
 *     the objects in water unfiltered, as before. */
#include "test_harness.h"
#include <POLYGON/POLY.H>
#include <POLYGON/POLYFILT.H>
#include <POLYGON/POLYTZF.H>
#include <SVGA/SCREEN.H>
#include <SVGA/CLIP.H>
#include "poly_test_fixture.h"
#include <string.h>

extern U32 Fill_Fog_Factor;    /* POLY.CPP */
extern U32 Fill_ScaledFogNear; /* POLY.CPP */

/* ── The dither, through Filler_TextureZFogSmoothZBuf ─────────────────────── */

/* The fog CLUT the fillers index as PtrCLUTFog + 12*256 + band + texel, holding
   at each entry the band it belongs to (0x80 | band), so a pixel reads back
   the band it was drawn with whatever the texel. */
static U8 g_band_clut[65536];

static void setup_tzf_strip(U32 startY, U32 nbLines, U32 xmin_fp, U32 xmax_fp) {
    setup_filler_common(startY, nbLines, xmin_fp, xmax_fp, 0);
    init_test_texture();
    PtrMap = g_test_texture;
    RepMask = 0xFFFF;
    Fill_Patch = 1;
    for (int i = 0; i < 65536; i++)
        g_band_clut[i] = (U8)(i < 12 * 256 ? 0 : 0x80 | (((i - 12 * 256) >> 12) & 15));
    PtrCLUTFog = g_band_clut;

    Fill_CurMapUMin = 0;
    Fill_CurMapVMin = 0;
    Fill_MapU_LeftSlope = 0;
    Fill_MapV_LeftSlope = 0;
    Fill_MapU_XSlope = 0x10000;
    Fill_MapV_XSlope = 0;
    Fill_Cur_W = 0x10000;
    Fill_W_XSlope = 0;
    Fill_CurWMin = 0x10000;
    Fill_W_LeftSlope = 0;
    Fill_Cur_MapUOverW = 0;
    Fill_Cur_MapVOverW = 0;
    Fill_Next_W = 0x10000;
    Fill_Next_MapUOverW = 0;
    Fill_Next_MapVOverW = 0;
    Fill_Next_MapU = 0;
    Fill_Next_MapV = 0;
    Fill_CurZBufMin = 0x10000;
    Fill_ZBuf_LeftSlope = 0;
    Fill_ZBuf_XSlope = 0;
    Fill_CurZBuf = 0;

    init_test_zbuffer(0xFFFF);
    PtrZBuffer = (PTR_U16)g_test_zbuffer;
}

/* Draw a strip at a flat depth whose fog value is `raw`: one pass to learn the
   depth the strip is drawn at, then the fog range set so that depth is one
   step past the fog's start and the factor makes it `raw`. */
static void draw_strip(U32 raw, S32 dither, U32 y, U32 h, U32 x0, U32 x1) {
    setup_tzf_strip(y, h, x0 << 16, x1 << 16);
    PolyFogDither = 0;
    Filler_TextureZFogSmoothZBuf(h, x0 << 16, x1 << 16);
    const S32 depth = (S32)(Fill_CurZBuf >> 8);

    setup_tzf_strip(y, h, x0 << 16, x1 << 16);
    Fill_ScaledFogNear = (U32)(depth - 1);
    Fill_Fog_Factor = raw << 8;
    PolyFogDither = dither;
    Filler_TextureZFogSmoothZBuf(h, x0 << 16, x1 << 16);
    PolyFogDither = 0;
}

static void check_strip(U32 raw, S32 dither, U32 y, U32 h, U32 x0, U32 x1) {
    char label[96];
    int drawn = 0;
    draw_strip(raw, dither, y, h, x0, x1);
    for (U32 row = y; row <= y + h; row++) {
        for (U32 x = x0; x < x1; x++) {
            const U32 band = dither ? PolyTZF_FogDitherRow(raw, x, row) : (raw & 0xF000);
            snprintf(label, sizeof(label), "raw 0x%04x dither %d at (%u, %u)", raw, dither, x, row);
            ASSERT_EQ_INT_MSG((int)(0x80 | (band >> 12)), (int)g_poly_framebuf[row * TEST_POLY_W + x], label);
            drawn++;
        }
    }
    ASSERT_EQ_INT((int)((x1 - x0) * (h + 1)), drawn);
}

static void test_off_is_the_original_band(void) {
    check_strip(0x5B40, 0, 20, 5, 3, 77);
    check_strip(0x7000, 0, 31, 3, 10, 100);
}

static void test_the_dither_lands_where_the_pixel_is(void) {
    /* Spans starting off the 4-column grid, several spans a line, lines off
       the 4-row grid, on both sides of a band's middle and on its edge. */
    check_strip(0x5B40, 1, 21, 6, 3, 77);
    check_strip(0x5340, 1, 30, 4, 1, 99);
    check_strip(0x7000, 1, 47, 7, 6, 150);
    check_strip(0xF800, 1, 60, 2, 0, 40); /* the last band's deep half stays */
}

static void test_a_band_middle_is_flat_with_the_dither(void) {
    int flat = 1;
    draw_strip(0x6800, 1, 40, 7, 2, 90);
    for (U32 row = 40; row <= 47; row++)
        for (U32 x = 2; x < 90; x++)
            flat &= g_poly_framebuf[row * TEST_POLY_W + x] == (0x80 | 6);
    ASSERT_TRUE(flat);
}

/* ── The water redraw's filter, through the no-Z-write bank ───────────────── */

static U8 g_pal[768];
static U8 g_frame_zbuf[TEST_POLY_SIZE];
static U8 g_frame_nzw[TEST_POLY_SIZE];

static void setup_terrain_pass(S32 type) {
    setup_polygon_screen();
    init_test_texture();
    /* Holes for the chroma-key types, so their opaque-taps-only rule runs. */
    if (type == POLY_TEXTURE_INCRUST || type == POLY_TEXTURE_INCRUST_GOURAUD)
        for (int i = 0; i < TEST_TEX_PIXELS; i += 7)
            g_test_texture[i] = 0;
    PtrMap = g_test_texture;
    RepMask = 0xFFFF;
    init_test_clut();
    PtrCLUTGouraud = g_test_clut;
    for (int i = 0; i < 256; i++)
        Fill_Logical_Palette[i] = (U8)i;
    init_test_zbuffer(0xFFFF);
    PtrZBuffer = (PTR_U16)g_test_zbuffer;
}

static Struc_Point terrain_point(S16 x, S16 y, U16 u, U16 v, U16 light) {
    Struc_Point p;
    memset(&p, 0, sizeof(p));
    p.Pt_XE = x;
    p.Pt_YE = y;
    p.Pt_MapU = u;
    p.Pt_MapV = v;
    p.Pt_Light = light;
    p.Pt_ZO = 0x4000;
    p.Pt_W = 0x10000;
    return p;
}

/* One magnified triangle of terrain, so the filter's sub-texel weights vary. */
static void draw_terrain(S32 type, U32 bank, S32 filter, S32 scope) {
    Struc_Point pts[3];
    setup_terrain_pass(type);
    /* Wound as the rasterizer draws a front face. */
    pts[0] = terrain_point(10, 8, 0, 0, 0x0400);
    pts[1] = terrain_point(40, 112, 9 << 8, 31 << 8, 0x0700);
    pts[2] = terrain_point(150, 30, 37 << 8, 5 << 8, 0x0C00);
    PolyTexFilter = filter;
    PolyTexFilterNZW = scope;
    Poly_EnsureBlendLUT();
    Switch_Fillers(bank);
    Fill_Poly(type, 0, 3, pts);
    Switch_Fillers(FILL_POLY_NO_TEXTURES);
    PolyTexFilter = POLY_TEX_FILTER_OFF;
    PolyTexFilterNZW = 0;
}

/* Two frames the same byte for byte; on a difference, the first one, named. */
static void assert_same_frame(const U8 *expected, const U8 *actual, const char *label) {
    for (int i = 0; i < TEST_POLY_SIZE; i++) {
        if (expected[i] != actual[i]) {
            char where[160];
            snprintf(where, sizeof(where), "%s, first at (%d, %d)", label, i % TEST_POLY_W, i / TEST_POLY_W);
            ASSERT_EQ_INT_MSG(expected[i], actual[i], where);
            return;
        }
    }
    ASSERT_TRUE(1);
}

static void check_redraw(S32 type, const char *name) {
    static U8 unfiltered[TEST_POLY_SIZE];
    char label[96];
    int differ = 0;

    for (S32 filter = POLY_TEX_FILTER_HORIZONTAL; filter <= POLY_TEX_FILTER_BILINEAR; filter++) {
        draw_terrain(type, FILL_POLY_FOG_ZBUFFER, filter, 0);
        memcpy(g_frame_zbuf, g_poly_framebuf, TEST_POLY_SIZE);
        draw_terrain(type, FILL_POLY_FOG_NZW, filter, 1);
        memcpy(g_frame_nzw, g_poly_framebuf, TEST_POLY_SIZE);
        snprintf(label, sizeof(label), "%s: the redraw is the frame's bytes (filter %d)", name, filter);
        assert_same_frame(g_frame_zbuf, g_frame_nzw, label);
    }

    draw_terrain(type, FILL_POLY_FOG_ZBUFFER, POLY_TEX_FILTER_OFF, 0);
    memcpy(unfiltered, g_poly_framebuf, TEST_POLY_SIZE);
    draw_terrain(type, FILL_POLY_FOG_NZW, POLY_TEX_FILTER_BILINEAR, 0);
    snprintf(label, sizeof(label), "%s: outside the redraw the bank draws unfiltered", name);
    assert_same_frame(unfiltered, g_poly_framebuf, label);

    /* And the filter does something here, or the two checks above prove
       nothing. */
    for (int i = 0; i < TEST_POLY_SIZE; i++)
        differ += unfiltered[i] != g_frame_zbuf[i];
    {
        int drawn = 0;
        for (int i = 0; i < TEST_POLY_SIZE; i++)
            drawn += unfiltered[i] != 0;
        printf("# %s: drawn %d, filtered differs %d\n", name, drawn, differ);
    }
    ASSERT_TRUE(differ > 100);
}

static void test_the_redraw_keeps_the_filter(void) {
    for (int i = 0; i < 256; i++) {
        g_pal[i * 3 + 0] = (U8)((i & 15) * 17);
        g_pal[i * 3 + 1] = (U8)(((i >> 4) & 15) * 17);
        g_pal[i * 3 + 2] = (U8)(255 - (i & 15) * 17);
    }
    Poly_BuildBlendLUT(g_pal);

    check_redraw(POLY_TEXTURE, "texture fog");
    check_redraw(POLY_TEXTURE_GOURAUD, "texture Gouraud");
    check_redraw(POLY_TEXTURE_INCRUST, "chroma-key texture fog");
    check_redraw(POLY_TEXTURE_INCRUST_GOURAUD, "chroma-key texture Gouraud");
}

int main(void) {
    RUN_TEST(test_off_is_the_original_band);
    RUN_TEST(test_the_dither_lands_where_the_pixel_is);
    RUN_TEST(test_a_band_middle_is_flat_with_the_dither);
    RUN_TEST(test_the_redraw_keeps_the_filter);
    TEST_SUMMARY();
    return test_failures != 0;
}
