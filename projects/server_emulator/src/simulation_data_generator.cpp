#include "simulation_data_generator.h"
#include "payload.h"
#include "pid_map.h"
#include <cmath>
#include <string.h>

SimulationDataGenerator::SimulationDataGenerator(DrivingProfile profile)
    : profile_(profile), elapsed_ms_(0.0f) {}

void SimulationDataGenerator::tick(uint32_t delta_ms) {
    elapsed_ms_ += (float)delta_ms;
    float speed     = computeSpeedKmh();
    float rpm       = computeRpm();
    float fuel_rate = computeFuelRateLPerH(rpm, speed);
    session_.update(speed, fuel_rate, delta_ms);
}

Payload SimulationDataGenerator::getPayload() const {
    float speed     = computeSpeedKmh();
    float rpm       = computeRpm();
    float fuel_rate = computeFuelRateLPerH(rpm, speed);
    float consumption = (speed > 0.0f && fuel_rate > 0.0f) ? speed / fuel_rate : 0.0f;

    float t           = elapsed_ms_ / 1000.0f;
    float load_pct    = (rpm - 800.0f) / 160.0f;   // ~0-100% scaled from RPM
    float throttle    = load_pct * 0.6f;            // throttle roughly 60% of load

    Payload p;
    memset(&p, 0, sizeof(p));
    p.version                  = PAYLOAD_VERSION;
    p.timestamp_ms             = (uint32_t)elapsed_ms_;

    // Core driving data
    p.rpm                      = (uint16_t)rpm;
    p.speed_kmh                = (uint8_t)speed;
    p.fuel_rate_l_per_h        = fuel_rate;
    p.consumption_km_per_l     = consumption;
    p.avg_consumption_km_per_l = session_.getAvgConsumptionKmPerL();
    p.distance_km              = session_.getDistanceKm();

    // MIL / DTC
    p.mil_on                   = false;
    p.dtc_count                = 0;

    // Verified PIDs
    p.engine_load_pct          = load_pct;
    p.coolant_temp_c           = 88.0f + 4.0f * sinf(t * 0.05f);
    p.map_pressure_kpa         = (uint8_t)(60.0f + 40.0f * (load_pct / 100.0f));
    p.intake_air_temp_c        = 35.0f + 10.0f * (load_pct / 100.0f);
    p.maf_g_per_s              = 5.0f + (rpm / 1000.0f) * 8.0f;
    p.throttle_pct             = throttle;
    p.runtime_s                = (uint16_t)(elapsed_ms_ / 1000.0f);
    p.dist_mil_km              = 0;
    p.fuel_rail_pres_kpa       = 35000.0f + (load_pct / 100.0f) * 65000.0f;
    p.egr_cmd_pct              = (speed < 5.0f) ? 0.0f : 30.0f - load_pct * 0.2f;
    p.egr_error_pct            = 0.0f;
    p.warmups                  = 0;
    p.dist_cleared_km          = 0;
    // Simulated altitude profile: a slow climb/descent between 0 and ~1200 m.
    // Barometric pressure is derived from it (inverse of the altitude formula)
    // so baro and altitude stay mutually consistent, like on the real server.
    float altitude_m           = 600.0f + 600.0f * sinf(t * 0.02f);
    float baro_kpa             = 101.325f * powf(1.0f - altitude_m / 44330.0f, 5.255f);
    p.baro_pressure_kpa        = (uint8_t)(baro_kpa + 0.5f);
    p.altitude_m               = altitude_m;
    p.catalyst_temp_c          = 320.0f + (load_pct / 100.0f) * 180.0f;
    p.module_voltage_v         = 13.8f + 0.4f * sinf(t * 0.1f);
    p.rel_throttle_pct         = throttle * 0.95f;
    p.accel_d_pct              = throttle;
    p.accel_e_pct              = throttle * 0.98f;
    p.throttle_act_pct         = throttle;
    p.time_mil_min             = 0;
    p.time_cleared_min         = (uint16_t)(elapsed_ms_ / 60000.0f);

    // Unverified Mode 01 PIDs
    p.stft_pct                 = 0.0f;
    p.ltft_pct                 = 0.0f;
    p.fuel_pressure_kpa        = 0.0f;
    p.o2_sensor                = 0.0f;
    p.abs_load_pct             = load_pct * 0.9f;
    p.cmd_afr_lambda           = 1.0f;
    p.ambient_temp_c           = 25.0f;
    p.throttle_b_pct           = throttle;
    p.hybrid_batt_pct          = 0.0f;
    p.oil_temp_c               = 85.0f + 5.0f * sinf(t * 0.04f);

    // Mode 01 informational
    p.obd_standards            = 6;     // 6 = EOBD (confirmed by real scan)

    // -----------------------------------------------------------------------
    // Mode 22 — AT ECU (simulated; real ECU returns negative responses)
    // -----------------------------------------------------------------------
    // Estimated gear: 1–5 based on speed
    int gear;
    if      (speed <  5.0f) gear = 1;
    else if (speed < 25.0f) gear = 2;
    else if (speed < 55.0f) gear = 3;
    else if (speed < 85.0f) gear = 4;
    else                    gear = 5;

    // Approximate final-drive + gear ratios → output shaft speed
    // (overall ratio = gear_ratio × differential ratio ≈ gear_ratio × 4.3)
    static const float GEAR_RATIOS[] = { 0.0f, 3.596f, 2.022f, 1.376f, 1.000f, 0.736f };
    float gear_ratio        = GEAR_RATIOS[gear];
    float output_speed_rpm  = (gear_ratio > 0.0f) ? (rpm / (gear_ratio * 4.3f)) * 60.0f : 0.0f;
    float tc_slip_rpm       = (speed < 10.0f) ? rpm * 0.15f                    // high slip at launch
                            : (speed < 30.0f) ? rpm * 0.05f                    // moderate slip in city
                            : 0.0f;                                             // locked up at highway
    bool  lockup            = (speed > 60.0f && gear >= 4);

    p.at_gear_pos            = (float)gear;
    p.at_gear_ratio          = gear_ratio;
    p.at_input_speed_rpm     = rpm;
    p.at_output_speed_rpm    = output_speed_rpm;
    p.at_tc_slip_rpm         = tc_slip_rpm;
    // ATF warms from 60 °C to ~85 °C over the first 5 minutes
    p.at_atf_temp_c          = 60.0f + 25.0f * (1.0f - expf(-elapsed_ms_ / 300000.0f));
    // Shift solenoid bitmask: one bit per solenoid — shifts with gear changes
    p.at_shift_sol_status    = (float)((gear == 1 || gear == 3) ? 0x01 :
                                       (gear == 2 || gear == 4) ? 0x02 : 0x04);
    p.at_lockup_status       = lockup ? 1.0f : 0.0f;
    p.at_prndl               = (profile_ == DrivingProfile::IDLE) ? 1.0f : 4.0f; // 1=P, 4=D
    p.at_target_gear         = (float)gear;   // stable — no shift in progress
    // Transmission line pressure roughly tracks engine load and RPM
    p.at_oil_pres            = 400.0f + (load_pct / 100.0f) * 800.0f
                               + 50.0f * sinf(t * 0.3f);   // kPa (arbitrary scale)

    // -----------------------------------------------------------------------
    // Mode 22 — Engine ECU (simulated; real ECU returns negative responses)
    // -----------------------------------------------------------------------
    // Turbo boost: zero at idle, rises with load (diesel turbo)
    float boost = (load_pct > 10.0f) ? (load_pct - 10.0f) * 1.5f : 0.0f;  // kPa gauge
    p.boost_pres         = boost;

    // EGR valve: wide open at idle/low load, shut at full load
    p.egr_valve_pos_pct  = fmaxf(0.0f, 60.0f - load_pct * 0.7f);

    // DPF soot load accumulates slowly (~2 % per simulated hour)
    p.dpf_soot_load      = fminf(100.0f, (elapsed_ms_ / 3600000.0f) * 2.0f);
    p.dpf_regen_status   = 0.0f;   // no active regeneration in simulation

    // Common-rail fuel pressure: diesel idle ~300–400 bar, full load ~1400 bar
    // (values in bar; scale to your preferred unit when a real formula is known)
    float rail_desired   = 300.0f + (load_pct / 100.0f) * 1100.0f;
    p.rail_pres_des      = rail_desired;
    p.rail_pres_act      = rail_desired + 10.0f * sinf(t * 2.0f);  // small ripple

    // Injector corrections: each cylinder has a slight phase-shifted trim (mg/stroke)
    p.inj_cor_cyl1       =  1.5f * sinf(t * 0.7f);
    p.inj_cor_cyl2       =  1.5f * sinf(t * 0.7f + 1.5708f);
    p.inj_cor_cyl3       =  1.5f * sinf(t * 0.7f + 3.1416f);
    p.inj_cor_cyl4       =  1.5f * sinf(t * 0.7f + 4.7124f);

    // -----------------------------------------------------------------------
    // Mitsubishi advanced PIDs (Pajero 4M41) — DID 0x20F2 / 0x2151
    // -----------------------------------------------------------------------
    // Fuel temperature tracks ambient + load, lagging a little below coolant.
    p.fuel_temp_c        = 40.0f + 30.0f * (load_pct / 100.0f) + 3.0f * sinf(t * 0.03f);
    // Cooling fan stays off until the engine warms, then ramps with coolant temp.
    p.cooling_fan_duty_pct = fminf(100.0f, fmaxf(0.0f, (p.coolant_temp_c - 90.0f) * 12.0f));

    p.flags = PAYLOAD_FLAG_DATA_VALID;
    if (rpm > 400.0f) {
        p.flags |= PAYLOAD_FLAG_ENGINE_RUNNING;
    }
    return p;
}

void SimulationDataGenerator::setProfile(DrivingProfile profile) {
    profile_ = profile;
}

float SimulationDataGenerator::computeRpm() const {
    float t = elapsed_ms_ / 1000.0f;
    switch (profile_) {
        case DrivingProfile::IDLE:
            return 800.0f + 50.0f * sinf(t * 0.2f);
        case DrivingProfile::CITY:
            return 1500.0f + 1000.0f * sinf(t * 0.3f);
        case DrivingProfile::HIGHWAY:
            return 2000.0f + 250.0f * sinf(t * 0.1f);
    }
    return 800.0f;
}

float SimulationDataGenerator::computeSpeedKmh() const {
    float t = elapsed_ms_ / 1000.0f;
    switch (profile_) {
        case DrivingProfile::IDLE:
            return 0.0f;
        case DrivingProfile::CITY:
            return 30.0f + 30.0f * (0.5f + 0.5f * sinf(t * 0.3f));
        case DrivingProfile::HIGHWAY:
            return 100.0f + 10.0f * sinf(t * 0.1f);
    }
    return 0.0f;
}

float SimulationDataGenerator::computeFuelRateLPerH(float /*rpm*/, float speed) const {
    switch (profile_) {
        case DrivingProfile::IDLE:
            return 0.8f;
        case DrivingProfile::CITY:
            return 1.0f + (speed / 10.0f);
        case DrivingProfile::HIGHWAY:
            return 6.0f + (speed - 100.0f) * 0.05f;
    }
    return 0.8f;
}
