/* Host test for the exterior view lock's arithmetic (SOURCES/EXTFOV.H): the
 * focal and near clip Init3DExtView sets for a frame height, the view's size in
 * 480-line pixels the Auto camera and the storm reason with, and the classic
 * camera's off-screen margins. All inline over plain values, so nothing here
 * boots the engine. */
#include "EXTFOV.H"
#include "test_harness.h"

/* Off is the fixed focal at any height, and the near clip that moves in with
 * the height above 480 and stays put at 480 and below. */
static void test_off_is_the_fixed_focal(void) {
    ASSERT_EQ_INT(600, ExtFov_Focal(0, 600, 480));
    ASSERT_EQ_INT(600, ExtFov_Focal(0, 600, 1080));
    ASSERT_EQ_INT(600, ExtFov_Focal(0, 600, 200));
    ASSERT_EQ_INT(3000, ExtFov_NearClip(0, 3000, 480));
    ASSERT_EQ_INT(2000, ExtFov_NearClip(0, 3000, 720));
    ASSERT_EQ_INT(1333, ExtFov_NearClip(0, 3000, 1080));
    ASSERT_EQ_INT(3000, ExtFov_NearClip(0, 3000, 200));
    ASSERT_EQ_INT(1080, ExtFov_ViewHeight(0, 1080));
    ASSERT_EQ_INT(1920, ExtFov_ViewWidth(0, 1920, 1080));
    ASSERT_EQ_INT(80, ExtFov_Pixels(0, 80, 1080));
}

/* On at 480 lines is the original too: the setting changes nothing there. */
static void test_on_at_the_base_height_is_the_original(void) {
    ASSERT_EQ_INT(600, ExtFov_Focal(1, 600, 480));
    ASSERT_EQ_INT(3000, ExtFov_NearClip(1, 3000, 480));
    ASSERT_EQ_INT(480, ExtFov_ViewHeight(1, 480));
    ASSERT_EQ_INT(640, ExtFov_ViewWidth(1, 640, 480));
    ASSERT_EQ_INT(848, ExtFov_ViewWidth(1, 848, 480));
    ASSERT_EQ_INT(80, ExtFov_Pixels(1, 80, 480));
}

/* The focal follows the height, so the vertical view is the original's. */
static void test_the_focal_follows_the_height(void) {
    ASSERT_EQ_INT(900, ExtFov_Focal(1, 600, 720));
    ASSERT_EQ_INT(1000, ExtFov_Focal(1, 600, 800));
    ASSERT_EQ_INT(1350, ExtFov_Focal(1, 600, 1080));
    ASSERT_EQ_INT(250, ExtFov_Focal(1, 600, 200)); /* a shorter frame shows less, as the original would */
}

/* With the lock the frustum is the original's, and so is the near plane. */
static void test_the_near_clip_stays_put_under_the_lock(void) {
    ASSERT_EQ_INT(3000, ExtFov_NearClip(1, 3000, 720));
    ASSERT_EQ_INT(3000, ExtFov_NearClip(1, 3000, 1080));
    ASSERT_EQ_INT(3000, ExtFov_NearClip(1, 3000, 200));
}

/* The view in 480-line pixels: 480 tall, as wide as the aspect makes it. */
static void test_the_view_is_measured_in_480_line_pixels(void) {
    ASSERT_EQ_INT(480, ExtFov_ViewHeight(1, 1080));
    ASSERT_EQ_INT(853, ExtFov_ViewWidth(1, 1920, 1080)); /* 16:9 */
    ASSERT_EQ_INT(853, ExtFov_ViewWidth(1, 1280, 720));  /* 16:9 */
    ASSERT_EQ_INT(768, ExtFov_ViewWidth(1, 1280, 800));  /* 16:10, the Steam Deck */
    ASSERT_EQ_INT(640, ExtFov_ViewWidth(1, 1024, 768));  /* 4:3 */
    ASSERT_EQ_INT(1920, ExtFov_ViewWidth(1, 1920, 0));   /* no height yet: the frame as it is */
}

/* The classic camera's margins, 1997 pixels, in this frame's pixels. */
static void test_the_margins_follow_the_focal(void) {
    ASSERT_EQ_INT(180, ExtFov_Pixels(1, 80, 1080));
    ASSERT_EQ_INT(225, ExtFov_Pixels(1, 100, 1080));
    ASSERT_EQ_INT(112, ExtFov_Pixels(1, 50, 1080));
    ASSERT_EQ_INT(120, ExtFov_Pixels(1, 80, 720));
}

int main(void) {
    RUN_TEST(test_off_is_the_fixed_focal);
    RUN_TEST(test_on_at_the_base_height_is_the_original);
    RUN_TEST(test_the_focal_follows_the_height);
    RUN_TEST(test_the_near_clip_stays_put_under_the_lock);
    RUN_TEST(test_the_view_is_measured_in_480_line_pixels);
    RUN_TEST(test_the_margins_follow_the_focal);
    TEST_SUMMARY();
    return test_failures != 0;
}
