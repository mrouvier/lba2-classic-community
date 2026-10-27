/* Host test for the moving sea's arithmetic (SOURCES/SEA_FX.H).
 *
 * The sea's quads span the page once, their corners' texture coordinates
 * 0..127*256+255 in 8.8 fixed point, and the fillers wrap the page with
 * RepMask. The current adds one offset to every corner; the chop composes a
 * finer copy of the page into it. What has to hold:
 *
 *   - the offset stays within one page, so a corner plus the offset fits the
 *     point's 16 bits and never wraps between two corners of a quad, which
 *     would turn the texture around inside it;
 *   - the offset moves at the current's speed and wraps by exactly a page,
 *     which RepMask cannot tell from no wrap at all;
 *   - the chop's phase steps one finer texel at a time, against the current,
 *     and its finer texel stays in the page's sea quadrant;
 *   - the chop keeps each texel in its colour ramp, the fog CLUT's unit, and
 *     a finer texel at the page's mean step leaves the texel as it is. */
#include "SEA_FX.H"

#include "test_harness.h"

static void test_the_offset_stays_in_a_page(void) {
    const U32 times[] = {0u, 1u, 999u, 1000u, 42666u, 42667u, 3600000u, 0x7FFFFFFFu, 0xFFFFFFFFu};
    for (unsigned i = 0; i < sizeof(times) / sizeof(times[0]); i++) {
        const S32 u = SeaFx_Origin(times[i], SEA_FX_DRIFT_U);
        const S32 v = SeaFx_Origin(times[i], SEA_FX_DRIFT_V);
        ASSERT_TRUE(u >= 0 && u < SEA_FX_UV_WRAP);
        ASSERT_TRUE(v >= 0 && v < SEA_FX_UV_WRAP);
    }
}

static void test_the_offset_moves_at_the_current(void) {
    ASSERT_EQ_INT(0, SeaFx_Origin(0, SEA_FX_DRIFT_U));
    /* On whole steps: an eighth of a texel, reached after 1000 * 32 / 768 ms. */
    ASSERT_EQ_INT(0, SeaFx_Origin(41, SEA_FX_DRIFT_U));
    ASSERT_EQ_INT(SEA_FX_STEP, SeaFx_Origin(42, SEA_FX_DRIFT_U));
    for (U32 t = 0; t < 200000; t += 997)
        ASSERT_EQ_INT(0, SeaFx_Origin(t, SEA_FX_DRIFT_V) % SEA_FX_STEP);
    ASSERT_EQ_INT(SEA_FX_DRIFT_U, SeaFx_Origin(1000, SEA_FX_DRIFT_U));
    ASSERT_EQ_INT(SEA_FX_DRIFT_V * 3, SeaFx_Origin(3000, SEA_FX_DRIFT_V));
    /* 128 texels, then the page again from the start: 128 * 256 / 768 s. */
    ASSERT_EQ_INT(SeaFx_Origin(5000, SEA_FX_DRIFT_U), SeaFx_Origin(5000 + 128000, SEA_FX_DRIFT_U) - 0);
    /* A step is a fraction of a texel. */
    ASSERT_TRUE(SEA_FX_STEP < 256 && (SEA_FX_UV_WRAP % SEA_FX_STEP) == 0);
}

static void test_a_shifted_corner_fits(void) {
    ASSERT_EQ_UINT(0u, SeaFx_Shift(0, 0));
    ASSERT_EQ_UINT(65503u, SeaFx_Shift(127 * 256 + 255, SEA_FX_UV_WRAP - SEA_FX_STEP));
    /* A quad's corners keep their order whatever the offset. */
    for (S32 o = 0; o < SEA_FX_UV_WRAP; o += 1021)
        ASSERT_TRUE(SeaFx_Shift(0, o) < SeaFx_Shift(127 * 256 + 255, o));
}

static void test_the_chop_steps_against_the_current(void) {
    ASSERT_EQ_INT(0, SeaFx_ChopPhase(0));
    ASSERT_EQ_INT(0, SeaFx_ChopPhase(SEA_FX_CHOP_MS - 1));
    ASSERT_EQ_INT(SEA_FX_PAGE - 1, SeaFx_ChopPhase(SEA_FX_CHOP_MS));
    ASSERT_EQ_INT(SEA_FX_PAGE - 2, SeaFx_ChopPhase(2 * SEA_FX_CHOP_MS));
    ASSERT_EQ_INT(0, SeaFx_ChopPhase(SEA_FX_PAGE * SEA_FX_CHOP_MS));
    for (U32 t = 0; t < 100000; t += 777) {
        const S32 p = SeaFx_ChopPhase(t);
        ASSERT_TRUE(p >= 0 && p < SEA_FX_PAGE);
    }
}

/* The finer texel is in the page's sea quadrant, the first 128 bytes of a
   row, whatever the texel and the phase, and the map turns the page rather
   than lining it up: neighbours along a row are not neighbours along a row. */
static void test_the_fine_texel_stays_in_the_page(void) {
    for (S32 p = 0; p < SEA_FX_PAGE; p += 7)
        for (S32 v = 0; v < SEA_FX_PAGE; v++)
            for (S32 u = 0; u < SEA_FX_PAGE; u++) {
                const S32 o = SeaFx_FineTexel(u, v, p);
                ASSERT_TRUE(o >= 0 && o < SEA_FX_PAGE * 256 && (o & 255) < SEA_FX_PAGE);
            }
    /* (2u + v, 2v - u): one step along a row is two across and one back down. */
    ASSERT_EQ_INT(127 * 256 + 2, SeaFx_FineTexel(1, 0, 0));
    ASSERT_EQ_INT(2 * 256 + 1, SeaFx_FineTexel(0, 1, 0));
    ASSERT_EQ_INT(5 * 256 + 5, SeaFx_FineTexel(0, 0, 5));
}

static void test_the_chop_keeps_the_ramp(void) {
    for (S32 m = 0; m < 16; m++) {
        for (S32 a = 0; a < 256; a++) {
            for (S32 b = 0; b < 256; b += 3) {
                const U8 c = SeaFx_Mix((U8)a, (U8)b, m);
                ASSERT_EQ_INT(a & 0xF0, c & 0xF0);
            }
        }
    }
}

static void test_the_mean_leaves_a_texel(void) {
    for (S32 a = 0; a < 256; a++)
        ASSERT_EQ_INT(a, SeaFx_Mix((U8)a, (U8)(0x30 | 8), 8));
    /* Brighter and darker by the finer texel's distance from the mean. */
    ASSERT_EQ_INT(0x59, SeaFx_Mix(0x57, 0x0A, 8));
    ASSERT_EQ_INT(0x55, SeaFx_Mix(0x57, 0x06, 8));
    /* Clamped inside the ramp. */
    ASSERT_EQ_INT(0x5F, SeaFx_Mix(0x5E, 0x0F, 8));
    ASSERT_EQ_INT(0x50, SeaFx_Mix(0x51, 0x00, 8));
}

static void test_the_mean_step(void) {
    static U8 page[SEA_FX_PAGE * 256];
    for (S32 v = 0; v < SEA_FX_PAGE; v++)
        for (S32 u = 0; u < 256; u++)
            page[v * 256 + u] = (U8)(0xA0 | ((u < SEA_FX_PAGE) ? ((u & 1) ? 9 : 6) : 15));
    /* The second half of each row, the sky's, is not read. */
    ASSERT_EQ_INT(8, SeaFx_MeanStep(page, 256)); /* 7.5, rounded */
    for (S32 i = 0; i < SEA_FX_PAGE * 256; i++)
        page[i] = 0x47;
    ASSERT_EQ_INT(7, SeaFx_MeanStep(page, 256));
}

/* A background object over the sea: the pixels it drew are noted when it is
   baked, the refill paints the sea over everything the depths call sea, and
   the noted pixels take the background's colour again, nothing else. */
static void test_a_baked_object_survives_the_refill(void) {
    enum { W = 12,
           H = 8 };
    U8 log[W * H], screen[W * H], keep[W * H];
    S32 i, x, y;

    for (i = 0; i < W * H; i++) {
        screen[i] = 0x40;
        log[i] = 0x40;
        keep[i] = 0;
    }
    /* The object, drawn in the frame over a 3x2 patch of sea. */
    for (y = 3; y <= 4; y++)
        for (x = 5; x <= 7; x++)
            log[y * W + x] = 0x90;
    /* Baked: noted over the object's clip box, which is wider than it. */
    SeaFx_NoteDrawn(log, screen, keep, W, 4, 2, 8, 5);
    for (i = 0; i < W * H; i++)
        screen[i] = log[i]; /* CopyBlock, the whole frame here */

    /* A refill paints sea everywhere, the object's pixels included. */
    for (i = 0; i < W * H; i++)
        log[i] = 0x41;
    SeaFx_PutBack(log, screen, keep, W, 0, 0, W - 1, H - 1);

    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++)
            ASSERT_EQ_INT((x >= 5 && x <= 7 && y >= 3 && y <= 4) ? 0x90 : 0x41, log[y * W + x]);
    /* Noted only where the object drew, not over its whole box. */
    ASSERT_EQ_INT(0, keep[2 * W + 4]);
    ASSERT_EQ_INT(1, keep[3 * W + 5]);
}

int main(void) {
    RUN_TEST(test_the_offset_stays_in_a_page);
    RUN_TEST(test_the_offset_moves_at_the_current);
    RUN_TEST(test_a_shifted_corner_fits);
    RUN_TEST(test_the_chop_steps_against_the_current);
    RUN_TEST(test_the_fine_texel_stays_in_the_page);
    RUN_TEST(test_the_chop_keeps_the_ramp);
    RUN_TEST(test_the_mean_leaves_a_texel);
    RUN_TEST(test_the_mean_step);
    RUN_TEST(test_a_baked_object_survives_the_refill);
    TEST_SUMMARY();
    return test_failures != 0;
}
