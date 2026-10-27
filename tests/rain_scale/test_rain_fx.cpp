/* Host test for the storm look's arithmetic (SOURCES/RAIN_FX.H): the wind,
 * the drift it gives GereRain, and the blend strength and width a streak gets
 * by depth. All inline over plain values, so nothing here boots the engine. */
#include "RAIN_FX.H"
#include "test_harness.h"

/* Off is the 1997 drift, half the delta, whatever the wind says. */
static void test_off_is_the_1997_drift(void) {
    ASSERT_EQ_INT(80, RainFx_Drift(0, 160, 256));
    ASSERT_EQ_INT(80, RainFx_Drift(0, 160, 500));
    ASSERT_EQ_INT(0, RainFx_Drift(0, 0, 500));
}

/* On, a wind of 256 is the 1997 drift and the wind scales it. */
static void test_the_wind_scales_the_drift(void) {
    ASSERT_EQ_INT(80, RainFx_Drift(1, 160, 256));
    ASSERT_EQ_INT(160, RainFx_Drift(1, 160, 512));
    ASSERT_EQ_INT(40, RainFx_Drift(1, 160, 128));
}

/* The wind stays within its band, sways over five seconds and gusts every
 * 7.3 seconds; it is periodic and integer, so every platform agrees. */
static void test_the_wind_is_bounded_and_periodic(void) {
    S32 t, lo = 100000, hi = -100000;
    for (t = 0; t < 365000; t++) {
        const S32 w = RainFx_Wind(t);
        if (w < lo)
            lo = w;
        if (w > hi)
            hi = w;
    }
    ASSERT_EQ_INT(166, lo);                                       /* 256 - 90 at the sway's trough, no gust */
    ASSERT_EQ_INT(1, hi <= 256 + 90 + 170);                       /* never past sway plus gust */
    ASSERT_EQ_INT(1, hi >= 256 + 170 - 90);                       /* a gust peak lands inside every sway */
    ASSERT_EQ_INT(RainFx_Wind(1234), RainFx_Wind(1234 + 365000)); /* 5000 and 7300 share 365000 */
    ASSERT_EQ_INT(RainFx_Wind(1234), RainFx_Wind(1234 - 365000)); /* a negative time wraps the same way */
}

/* A gust rises and falls over 1.5 s, peaking at +170 at its middle. */
static void test_a_gust_peaks_at_its_middle(void) {
    ASSERT_EQ_INT(170, RainFx_Wind(750) - RainFx_Wind(5750)); /* same sway phase, 5750 is past the gust */
    ASSERT_EQ_INT(1, RainFx_Wind(750) > RainFx_Wind(100));
    ASSERT_EQ_INT(1, RainFx_Wind(750) > RainFx_Wind(1400));
    ASSERT_EQ_INT(RainFx_Wind(0), RainFx_Wind(5000)); /* no gust at either, same sway */
}

/* A near streak may be drawn as it is and gains a column; a far one stays at
 * 75%, and no pixel goes below the 50% the palette can still show. */
static void test_the_streak_fades_with_depth(void) {
    ASSERT_EQ_INT(4, RainFx_Strength(0));
    ASSERT_EQ_INT(4, RainFx_Strength(11999));
    ASSERT_EQ_INT(3, RainFx_Strength(12000));
    ASSERT_EQ_INT(3, RainFx_Strength(65535));
    ASSERT_EQ_INT(1, RainFx_ExtraWidth(0));
    ASSERT_EQ_INT(0, RainFx_ExtraWidth(12000));
}

/* Along a streak the tail is soft and the head solid, within the depth's cap. */
static void test_the_streak_is_soft_at_the_tail(void) {
    ASSERT_EQ_INT(2, RainFx_PixelStrength(0, 20, 4));  /* tail quarter: 50% */
    ASSERT_EQ_INT(3, RainFx_PixelStrength(5, 20, 4));  /* next quarter: 75% */
    ASSERT_EQ_INT(4, RainFx_PixelStrength(10, 20, 4)); /* head half: as it is */
    ASSERT_EQ_INT(4, RainFx_PixelStrength(20, 20, 4));
    ASSERT_EQ_INT(3, RainFx_PixelStrength(20, 20, 3)); /* a far streak never solid */
    ASSERT_EQ_INT(2, RainFx_PixelStrength(0, 20, 3));
    ASSERT_EQ_INT(4, RainFx_PixelStrength(0, 0, 4)); /* a one-pixel streak is its head */
}

/* The streak's grey steps three entries up its ramp and stops at the ramp's top. */
static void test_the_streak_is_brightened_within_its_ramp(void) {
    ASSERT_EQ_INT(54, RainFx_Brighten(51));
    ASSERT_EQ_INT(61, RainFx_Brighten(58));
    ASSERT_EQ_INT(63, RainFx_Brighten(62));
    ASSERT_EQ_INT(63, RainFx_Brighten(63));
}

/* The streak is the 1997 delta off, twice it on, and leans with the wind. */
static void test_the_streak_lengthens_and_leans(void) {
    ASSERT_EQ_INT(128, RainFx_StreakX(0, 128, 500));
    ASSERT_EQ_INT(256, RainFx_StreakYZ(0, 256));
    ASSERT_EQ_INT(256, RainFx_StreakX(1, 128, 256));
    ASSERT_EQ_INT(512, RainFx_StreakX(1, 128, 512));
    ASSERT_EQ_INT(512, RainFx_StreakYZ(1, 256));
}

/* The look runs a third more drops than the scale gives, by the intensity,
 * within the pool. */
static void test_the_look_adds_a_third_within_the_pool(void) {
    ASSERT_EQ_INT(200, RainFx_Count(0, 200, 256, 1350));
    ASSERT_EQ_INT(200, RainFx_Count(0, 200, 100, 1350));
    ASSERT_EQ_INT(266, RainFx_Count(1, 200, 256, 1350));
    ASSERT_EQ_INT(1350, RainFx_Count(1, 1350, 256, 1350));
    ASSERT_EQ_INT(177, RainFx_Count(1, 200, 171, 1350)); /* the lull's bottom */
    ASSERT_EQ_INT(1, RainFx_Count(1, 1, 171, 1350));
    ASSERT_EQ_INT(1, RainFx_NearDrop(1, 0));
    ASSERT_EQ_INT(0, RainFx_NearDrop(1, 1));
    ASSERT_EQ_INT(1, RainFx_NearDrop(1, 4));
    ASSERT_EQ_INT(0, RainFx_NearDrop(0, 0));
}

/* The near layer is as wide as the view at its depth: 1920 of fixed focal is
 * 1.6 times the depth either side, a locked 16:9 view 0.71. */
static void test_the_near_layer_is_as_wide_as_the_view(void) {
    ASSERT_EQ_INT(800 + 16000, RainFx_NearHalfWidth(10000, 1920));
    ASSERT_EQ_INT(800 + 7108, RainFx_NearHalfWidth(10000, 853));
    ASSERT_EQ_INT(800, RainFx_NearHalfWidth(0, 1920));
}

/* The ripple starts as wide as the V and triples over the splash's 80 ms. */
static void test_the_ripple_grows_with_the_splash(void) {
    ASSERT_EQ_INT(5, RainFx_RippleRadius(4, 0));
    ASSERT_EQ_INT(9, RainFx_RippleRadius(4, 40));
    ASSERT_EQ_INT(13, RainFx_RippleRadius(4, 80));
    ASSERT_EQ_INT(13, RainFx_RippleRadius(4, 200)); /* clamped to the life */
    ASSERT_EQ_INT(2, RainFx_RippleRadius(1, 0));
}

/* A strike gusts to +200 at its middle and is gone after 1.5 s; the storm
 * lulls to two thirds over 45 s and bursts a third after a strike. */
static void test_lightning_gusts_and_the_storm_lulls(void) {
    ASSERT_EQ_INT(0, RainFx_FlashGust(-1));
    ASSERT_EQ_INT(200, RainFx_FlashGust(750));
    ASSERT_EQ_INT(0, RainFx_FlashGust(1500));
    ASSERT_EQ_INT(256, RainFx_Intensity(0, -1));
    ASSERT_EQ_INT(171, RainFx_Intensity(22500, -1));
    ASSERT_EQ_INT(256, RainFx_Intensity(45000, -1));
    ASSERT_EQ_INT(256 + 85, RainFx_Intensity(0, 750));
    ASSERT_EQ_INT(RainFx_Intensity(1234, -1), RainFx_Intensity(1234 - 45000, -1));
}

/* The depth ramp is LineRain's: a wrapped start and a sign-extended 16-bit
 * step, so a streak straddling the far clip ramps past 65535 or below 0 and
 * stays hidden by everything but the sky, as the ASM's does. */
static void test_the_depth_ramp_is_the_asm_ones(void) {
    ASSERT_EQ_INT(65400, RainFx_DepthStart(65400));
    ASSERT_EQ_INT(164, RainFx_DepthStart(65700));
    ASSERT_EQ_INT(300, RainFx_DepthDelta(65400, 65700));
    ASSERT_EQ_INT(65700, RainFx_DepthStart(65400) + RainFx_DepthDelta(65400, 65700));
    ASSERT_EQ_INT(-546, RainFx_DepthDelta(10, 65000));
    ASSERT_EQ_INT(-6000, RainFx_DepthDelta(66000, 60000));
    ASSERT_EQ_INT(100, RainFx_DepthDelta(100, 200));
}

/* A line and its mirror draw the same pixels and both reach their end. */
static void test_the_dda_is_symmetric(void) {
    ASSERT_EQ_INT(30, RainFx_Dda(30, 30, 30));
    ASSERT_EQ_INT(-30, RainFx_Dda(-30, 30, 30));
    ASSERT_EQ_INT(1, RainFx_Dda(30, 1, 30));
    ASSERT_EQ_INT(-1, RainFx_Dda(-30, 1, 30));
    ASSERT_EQ_INT(0, RainFx_Dda(-7, 0, 30));
    ASSERT_EQ_INT(-7, RainFx_Dda(-7, 30, 30));
    ASSERT_EQ_INT(2, RainFx_Dda(7, 10, 30)); /* 2.33 */
    ASSERT_EQ_INT(-2, RainFx_Dda(-7, 10, 30));
}

/* With the look on RAIN.CPP draws no columns of its own, and a splash box
 * reaches the columns the streak adds past the V's right arm. */
static void test_the_look_owns_the_columns(void) {
    ASSERT_EQ_INT(2, RainFx_ColumnWidth(0, 2));
    ASSERT_EQ_INT(1, RainFx_ColumnWidth(1, 2));
    ASSERT_EQ_INT(100, RainFx_SplashRight(0, 100, 2, 0));
    ASSERT_EQ_INT(102, RainFx_SplashRight(1, 100, 2, 0));     /* near: 2 + 1 columns */
    ASSERT_EQ_INT(101, RainFx_SplashRight(1, 100, 2, 30000)); /* far: 2 columns */
}

int main(void) {
    RUN_TEST(test_off_is_the_1997_drift);
    RUN_TEST(test_the_wind_scales_the_drift);
    RUN_TEST(test_the_wind_is_bounded_and_periodic);
    RUN_TEST(test_a_gust_peaks_at_its_middle);
    RUN_TEST(test_the_streak_fades_with_depth);
    RUN_TEST(test_the_streak_is_soft_at_the_tail);
    RUN_TEST(test_the_streak_is_brightened_within_its_ramp);
    RUN_TEST(test_the_streak_lengthens_and_leans);
    RUN_TEST(test_the_look_adds_a_third_within_the_pool);
    RUN_TEST(test_the_near_layer_is_as_wide_as_the_view);
    RUN_TEST(test_the_ripple_grows_with_the_splash);
    RUN_TEST(test_lightning_gusts_and_the_storm_lulls);
    RUN_TEST(test_the_depth_ramp_is_the_asm_ones);
    RUN_TEST(test_the_dda_is_symmetric);
    RUN_TEST(test_the_look_owns_the_columns);
    TEST_SUMMARY();
    return test_failures != 0;
}
