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

/* ── The dither, through the three perspective fog fillers ────────────────── */

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
    Fill_MapU_XSlope = 0x5000; /* fractional texels, so a filter weighs its taps */
    Fill_MapV_XSlope = 0x3000;
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

typedef S32 (*T_FILLER)(U32 nbLines, U32 fillCurXMin, U32 fillCurXMax);

/* The three fillers of the polygon type the sea and the sky are drawn with:
   the plain one finds its pixel's place from Log, the two Z-buffer ones from
   the Log they cached when they were patched. All three draw against the
   0xFFFF Z-buffer the setup writes. */
static const struct {
    T_FILLER fill;
    const char *name;
} g_fillers[] = {
    {Filler_TextureZFogSmooth, "plain"},
    {Filler_TextureZFogSmoothZBuf, "ZBuf"},
    {Filler_TextureZFogSmoothNZW, "NZW"},
};
#define FILLER_COUNT ((int)(sizeof(g_fillers) / sizeof(g_fillers[0])))

/* Draw a strip whose fog value is `raw` at its first span: one pass to learn
   the depth the strip starts at, then the fog range set so that depth is one
   step past the fog's start and the factor makes it `raw`. With `zslope` the
   depth rises a unit a 32-pixel span, so span k is at (k + 1) * raw. */
static void draw_strip(T_FILLER fill, U32 raw, S32 dither, U32 y, U32 h, U32 x0, U32 x1, S32 zslope) {
    setup_tzf_strip(y, h, x0 << 16, x1 << 16);
    PolyFogDither = 0;
    fill(h, x0 << 16, x1 << 16);
    const S32 depth = (S32)(Fill_CurZBuf >> 8); /* the first span's: that pass is flat */

    setup_tzf_strip(y, h, x0 << 16, x1 << 16);
    Fill_ZBuf_XSlope = zslope;
    Fill_ScaledFogNear = (U32)(depth - 1);
    Fill_Fog_Factor = raw << 8;
    PolyFogDither = dither;
    fill(h, x0 << 16, x1 << 16);
    PolyFogDither = 0;
}

/* The band each pixel of a strip shows, from the band CLUT, against what the
   original or the dither gives for its span's fog value; and nothing drawn
   either side of the strip. */
static void check_strip(int f, U32 raw, S32 dither, U32 y, U32 h, U32 x0, U32 x1, S32 zslope) {
    char label[128];
    draw_strip(g_fillers[f].fill, raw, dither, y, h, x0, x1, zslope);
    for (U32 row = y; row <= y + h; row++) {
        for (U32 x = x0; x < x1; x++) {
            const U32 spanRaw = zslope ? (((x - x0) / 32 + 1) * (raw << 8)) >> 8 : raw;
            const U32 band = dither ? PolyTZF_FogDitherRow(spanRaw, x, row) : (spanRaw & 0xF000);
            snprintf(label, sizeof(label), "%s raw 0x%04x dither %d at (%u, %u)", g_fillers[f].name, spanRaw, dither,
                     x, row);
            ASSERT_EQ_INT_MSG((int)(0x80 | (band >> 12)), (int)g_poly_framebuf[row * TEST_POLY_W + x], label);
        }
        snprintf(label, sizeof(label), "%s: nothing drawn beside line %u", g_fillers[f].name, row);
        if (x0 > 0)
            ASSERT_EQ_INT_MSG(0, (int)g_poly_framebuf[row * TEST_POLY_W + x0 - 1], label);
        ASSERT_EQ_INT_MSG(0, (int)g_poly_framebuf[row * TEST_POLY_W + x1], label);
    }
}

static void test_off_is_the_original_band(void) {
    for (int f = 0; f < FILLER_COUNT; f++) {
        check_strip(f, 0x5B40, 0, 20, 5, 3, 77, 0);
        check_strip(f, 0x7000, 0, 31, 3, 10, 100, 0);
    }
}

static void test_the_dither_lands_where_the_pixel_is(void) {
    /* Spans starting off the 4-column grid, several spans a line, lines off
       the 4-row grid, on both sides of a band's middle and on its edge. */
    for (int f = 0; f < FILLER_COUNT; f++) {
        check_strip(f, 0x5B40, 1, 21, 6, 3, 77, 0);
        check_strip(f, 0x5340, 1, 30, 4, 1, 99, 0);
        check_strip(f, 0x7000, 1, 47, 7, 6, 150, 0);
    }
}

/* Each span takes its own depth's band, dithered or not: a band carried over
   from the span before would show here. */
static void test_each_span_takes_its_own_band(void) {
    for (int f = 0; f < FILLER_COUNT; f++) {
        check_strip(f, 0x0530, 0, 50, 3, 2, 150, 8);
        check_strip(f, 0x0530, 1, 50, 3, 2, 150, 8);
    }
}

static void test_a_band_middle_is_flat_with_the_dither(void) {
    for (int f = 0; f < FILLER_COUNT; f++) {
        int flat = 1;
        draw_strip(g_fillers[f].fill, 0x6800, 1, 40, 7, 2, 90, 0);
        for (U32 row = 40; row <= 47; row++)
            for (U32 x = 2; x < 90; x++)
                flat &= g_poly_framebuf[row * TEST_POLY_W + x] == (0x80 | 6);
        ASSERT_TRUE(flat);
    }
}

/* The deep half of the last band stays on it: there is no band after it. */
static void test_the_last_band_stays(void) {
    for (int f = 0; f < FILLER_COUNT; f++) {
        int last = 1;
        draw_strip(g_fillers[f].fill, 0xFC40, 1, 60, 2, 0, 40, 0);
        for (U32 row = 60; row <= 62; row++)
            for (U32 x = 0; x < 40; x++)
                last &= g_poly_framebuf[row * TEST_POLY_W + x] == (0x80 | 15);
        ASSERT_TRUE(last);
    }
}

/* With the texture filter on, every tap of the blend takes the pixel's
   dithered band: on the band CLUT every tap reads the band, so the pixel
   does too, whatever the sub-texel weights. */
static void test_the_filter_follows_the_dithered_band(void) {
    static U8 pal[768];
    for (int i = 0; i < 256; i++) {
        pal[i * 3 + 0] = (U8)i;
        pal[i * 3 + 1] = (U8)(255 - i);
        pal[i * 3 + 2] = (U8)((i * 7) & 0xFF);
    }
    Poly_BuildBlendLUT(pal);
    PolyTexFilter = POLY_TEX_FILTER_BILINEAR;
    Poly_EnsureBlendLUT();
    for (int f = 1; f < FILLER_COUNT; f++) {
        /* The plain filler has no filter; the NZW one filters inside its scope. */
        PolyTexFilterNZW = 1;
        check_strip(f, 0x7300, 1, 70, 4, 5, 140, 0);
        PolyTexFilterNZW = 0;
    }
    PolyTexFilter = POLY_TEX_FILTER_OFF;
}

/* ── The water redraw's filter, through the no-Z-write bank ───────────────── */

static U8 g_pal[768];
static U8 g_frame_zbuf[TEST_POLY_SIZE];
static U8 g_frame_nzw[TEST_POLY_SIZE];

static void setup_terrain_pass(S32 type) {
    setup_polygon_screen();
    /* A texture that changes along both axes, so the vertical taps matter. */
    for (int i = 0; i < TEST_TEX_PIXELS; i++)
        g_test_texture[i] = (U8)((((i >> 8) * 5) + (i & 0xFF) * 3) | 1);
    /* Holes for the chroma-key types, so their opaque-taps-only rule runs. */
    if (type == POLY_TEXTURE_INCRUST || type == POLY_TEXTURE_INCRUST_GOURAUD)
        for (int i = 0; i < TEST_TEX_PIXELS; i += 7)
            g_test_texture[i] = 0;
    PtrMap = g_test_texture;
    RepMask = 0xFFFF;
    /* Shade rows that differ and a fog palette that is not the identity, so a
       tap shaded or fogged differently from the frame shows. */
    for (int i = 0; i < TEST_CLUT_SIZE; i++)
        g_test_clut[i] = (U8)((i & 0xFF) ^ ((i >> 8) * 0x1D));
    PtrCLUTGouraud = g_test_clut;
    PtrCLUTFog = g_test_clut; /* the perspective fog filler's, the sea's */
    for (int i = 0; i < 256; i++)
        Fill_Logical_Palette[i] = (U8)(i * 7 + 3);
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
    if (type == POLY_TEXTURE_Z_FOG) {
        /* The sea's polygon type: depths that differ across the triangle, and
           a fog range over them, so several fog rows are drawn. */
        pts[0].Pt_ZO = 0x1000;
        pts[0].Pt_W = 0x18000;
        pts[1].Pt_ZO = 0x6000;
        pts[1].Pt_W = 0x0C000;
        pts[2].Pt_ZO = 0xB000;
        pts[2].Pt_W = 0x08000;
        Fill_ScaledFogNear = 0;
        Fill_Fog_Factor = 0x400;
    }
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

    static U8 horizontal[TEST_POLY_SIZE];
    for (S32 filter = POLY_TEX_FILTER_HORIZONTAL; filter <= POLY_TEX_FILTER_BILINEAR; filter++) {
        draw_terrain(type, FILL_POLY_FOG_ZBUFFER, filter, 0);
        memcpy(g_frame_zbuf, g_poly_framebuf, TEST_POLY_SIZE);
        if (filter == POLY_TEX_FILTER_HORIZONTAL)
            memcpy(horizontal, g_frame_zbuf, TEST_POLY_SIZE);
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

    /* And the filter does something here, both its axes, or the checks above
       prove nothing. */
    {
        int vertical = 0;
        for (int i = 0; i < TEST_POLY_SIZE; i++) {
            differ += unfiltered[i] != g_frame_zbuf[i];
            vertical += horizontal[i] != g_frame_zbuf[i];
        }
        ASSERT_TRUE(vertical > 100);
    }
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
    /* The sea's own filler, which its refill draws through, dithered or not. */
    check_redraw(POLY_TEXTURE_Z_FOG, "perspective fog");
    PolyFogDither = 1;
    check_redraw(POLY_TEXTURE_Z_FOG, "perspective fog, dithered");
    PolyFogDither = 0;
}

int main(void) {
    RUN_TEST(test_off_is_the_original_band);
    RUN_TEST(test_the_dither_lands_where_the_pixel_is);
    RUN_TEST(test_each_span_takes_its_own_band);
    RUN_TEST(test_a_band_middle_is_flat_with_the_dither);
    RUN_TEST(test_the_last_band_stays);
    RUN_TEST(test_the_filter_follows_the_dithered_band);
    RUN_TEST(test_the_redraw_keeps_the_filter);
    TEST_SUMMARY();
    return test_failures != 0;
}
