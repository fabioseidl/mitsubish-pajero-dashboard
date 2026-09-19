#include "payload_builder.h"
#include "pid_map.h"
#include "derived_calculator.h"

Payload PayloadBuilder::build(const DataAggregator& aggregator,
                               const SessionAccumulator& session,
                               float consumption,
                               uint32_t timestamp_ms) {
    // Zero-init, never default-init. Every field below is assigned today, so
    // `Payload p;` happens to be correct — but the failure mode when a newly
    // added field is missed here is stack garbage broadcast to every client, and
    // the static_assert cannot catch it (the assert gets updated as part of
    // adding the field). A 149-byte memset at 10 Hz is not worth the risk.
    Payload p{};
    p.version                  = PAYLOAD_VERSION;
    p.timestamp_ms             = timestamp_ms;

    // Core driving data
    p.rpm                      = (uint16_t)aggregator.get(PID_RPM);
    p.speed_kmh                = (uint8_t)aggregator.get(PID_SPEED);
    p.fuel_rate_l_per_h        = DerivedCalculator::computeFuelRate(aggregator);
    p.consumption_km_per_l     = consumption;
    p.avg_consumption_km_per_l = session.getAvgConsumptionKmPerL();
    p.distance_km              = session.getDistanceKm();

    // MIL / DTC
    p.mil_on                   = aggregator.getMilStatus();
    p.dtc_count                = aggregator.getDtcCount();

    // Verified PIDs
    p.engine_load_pct          = aggregator.get(PID_ENGINE_LOAD);
    p.coolant_temp_c           = aggregator.get(PID_COOLANT_TEMP);
    p.map_pressure_kpa         = (uint8_t)aggregator.get(PID_MAP_PRESSURE);
    p.intake_air_temp_c        = aggregator.get(PID_INTAKE_AIR_TEMP);
    p.maf_g_per_s              = aggregator.get(PID_MAF);
    p.throttle_pct             = aggregator.get(PID_THROTTLE);
    p.runtime_s                = (uint16_t)aggregator.get(PID_RUNTIME);
    p.dist_mil_km              = (uint16_t)aggregator.get(PID_DIST_MIL);
    p.fuel_rail_pres_kpa       = aggregator.get(PID_FUEL_RAIL_PRES);
    p.egr_cmd_pct              = aggregator.get(PID_EGR_CMD);
    p.egr_error_pct            = aggregator.get(PID_EGR_ERROR);
    p.warmups                  = (uint8_t)aggregator.get(PID_WARMUPS);
    p.dist_cleared_km          = (uint16_t)aggregator.get(PID_DIST_CLEARED);
    p.baro_pressure_kpa        = (uint8_t)aggregator.get(PID_BARO_PRESSURE);
    p.altitude_m               = DerivedCalculator::computeAltitude(aggregator.get(PID_BARO_PRESSURE));
    p.catalyst_temp_c          = aggregator.get(PID_CATALYST_TEMP);
    p.module_voltage_v         = aggregator.get(PID_MODULE_VOLTAGE);
    p.rel_throttle_pct         = aggregator.get(PID_REL_THROTTLE);
    p.accel_d_pct              = aggregator.get(PID_ACCEL_D);
    p.accel_e_pct              = aggregator.get(PID_ACCEL_E);
    p.throttle_act_pct         = aggregator.get(PID_THROTTLE_ACT);
    p.time_mil_min             = (uint16_t)aggregator.get(PID_TIME_MIL);
    p.time_cleared_min         = (uint16_t)aggregator.get(PID_TIME_CLEARED);

    // Unverified PIDs
    p.stft_pct                 = aggregator.get(PID_STFT);
    p.ltft_pct                 = aggregator.get(PID_LTFT);
    p.fuel_pressure_kpa        = aggregator.get(PID_FUEL_PRESSURE);
    p.o2_sensor                = aggregator.get(PID_O2_SENSOR);
    p.abs_load_pct             = aggregator.get(PID_ABS_LOAD);
    p.cmd_afr_lambda           = aggregator.get(PID_CMD_AFR);
    p.ambient_temp_c           = aggregator.get(PID_AMBIENT_TEMP);
    p.throttle_b_pct           = aggregator.get(PID_THROTTLE_B);
    p.oil_temp_c               = aggregator.get(PID_OIL_TEMP);

    // Mode 01 informational
    p.obd_standards            = (uint8_t)aggregator.get(PID_OBD_STANDARDS);

    // Free-running CAN broadcast frames. 0x218 is emitted continuously by the
    // 4M41, so these are the only transmission values that exist on this vehicle
    // — every Mode 22 field was removed in PAYLOAD_VERSION 5 (see payload.h).
    p.at_gear_pos              = aggregator.get(PID_M22_AT_GEAR_POS);
    p.at_target_gear           = aggregator.get(PID_M22_AT_TARGET_GEAR);

    // Boost is derived, not read: PID_M22_BOOST_PRES is one of the speculative
    // 0xF3xx DIDs this ECU never answers, so it was always 0. Computed from
    // manifold + ambient pressure instead. Units: bar (gauge), which is what the
    // main_display "BOOST bar" readout expects.
    p.boost_pres               = DerivedCalculator::computeBoostBar(aggregator);

    p.flags = 0;
    if (aggregator.allRequiredPidsReceived()) {
        p.flags |= PAYLOAD_FLAG_DATA_VALID;
    }
    if (aggregator.get(PID_RPM) > 400.0f) {
        p.flags |= PAYLOAD_FLAG_ENGINE_RUNNING;
    }
    return p;
}
