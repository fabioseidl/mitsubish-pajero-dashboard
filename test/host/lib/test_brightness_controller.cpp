#include <unity.h>
#include "brightness_controller.h"
#include "../mocks/mock_display.h"

static void test_initial_level_is_2() {
    MockDisplay d;
    BrightnessController ctrl(d);
    TEST_ASSERT_EQUAL_UINT8(2, ctrl.getCurrentLevel());
    TEST_ASSERT_EQUAL_UINT8(75, ctrl.getCurrentPercent());
}

static void test_onTouch_advances_level_from_2_to_3() {
    MockDisplay d;
    BrightnessController ctrl(d);
    ctrl.onTouch();
    TEST_ASSERT_EQUAL_UINT8(3, ctrl.getCurrentLevel());
    TEST_ASSERT_EQUAL_UINT8(100, ctrl.getCurrentPercent());
}

static void test_onTouch_wraps_from_level_3_to_0() {
    MockDisplay d;
    BrightnessController ctrl(d);
    ctrl.onTouch(); // 2->3
    ctrl.onTouch(); // 3->0
    TEST_ASSERT_EQUAL_UINT8(0, ctrl.getCurrentLevel());
    TEST_ASSERT_EQUAL_UINT8(25, ctrl.getCurrentPercent());
}

static void test_onTouch_calls_setBacklightPercent_on_display() {
    MockDisplay d;
    BrightnessController ctrl(d);
    ctrl.onTouch();
    TEST_ASSERT_EQUAL_UINT8(ctrl.getCurrentPercent(), d.last_percent);
    TEST_ASSERT_EQUAL_INT(1, d.call_count);
}

static void test_applyInitial_calls_setBacklightPercent_with_75() {
    MockDisplay d;
    BrightnessController ctrl(d);
    ctrl.applyInitial();
    TEST_ASSERT_EQUAL_UINT8(75, d.last_percent);
    TEST_ASSERT_EQUAL_INT(1, d.call_count);
}

static void test_four_touches_return_to_original_level() {
    MockDisplay d;
    BrightnessController ctrl(d);
    ctrl.onTouch();
    ctrl.onTouch();
    ctrl.onTouch();
    ctrl.onTouch();
    TEST_ASSERT_EQUAL_UINT8(2, ctrl.getCurrentLevel());
}

static void test_level_percents_are_25_50_75_100() {
    TEST_ASSERT_EQUAL_UINT8(25,  BrightnessController::LEVEL_PERCENTS[0]);
    TEST_ASSERT_EQUAL_UINT8(50,  BrightnessController::LEVEL_PERCENTS[1]);
    TEST_ASSERT_EQUAL_UINT8(75,  BrightnessController::LEVEL_PERCENTS[2]);
    TEST_ASSERT_EQUAL_UINT8(100, BrightnessController::LEVEL_PERCENTS[3]);
}

static void test_onTouch_does_not_call_begin_on_display() {
    MockDisplay d;
    BrightnessController ctrl(d);
    ctrl.onTouch();
    ctrl.onTouch();
    TEST_ASSERT_FALSE(d.begin_called);
}

static void test_onLdrReading_bright_sets_high_backlight() {
    // Full brightness ADC value should produce ~100% backlight after convergence
    MockDisplay d;
    BrightnessController ctrl(d);
    // Feed saturated bright reading many times so EMA fully converges
    for (int i = 0; i < 100; ++i) ctrl.onLdrReading(4095);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT8(90, d.last_percent);
}

static void test_onLdrReading_dark_sets_low_backlight() {
    // Full dark ADC value should produce ~BL_PCT_MIN backlight after convergence
    MockDisplay d;
    BrightnessController ctrl(d);
    for (int i = 0; i < 100; ++i) ctrl.onLdrReading(0);
    TEST_ASSERT_LESS_OR_EQUAL_UINT8(15, d.last_percent);
}

static void test_onLdrReading_calls_setBacklightPercent() {
    MockDisplay d;
    BrightnessController ctrl(d);
    int before = d.call_count;
    ctrl.onLdrReading(2048);
    TEST_ASSERT_GREATER_THAN_INT(before, d.call_count);
}

void run_brightness_controller_tests() {
    RUN_TEST(test_initial_level_is_2);
    RUN_TEST(test_onTouch_advances_level_from_2_to_3);
    RUN_TEST(test_onTouch_wraps_from_level_3_to_0);
    RUN_TEST(test_onTouch_calls_setBacklightPercent_on_display);
    RUN_TEST(test_applyInitial_calls_setBacklightPercent_with_75);
    RUN_TEST(test_four_touches_return_to_original_level);
    RUN_TEST(test_level_percents_are_25_50_75_100);
    RUN_TEST(test_onTouch_does_not_call_begin_on_display);
    RUN_TEST(test_onLdrReading_bright_sets_high_backlight);
    RUN_TEST(test_onLdrReading_dark_sets_low_backlight);
    RUN_TEST(test_onLdrReading_calls_setBacklightPercent);
}

#include "../../../lib/core/src/brightness_controller.cpp"
