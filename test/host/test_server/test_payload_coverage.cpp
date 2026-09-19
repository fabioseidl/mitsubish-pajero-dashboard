// Payload field coverage — the guard against a field that silently stays zero.
//
// The emulator hand-builds its Payload in simulation_data_generator.cpp, entirely
// independently of the server's PayloadBuilder. Nothing forced the two to agree,
// so adding a Payload field meant remembering to populate it in both places; miss
// the emulator and the field is silently 0 on the bench, which is precisely the
// case client development cannot detect. (The static_assert on sizeof(Payload)
// does not help — it gets updated as part of adding the field.)
//
// This closes that loop with a field table that must account for every byte of
// the struct. Add a field without listing it here and FIELDS_COVER_WHOLE_PAYLOAD
// fails, which forces an explicit answer to "does the emulator populate this?".
//
// It also caught a real divergence when it was written: the emulator produced
// boost_pres in kPa while the server produced bar.

#include <unity.h>
#include <stddef.h>
#include <string.h>
#include "payload.h"
#include "simulation_data_generator.h"

namespace {

struct FieldSpec {
    size_t      offset;
    size_t      size;
    const char* name;
    // true  → the emulator must produce a non-zero value while "driving"
    // false → legitimately zero in a healthy simulation (fault counters, PIDs the
    //         simulation deliberately leaves unsupported)
    bool        expect_nonzero;
};

#define F(field, nonzero) { offsetof(Payload, field), sizeof(((Payload*)0)->field), #field, nonzero }

const FieldSpec kFields[] = {
    F(version,                  true),
    F(timestamp_ms,             true),
    F(rpm,                      true),
    F(speed_kmh,                true),
    F(fuel_rate_l_per_h,        true),
    F(consumption_km_per_l,     true),
    F(avg_consumption_km_per_l, true),
    F(distance_km,              true),
    F(mil_on,                   false),  // no simulated fault
    F(dtc_count,                false),  // no simulated fault
    F(engine_load_pct,          true),
    F(coolant_temp_c,           true),
    F(map_pressure_kpa,         true),
    F(intake_air_temp_c,        true),
    F(maf_g_per_s,              true),
    F(throttle_pct,             true),
    F(runtime_s,                true),
    F(dist_mil_km,              false),  // no MIL, so no distance since MIL
    F(fuel_rail_pres_kpa,       true),
    F(egr_cmd_pct,              true),
    F(egr_error_pct,            false),  // simulated as a perfectly tracking valve
    F(warmups,                  false),  // single continuous simulated run
    F(dist_cleared_km,          false),  // codes never cleared in simulation
    F(baro_pressure_kpa,        true),
    F(altitude_m,               true),
    F(catalyst_temp_c,          true),
    F(module_voltage_v,         true),
    F(rel_throttle_pct,         true),
    F(accel_d_pct,              true),
    F(accel_e_pct,              true),
    F(throttle_act_pct,         true),
    F(time_mil_min,             false),  // no MIL
    F(time_cleared_min,         true),
    F(stft_pct,                 false),  // diesel: no fuel trim reported
    F(ltft_pct,                 false),  // diesel: no fuel trim reported
    F(fuel_pressure_kpa,        false),  // unsupported on this vehicle
    F(o2_sensor,                false),  // unsupported on this vehicle
    F(abs_load_pct,             true),
    F(cmd_afr_lambda,           true),
    F(ambient_temp_c,           true),
    F(throttle_b_pct,           true),
    F(oil_temp_c,               true),
    F(obd_standards,            true),
    F(at_gear_pos,              true),
    F(at_target_gear,           true),
    F(boost_pres,               true),
    F(flags,                    true),
};

#undef F

const size_t kFieldCount = sizeof(kFields) / sizeof(kFields[0]);

bool all_zero(const Payload& p, const FieldSpec& f) {
    const unsigned char* base = reinterpret_cast<const unsigned char*>(&p);
    for (size_t i = 0; i < f.size; ++i) {
        if (base[f.offset + i] != 0) return false;
    }
    return true;
}

// Drive the simulation far enough that every ramp has left zero.
Payload driven_payload() {
    SimulationDataGenerator gen(DrivingProfile::HIGHWAY);
    for (int i = 0; i < 600; ++i) {   // 600 * 100 ms = 60 simulated seconds
        gen.tick(100);
    }
    return gen.getPayload();
}

}  // namespace

// The table must describe the struct exactly: same field order, no gaps, no
// overlaps, and ending precisely at sizeof(Payload). This is what makes a newly
// added field impossible to ignore.
static void test_field_table_covers_whole_payload() {
    size_t expected_offset = 0;
    for (size_t i = 0; i < kFieldCount; ++i) {
        TEST_ASSERT_EQUAL_MESSAGE((int)expected_offset, (int)kFields[i].offset,
                                  kFields[i].name);
        expected_offset += kFields[i].size;
    }
    TEST_ASSERT_EQUAL_MESSAGE((int)sizeof(Payload), (int)expected_offset,
        "Payload field table is out of sync with the struct - a field was added "
        "or removed without updating test_payload_coverage.cpp");
}

// Every field the emulator is supposed to drive must actually be non-zero after
// a minute of simulated highway driving.
static void test_emulator_populates_every_expected_field() {
    Payload p = driven_payload();
    for (size_t i = 0; i < kFieldCount; ++i) {
        if (!kFields[i].expect_nonzero) continue;
        TEST_ASSERT_FALSE_MESSAGE(all_zero(p, kFields[i]), kFields[i].name);
    }
}

// The emulator must speak the same protocol version as everything else, or the
// clients silently drop its frames (ESPNowReceiver checks the version).
static void test_emulator_uses_current_payload_version() {
    Payload p = driven_payload();
    TEST_ASSERT_EQUAL_UINT8(PAYLOAD_VERSION, p.version);
}

// Boost is in BAR on both sides. The emulator once produced kPa here, which put
// the main_display "BOOST bar" readout 100x high on the bench only.
static void test_emulator_boost_is_in_bar_not_kpa() {
    Payload p = driven_payload();
    // A 4M41 runs roughly 0.0-1.5 bar gauge. Anything above ~3 means kPa leaked in.
    TEST_ASSERT_TRUE_MESSAGE(p.boost_pres >= 0.0f && p.boost_pres < 3.0f,
                             "boost_pres out of plausible bar range - units bug?");
}

// The whole point of trimming v4: the broadcast has to fit, with room to grow.
static void test_payload_fits_espnow_broadcast() {
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(PAYLOAD_MAX_WIRE_BYTES, (uint32_t)sizeof(Payload));
}

void run_payload_coverage_tests() {
    RUN_TEST(test_field_table_covers_whole_payload);
    RUN_TEST(test_emulator_populates_every_expected_field);
    RUN_TEST(test_emulator_uses_current_payload_version);
    RUN_TEST(test_emulator_boost_is_in_bar_not_kpa);
    RUN_TEST(test_payload_fits_espnow_broadcast);
}

// Single-file compilation, as elsewhere in this suite. SessionAccumulator (a
// member of SimulationDataGenerator) is NOT included here — test_session_
// accumulator.cpp already provides it to the link.
#include "../../../projects/server_emulator/src/simulation_data_generator.cpp"
