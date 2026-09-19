#include <unity.h>
#include "step_brightness.h"
#include "../mocks/mock_display.h"

static void test_initial_level_is_50_percent() {
    MockDisplay d;
    StepBrightness b(d);
    TEST_ASSERT_EQUAL_UINT8(4, b.getCurrentLevel());
    TEST_ASSERT_EQUAL_UINT8(50, b.getCurrentPercent());
}

static void test_applyCurrent_pushes_initial_percent() {
    MockDisplay d;
    StepBrightness b(d);
    b.applyCurrent();
    TEST_ASSERT_EQUAL_UINT8(50, d.last_percent);
    TEST_ASSERT_EQUAL_INT(1, d.call_count);
}

static void test_increase_steps_up_one_level() {
    MockDisplay d;
    StepBrightness b(d);
    b.increase();
    TEST_ASSERT_EQUAL_UINT8(60, b.getCurrentPercent());
    TEST_ASSERT_EQUAL_UINT8(60, d.last_percent);
}

static void test_decrease_steps_down_one_level() {
    MockDisplay d;
    StepBrightness b(d);
    b.decrease();
    TEST_ASSERT_EQUAL_UINT8(40, b.getCurrentPercent());
    TEST_ASSERT_EQUAL_UINT8(40, d.last_percent);
}

static void test_increase_clamps_at_100_percent() {
    MockDisplay d;
    StepBrightness b(d);
    for (int i = 0; i < 10; ++i) b.increase();
    TEST_ASSERT_EQUAL_UINT8(100, b.getCurrentPercent());
}

static void test_decrease_clamps_at_10_percent() {
    MockDisplay d;
    StepBrightness b(d);
    for (int i = 0; i < 10; ++i) b.decrease();
    TEST_ASSERT_EQUAL_UINT8(10, b.getCurrentPercent());
}

// A clamped press must not reach the display: the +/- pair would otherwise
// re-send the same duty on every tap at the ends of the range.
static void test_increase_at_ceiling_does_not_touch_display() {
    MockDisplay d;
    StepBrightness b(d);
    for (int i = 0; i < 6; ++i) b.increase();  // 50% -> 100%, five real steps
    const int calls_at_ceiling = d.call_count;
    b.increase();
    TEST_ASSERT_EQUAL_INT(calls_at_ceiling, d.call_count);
}

static void test_decrease_at_floor_does_not_touch_display() {
    MockDisplay d;
    StepBrightness b(d);
    for (int i = 0; i < 5; ++i) b.decrease();  // 50% -> 10%, four real steps
    const int calls_at_floor = d.call_count;
    b.decrease();
    TEST_ASSERT_EQUAL_INT(calls_at_floor, d.call_count);
}

static void test_increase_then_decrease_returns_to_start() {
    MockDisplay d;
    StepBrightness b(d);
    b.increase();
    b.decrease();
    TEST_ASSERT_EQUAL_UINT8(50, b.getCurrentPercent());
}

// next() keeps its wrapping cycle — main_display drives it from one button.
static void test_next_wraps_from_100_to_10() {
    MockDisplay d;
    StepBrightness b(d);
    for (int i = 0; i < 5; ++i) b.next();  // 50% -> 100%
    TEST_ASSERT_EQUAL_UINT8(100, b.getCurrentPercent());
    b.next();
    TEST_ASSERT_EQUAL_UINT8(10, b.getCurrentPercent());
}

void run_step_brightness_tests() {
    RUN_TEST(test_initial_level_is_50_percent);
    RUN_TEST(test_applyCurrent_pushes_initial_percent);
    RUN_TEST(test_increase_steps_up_one_level);
    RUN_TEST(test_decrease_steps_down_one_level);
    RUN_TEST(test_increase_clamps_at_100_percent);
    RUN_TEST(test_decrease_clamps_at_10_percent);
    RUN_TEST(test_increase_at_ceiling_does_not_touch_display);
    RUN_TEST(test_decrease_at_floor_does_not_touch_display);
    RUN_TEST(test_increase_then_decrease_returns_to_start);
    RUN_TEST(test_next_wraps_from_100_to_10);
}

#include "../../../lib/core/src/step_brightness.cpp"
