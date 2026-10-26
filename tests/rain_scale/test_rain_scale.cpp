/* Host test for the storm's scale arithmetic (SOURCES/RAIN_SCALE.H): the drop
 * count, the box spans and the streak width RAIN.CPP runs for a framebuffer,
 * and the pool, which RAIN.CPP holds equal to COMMON.H's MAX_RAIN at compile
 * time. All inline over plain values, so nothing here boots the engine. */
#include "RAIN_SCALE.H"
#include "test_harness.h"

/* Off is the 1997 storm at any size: 200 drops, the 1997 box, one column wide. */
static void test_off_is_the_original_at_any_size(void) {
    ASSERT_EQ_INT(RAIN_DROPS, RainScale_Count(0, 640, 480, RAIN_POOL));
    ASSERT_EQ_INT(RAIN_DROPS, RainScale_Count(0, 1920, 1080, RAIN_POOL));
    ASSERT_EQ_INT(RAIN_DROPS, RainScale_Count(0, 320, 200, RAIN_POOL));
    ASSERT_EQ_INT(30000, RainScale_Span(0, 30000, 1920, RAIN_BASE_W));
    ASSERT_EQ_INT(10000, RainScale_Span(0, 10000, 1080, RAIN_BASE_H));
    ASSERT_EQ_INT(1, RainScale_Width(0, 480));
    ASSERT_EQ_INT(1, RainScale_Width(0, 1080));
}

/* On at 640x480 is the original too: the setting changes nothing there. */
static void test_on_at_the_base_frame_is_the_original(void) {
    ASSERT_EQ_INT(RAIN_DROPS, RainScale_Count(1, 640, 480, RAIN_POOL));
    ASSERT_EQ_INT(30000, RainScale_Span(1, 30000, 640, RAIN_BASE_W));
    ASSERT_EQ_INT(15000, RainScale_Span(1, 15000, 640, RAIN_BASE_W));
    ASSERT_EQ_INT(10000, RainScale_Span(1, 10000, 480, RAIN_BASE_H));
    ASSERT_EQ_INT(1, RainScale_Width(1, 480));
}

/* The box's sides follow the width over 640, its height the height over 480. */
static void test_the_box_follows_the_view(void) {
    ASSERT_EQ_INT(90000, RainScale_Span(1, 30000, 1920, RAIN_BASE_W));
    ASSERT_EQ_INT(45000, RainScale_Span(1, 15000, 1920, RAIN_BASE_W));
    ASSERT_EQ_INT(39750, RainScale_Span(1, 30000, 848, RAIN_BASE_W));
    ASSERT_EQ_INT(22500, RainScale_Span(1, 10000, 1080, RAIN_BASE_H));
    ASSERT_EQ_INT(15000, RainScale_Span(1, 10000, 720, RAIN_BASE_H));
}

/* The count follows the box: (w/640) squared times h/480, rounded to nearest,
 * so the drops stay as dense in the world as the 200 were in the 1997 box. */
static void test_the_count_follows_the_box(void) {
    ASSERT_EQ_INT(288, RainScale_Count(1, 768, 480, RAIN_POOL));    /* 1.44x, the Steam Deck render width */
    ASSERT_EQ_INT(351, RainScale_Count(1, 848, 480, RAIN_POOL));    /* 1.76x, the 16:9 auto-detect */
    ASSERT_EQ_INT(1200, RainScale_Count(1, 1280, 720, RAIN_POOL));  /* 6x */
    ASSERT_EQ_INT(1333, RainScale_Count(1, 1280, 800, RAIN_POOL));  /* 6.67x */
    ASSERT_EQ_INT(4050, RainScale_Count(1, 1920, 1080, RAIN_POOL)); /* 20.25x */
    ASSERT_EQ_INT(21, RainScale_Count(1, 320, 200, RAIN_POOL));     /* 0.1x: a smaller frame gets fewer */
}

/* The count never leaves [1, pool]. */
static void test_the_count_is_bounded(void) {
    ASSERT_EQ_INT(1, RainScale_Count(1, 8, 8, RAIN_POOL));
    ASSERT_EQ_INT(100, RainScale_Count(1, 1920, 1080, 100));
}

/* The pool is the largest frame's count, so no resolution the player can pick
 * is clamped; COMMON.H's MAX_RAIN carries the same number for TabRain. The
 * macro's largest term is inside S32 there, which the value shows. */
static void test_the_pool_holds_the_largest_frame(void) {
    ASSERT_EQ_INT(4050, RAIN_POOL);
    ASSERT_EQ_INT(RAIN_POOL, RainScale_Count(1, RES_MAX_W, RES_MAX_H, RAIN_POOL));
}

/* A streak is one column per 480 lines, rounded to nearest, never none. */
static void test_the_width_follows_the_height(void) {
    ASSERT_EQ_INT(1, RainScale_Width(1, 200));
    ASSERT_EQ_INT(1, RainScale_Width(1, 480));
    ASSERT_EQ_INT(1, RainScale_Width(1, 600));
    ASSERT_EQ_INT(2, RainScale_Width(1, 720));
    ASSERT_EQ_INT(2, RainScale_Width(1, 800));
    ASSERT_EQ_INT(2, RainScale_Width(1, 1080));
}

int main(void) {
    RUN_TEST(test_off_is_the_original_at_any_size);
    RUN_TEST(test_on_at_the_base_frame_is_the_original);
    RUN_TEST(test_the_box_follows_the_view);
    RUN_TEST(test_the_count_follows_the_box);
    RUN_TEST(test_the_count_is_bounded);
    RUN_TEST(test_the_pool_holds_the_largest_frame);
    RUN_TEST(test_the_width_follows_the_height);
    TEST_SUMMARY();
    return test_failures != 0;
}
