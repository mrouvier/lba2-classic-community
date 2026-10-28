/* Host test for the fog dither's band pick, PolyTZF_FogDitherRow
 * (LIB386/H/POLYGON/POLYTZF.H).
 *
 * The perspective fog fillers draw the sea and the sky with one fog band a
 * span, the top four bits of the span's fog value; the four bits under it say
 * how far into its band the span is. The dither, centred on the band, keeps a
 * band's middle as it is and mixes the two bands either side of each edge.
 * The properties that make it a gradient rather than noise, and keep the sea's
 * tone the original's, each fail in a way a screenshot shows only as the wrong
 * kind of texture:
 *
 *   - a span in the middle of its band draws the original's band everywhere;
 *   - a span f/16 into its band (f from 8) promotes f - 8 of the 16 pixels of
 *     every 4x4 tile to the next band, and one f/16 in (f under 8) leaves
 *     8 - f of them on the band before, so the share moves linearly across
 *     the edge and is half and half on it;
 *   - the pixels taking the deeper band at one value take it at every deeper
 *     value, so a span drifting deeper adds pixels rather than reshuffling them
 *     (the camera's slightest move would otherwise make the sea crawl);
 *   - band 15 is never promoted, since the fog CLUT has no band after it;
 *   - only the low 16 bits count, as only those count in the original.
 *
 * Pure arithmetic in a header, so it runs in the host suite. */
#include <POLYGON/POLYTZF.H>

#include "test_harness.h"

/* How many of a 4x4 tile's pixels take band `band` for a raw value. */
static int InTile(U32 raw, U32 band) {
    int n = 0;
    for (U32 y = 0; y < 4; y++)
        for (U32 x = 0; x < 4; x++)
            n += PolyTZF_FogDitherRow(raw, x, y) == band;
    return n;
}

static void test_a_band_middle_is_the_original(void) {
    for (U32 band = 0; band < 16; band++)
        for (U32 y = 0; y < 8; y++)
            for (U32 x = 0; x < 8; x++)
                ASSERT_EQ_UINT(band << 12, PolyTZF_FogDitherRow((band << 12) | 0x8FF, x, y));
}

static void test_the_share_moves_across_the_edge(void) {
    for (U32 band = 1; band < 15; band++) {
        for (U32 f = 0; f < 16; f++) {
            const U32 raw = (band << 12) | (f << 8) | 0x5A; /* low bits are below the fraction */
            if (f >= 8) {
                ASSERT_EQ_INT((int)(f - 8), InTile(raw, (band + 1) << 12));
                ASSERT_EQ_INT((int)(24 - f), InTile(raw, band << 12));
            } else {
                ASSERT_EQ_INT((int)(8 - f), InTile(raw, (band - 1) << 12));
                ASSERT_EQ_INT((int)(8 + f), InTile(raw, band << 12));
            }
        }
    }
    /* On the edge itself, half and half. */
    ASSERT_EQ_INT(8, InTile(0x7000, 0x6000));
    ASSERT_EQ_INT(8, InTile(0x7000, 0x7000));
}

static void test_deeper_adds_pixels(void) {
    for (U32 raw = 0x2000; raw < 0x5000; raw += 0x100) {
        for (U32 y = 0; y < 4; y++) {
            for (U32 x = 0; x < 4; x++) {
                const U32 at = PolyTZF_FogDitherRow(raw, x, y);
                const U32 next = PolyTZF_FogDitherRow(raw + 0x100, x, y);
                ASSERT_TRUE(next >= at && next - at <= 0x1000);
            }
        }
    }
}

static void test_the_last_band_stays(void) {
    for (U32 f = 8; f < 16; f++)
        for (U32 y = 0; y < 4; y++)
            for (U32 x = 0; x < 4; x++)
                ASSERT_EQ_UINT(0xF000u, PolyTZF_FogDitherRow(0xF000 | (f << 8), x, y));
    ASSERT_EQ_INT(16, InTile(0xFFFF, 0xF000));
}

static void test_the_near_edge_stays(void) {
    /* Before the fog starts nothing is promoted, and half a band in is still
       band 0: there is no band before it to mix with. */
    for (U32 raw = 0; raw < 0x900; raw += 0x80)
        ASSERT_EQ_INT(16, InTile(raw, 0));
}

static void test_only_the_low_bits_count(void) {
    for (U32 y = 0; y < 4; y++)
        for (U32 x = 0; x < 4; x++) {
            ASSERT_EQ_UINT(PolyTZF_FogDitherRow(0x2A00, x, y), PolyTZF_FogDitherRow(0x12A00, x, y));
            ASSERT_EQ_UINT(PolyTZF_FogDitherRow(0x0300, x, y), PolyTZF_FogDitherRow(0x30300, x, y));
        }
}

/* The pattern repeats every four pixels both ways. */
static void test_the_tile_repeats(void) {
    for (U32 f = 0; f < 16; f++) {
        const U32 raw = 0x6000 | (f << 8);
        for (U32 y = 0; y < 4; y++)
            for (U32 x = 0; x < 4; x++)
                ASSERT_EQ_UINT(PolyTZF_FogDitherRow(raw, x, y), PolyTZF_FogDitherRow(raw, x + 1916, y + 1076));
    }
}

int main(void) {
    RUN_TEST(test_a_band_middle_is_the_original);
    RUN_TEST(test_the_share_moves_across_the_edge);
    RUN_TEST(test_deeper_adds_pixels);
    RUN_TEST(test_the_last_band_stays);
    RUN_TEST(test_the_near_edge_stays);
    RUN_TEST(test_only_the_low_bits_count);
    RUN_TEST(test_the_tile_repeats);
    TEST_SUMMARY();
    return test_failures != 0;
}
