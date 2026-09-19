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

    // Calculated engine load, scaled from rpm across the 4M41's usable band:
    // idle (800) → 0 %, redline-ish (4000) → 100 %, so the divisor is 32, not the
    // 160 this used to carry. With 160 the whole simulation topped out near 14 %
    // load, which dragged down everything derived from it (boost, MAP, EGR
    // command, catalyst and intake temps, absolute load) and left boost pinned at
    // zero on the highway profile. Clamped because the CITY profile's rpm swing
    // dips below idle.
    float load_pct    = (rpm - 800.0f) / 32.0f;
    if (load_pct <   0.0f) load_pct =   0.0f;
    if (load_pct > 100.0f) load_pct = 100.0f;
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
    p.trip_time_s              = session_.getTripTimeS();

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
    p.oil_temp_c               = 85.0f + 5.0f * sinf(t * 0.04f);

    // Mode 01 informational
    p.obd_standards            = 6;     // 6 = EOBD (confirmed by real scan)

    // -----------------------------------------------------------------------
    // Transmission gear (CAN 0x218)
    // -----------------------------------------------------------------------
    // PAYLOAD_VERSION 5 removed every Mode 22 field — this vehicle's ECUs reject
    // UDS service 0x22 outright, so the real server can never populate them and
    // simulating them only taught client code to expect data that will not come.
    // Gear survives because it rides a free-running broadcast frame.
    int gear;
    if      (speed <  5.0f) gear = 1;
    else if (speed < 25.0f) gear = 2;
    else if (speed < 55.0f) gear = 3;
    else if (speed < 85.0f) gear = 4;
    else                    gear = 5;

    // Gear *code* uses the real 0x218 encoding (see GEAR_CODE_* in pid_map.h):
    // Park while idle/stationary, else the forward gear number 1..5.
    float gear_code          = (profile_ == DrivingProfile::IDLE)
                               ? (float)GEAR_CODE_PARK : (float)gear;
    p.at_gear_pos            = gear_code;
    p.at_target_gear         = gear_code;     // stable — no shift in progress

    // -----------------------------------------------------------------------
    // Boost (derived on the real server by DerivedCalculator::computeBoostBar)
    // -----------------------------------------------------------------------
    // UNITS: bar gauge, matching the server. An earlier revision produced kPa
    // here while the server produced bar, so the emulator drove the main_display
    // "BOOST bar" readout 100x high — precisely the kind of divergence the
    // emulator exists to prevent. Keep this in bar.
    p.boost_pres         = (load_pct > 10.0f) ? (load_pct - 10.0f) * 0.015f : 0.0f;

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
