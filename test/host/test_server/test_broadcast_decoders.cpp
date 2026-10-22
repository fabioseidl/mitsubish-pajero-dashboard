// Shared decoders for the confirmed free-running broadcast frames (pid_map.h).
// The sniffer, the server and the emulator all read the frames through these, so
// a change here would silently desync them; these cases pin the byte layouts
// recorded in .claude/skills/can-reverse-engineering/SKILL.md.

#include <unity.h>
#include "pid_map.h"
#include "payload.h"

// 0x215 D0,D1 big-endian, 1/128 km/h.
static void test_speed_is_d0_d1_over_128() {
    const uint8_t f[8] = { 0x1E, 0x00, 0, 0, 0, 0, 0, 0 };   // 0x1E00 = 7680
    TEST_ASSERT_EQUAL_FLOAT(60.0f, bcast_speed_kmh(f));
}

static void test_speed_keeps_the_fraction() {
    const uint8_t f[8] = { 0x00, 0xDB, 0, 0, 0, 0, 0, 0 };   // 219 / 128
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.71f, bcast_speed_kmh(f));
}

// 0x215 D2,D3 big-endian counter, no scaling.
static void test_trip_counter_is_d2_d3() {
    const uint8_t f[8] = { 0, 0, 0x01, 0x2C, 0, 0, 0, 0 };
    TEST_ASSERT_EQUAL_UINT16(300, bcast_trip_counts(f));
}

// 0x608 D0, (D0 - 40) C.
static void test_coolant_temp_offset() {
    const uint8_t warm[8] = { 0x7A, 0, 0, 0, 0, 0, 0, 0 };   // 122 - 40
    const uint8_t cold[8] = { 0x14, 0, 0, 0, 0, 0, 0, 0 };   // 20 - 40
    TEST_ASSERT_EQUAL_FLOAT(82.0f,  bcast_coolant_temp_c(warm));
    TEST_ASSERT_EQUAL_FLOAT(-20.0f, bcast_coolant_temp_c(cold));
}

// 0x424: low beam D1 bit 5, high beam D1 bit 2, turn signals D1 bits 1 and 0,
// doors D2 bits 1 and 0.
static void test_body_flags_lamps_and_doors() {
    const uint8_t f[8] = { 0, ETACS_D1_HEAD_LAMP_LO | ETACS_D1_HEAD_LAMP_HI,
                           ETACS_D2_DRIVER_DOOR, 0, 0, 0, 0, 0 };
    uint8_t flags = bcast_body_flags(f);
    TEST_ASSERT_EQUAL_UINT8(PAYLOAD_BODY_HEAD_LAMP_LO | PAYLOAD_BODY_HEAD_LAMP_HI |
                            PAYLOAD_BODY_DRIVER_DOOR, flags);
    TEST_ASSERT_EQUAL_UINT8(0, flags & PAYLOAD_BODY_OTHER_DOOR);
}

static void test_body_flags_turn_signals() {
    const uint8_t left[8]  = { 0, ETACS_D1_LEFT_TURN,  0, 0, 0, 0, 0, 0 };
    const uint8_t right[8] = { 0, ETACS_D1_RIGHT_TURN, 0, 0, 0, 0, 0, 0 };
    TEST_ASSERT_EQUAL_UINT8(PAYLOAD_BODY_LEFT_TURN,  bcast_body_flags(left));
    TEST_ASSERT_EQUAL_UINT8(PAYLOAD_BODY_RIGHT_TURN, bcast_body_flags(right));
}

// The position lamp is the one bit of this frame still unconfirmed, so it must
// not leak into body_flags.
static void test_body_flags_ignores_unconfirmed_bits() {
    const uint8_t f[8] = { ETACS_D0_POSITION_LAMP, 0, 0, 0, 0, 0, 0, 0 };
    TEST_ASSERT_EQUAL_UINT8(0, bcast_body_flags(f));
}

// 0x445: A/C and recirculation in D3, vent routing and fan in D5.
static void test_climate_flags_and_fan() {
    uint8_t f[8] = { 0, 0, 0, AUTOAC_D3_AC_ON | AUTOAC_D3_RECIRCULATE, 0,
                     (uint8_t)(AUTOAC_D5_VENT_UP | 0x30), 0, 0 };
    TEST_ASSERT_EQUAL_UINT8(PAYLOAD_CLIMATE_AC_ON | PAYLOAD_CLIMATE_RECIRC |
                            PAYLOAD_CLIMATE_VENT_FACE, bcast_climate_flags(f));
    TEST_ASSERT_EQUAL_UINT8(3, bcast_ac_fan_speed(f));   // high nibble of D5
}

static void test_climate_vent_feet_and_windscreen() {
    uint8_t f[8] = { 0, 0, 0, 0, 0,
                     (uint8_t)(AUTOAC_D5_VENT_DOWN | AUTOAC_D5_VENT_WIND), 0, 0 };
    TEST_ASSERT_EQUAL_UINT8(PAYLOAD_CLIMATE_VENT_FEET | PAYLOAD_CLIMATE_VENT_WIND,
                            bcast_climate_flags(f));
}

// 0x445 D0 set point, from the dial sweep on this vehicle: 50 = LO, 64 = 20 C,
// 2 counts per degree to 82 = 29 C, 96 = 30 C, 98 = HI.
static void test_climate_set_temp_scale() {
    uint8_t f[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    f[0] = AUTOAC_D0_TEMP_MIN_RAW; TEST_ASSERT_EQUAL_UINT8(AUTOAC_TEMP_LO, bcast_ac_set_temp(f));
    f[0] = 64;                     TEST_ASSERT_EQUAL_UINT8(20, bcast_ac_set_temp(f));
    f[0] = 66;                     TEST_ASSERT_EQUAL_UINT8(21, bcast_ac_set_temp(f));
    f[0] = 74;                     TEST_ASSERT_EQUAL_UINT8(25, bcast_ac_set_temp(f));
    f[0] = 82;                     TEST_ASSERT_EQUAL_UINT8(29, bcast_ac_set_temp(f));
    f[0] = 96;                     TEST_ASSERT_EQUAL_UINT8(30, bcast_ac_set_temp(f));
    f[0] = AUTOAC_D0_TEMP_MAX_RAW; TEST_ASSERT_EQUAL_UINT8(AUTOAC_TEMP_HI, bcast_ac_set_temp(f));
}

// Below the first numeric step the panel shows no number, so anything under 64
// reports LO rather than a fabricated temperature.
static void test_climate_set_temp_below_range_is_lo() {
    uint8_t f[8] = { 63, 0, 0, 0, 0, 0, 0, 0 };
    TEST_ASSERT_EQUAL_UINT8(AUTOAC_TEMP_LO, bcast_ac_set_temp(f));
    f[0] = 0;
    TEST_ASSERT_EQUAL_UINT8(AUTOAC_TEMP_LO, bcast_ac_set_temp(f));
}

// The payload accessors must invert the server's scaling exactly.
static void test_payload_speed_accessors() {
    Payload p;
    p.speed_kmh_x128 = (uint16_t)(42.5f * 128.0f);
    TEST_ASSERT_EQUAL_FLOAT(42.5f, payload_speed_kmh(&p));
    TEST_ASSERT_EQUAL_UINT8(43, payload_speed_kmh_u8(&p));
    p.speed_kmh_x128 = 0;
    TEST_ASSERT_EQUAL_UINT8(0, payload_speed_kmh_u8(&p));
}

void run_broadcast_decoder_tests() {
    RUN_TEST(test_speed_is_d0_d1_over_128);
    RUN_TEST(test_speed_keeps_the_fraction);
    RUN_TEST(test_trip_counter_is_d2_d3);
    RUN_TEST(test_coolant_temp_offset);
    RUN_TEST(test_body_flags_lamps_and_doors);
    RUN_TEST(test_body_flags_turn_signals);
    RUN_TEST(test_body_flags_ignores_unconfirmed_bits);
    RUN_TEST(test_climate_flags_and_fan);
    RUN_TEST(test_climate_vent_feet_and_windscreen);
    RUN_TEST(test_climate_set_temp_scale);
    RUN_TEST(test_climate_set_temp_below_range_is_lo);
    RUN_TEST(test_payload_speed_accessors);
}
