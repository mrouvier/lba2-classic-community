/* Host test for the retail-fix rows' arithmetic (SOURCES/RETAILFIX.H): what a fix
 * site gets from the master row and the fix's own row, and the options menu's
 * cycle. Both are inline over plain values, so nothing here boots the engine. */
#include "RETAILFIX.H"
#include "test_harness.h"

/* No and Yes decide alone; the fix's own row is ignored under them. */
static void test_yes_and_no_override_the_fix(void) {
    ASSERT_EQ_INT(0, RetailFix_Effective(RETAIL_FIXES_NO, 1));
    ASSERT_EQ_INT(0, RetailFix_Effective(RETAIL_FIXES_NO, 0));
    ASSERT_EQ_INT(1, RetailFix_Effective(RETAIL_FIXES_YES, 1));
    ASSERT_EQ_INT(1, RetailFix_Effective(RETAIL_FIXES_YES, 0));
}

/* Custom hands the decision to the fix's own row. */
static void test_custom_follows_the_fix(void) {
    ASSERT_EQ_INT(1, RetailFix_Effective(RETAIL_FIXES_CUSTOM, 1));
    ASSERT_EQ_INT(0, RetailFix_Effective(RETAIL_FIXES_CUSTOM, 0));
    ASSERT_EQ_INT(1, RetailFix_Effective(RETAIL_FIXES_CUSTOM, 5));
}

/* Yes, No, back to Yes while no fix is off its default: Custom would equal Yes. */
static void test_the_menu_skips_custom_when_it_means_nothing(void) {
    ASSERT_EQ_INT(RETAIL_FIXES_NO, RetailFix_NextMenuValue(RETAIL_FIXES_YES, 0));
    ASSERT_EQ_INT(RETAIL_FIXES_YES, RetailFix_NextMenuValue(RETAIL_FIXES_NO, 0));
}

/* With a fix changed, the cycle passes through Custom and on to Yes. */
static void test_the_menu_offers_custom_once_a_fix_is_changed(void) {
    ASSERT_EQ_INT(RETAIL_FIXES_NO, RetailFix_NextMenuValue(RETAIL_FIXES_YES, 1));
    ASSERT_EQ_INT(RETAIL_FIXES_CUSTOM, RetailFix_NextMenuValue(RETAIL_FIXES_NO, 1));
    ASSERT_EQ_INT(RETAIL_FIXES_YES, RetailFix_NextMenuValue(RETAIL_FIXES_CUSTOM, 1));
    ASSERT_EQ_INT(RETAIL_FIXES_YES, RetailFix_NextMenuValue(RETAIL_FIXES_CUSTOM, 0));
}

/* The lightning strobe toggles on every call with the fix off, and once per
 * 50 ms with it on, from the time of the last toggle. */
static void test_the_lightning_strobe_is_paced_by_the_fix(void) {
    U32 last = 0;
    ASSERT_EQ_INT(1, RetailFix_LightningToggle(0, 1000, &last));
    ASSERT_EQ_INT(1, RetailFix_LightningToggle(0, 1016, &last));
    ASSERT_EQ_INT(1, RetailFix_LightningToggle(1, 2000, &last));
    ASSERT_EQ_INT(0, RetailFix_LightningToggle(1, 2016, &last));
    ASSERT_EQ_INT(0, RetailFix_LightningToggle(1, 2049, &last));
    ASSERT_EQ_INT(1, RetailFix_LightningToggle(1, 2050, &last));
    ASSERT_EQ_INT((int)2050, (int)last);
}

int main(void) {
    RUN_TEST(test_yes_and_no_override_the_fix);
    RUN_TEST(test_custom_follows_the_fix);
    RUN_TEST(test_the_menu_skips_custom_when_it_means_nothing);
    RUN_TEST(test_the_menu_offers_custom_once_a_fix_is_changed);
    RUN_TEST(test_the_lightning_strobe_is_paced_by_the_fix);
    TEST_SUMMARY();
    return test_failures != 0;
}
