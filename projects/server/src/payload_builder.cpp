#include "payload_builder.h"
#include "pid_map.h"

Payload PayloadBuilder::build(const DataAggregator& aggregator,
                               const SessionAccumulator& session,
                               float consumption,
                               uint32_t timestamp_ms) {
    Payload p;
    p.version                  = PAYLOAD_VERSION;
    p.timestamp_ms             = timestamp_ms;

    // Core driving data
    p.rpm                      = (uint16_t)aggregator.get(PID_RPM);
    p.speed_kmh                = (uint8_t)aggregator.get(PID_SPEED);
    p.fuel_rate_l_per_h        = aggregator.get(PID_FUEL_RATE);
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
    p.hybrid_batt_pct          = aggregator.get(PID_HYBRID_BATT);
    p.oil_temp_c               = aggregator.get(PID_OIL_TEMP);

    // Mode 01 informational
    p.obd_standards            = (uint8_t)aggregator.get(PID_OBD_STANDARDS);

    // Mode 22 — AT ECU (slot IDs PID_M22_AT_*)
    p.at_gear_pos              = aggregator.get(PID_M22_AT_GEAR_POS);
    p.at_gear_ratio            = aggregator.get(PID_M22_AT_GEAR_RATIO);
    p.at_input_speed_rpm       = aggregator.get(PID_M22_AT_INPUT_SPEED);
    p.at_output_speed_rpm      = aggregator.get(PID_M22_AT_OUTPUT_SPEED);
    p.at_tc_slip_rpm           = aggregator.get(PID_M22_AT_TC_SLIP);
    p.at_atf_temp_c            = aggregator.get(PID_M22_AT_ATF_TEMP);
    p.at_shift_sol_status      = aggregator.get(PID_M22_AT_SHIFT_SOL);
    p.at_lockup_status         = aggregator.get(PID_M22_AT_LOCKUP);
    p.at_prndl                 = aggregator.get(PID_M22_AT_PRNDL);
    p.at_target_gear           = aggregator.get(PID_M22_AT_TARGET_GEAR);
    p.at_oil_pres              = aggregator.get(PID_M22_AT_OIL_PRES);

    // Mode 22 — Engine ECU (slot IDs PID_M22_BOOST_PRES … PID_M22_INJ_COR_CYL4)
    p.boost_pres               = aggregator.get(PID_M22_BOOST_PRES);
    p.egr_valve_pos_pct        = aggregator.get(PID_M22_EGR_VALVE_POS);
    p.dpf_soot_load            = aggregator.get(PID_M22_DPF_SOOT);
    p.dpf_regen_status         = aggregator.get(PID_M22_DPF_REGEN);
    p.rail_pres_act            = aggregator.get(PID_M22_RAIL_PRES_ACT);
    p.rail_pres_des            = aggregator.get(PID_M22_RAIL_PRES_DES);
    p.inj_cor_cyl1             = aggregator.get(PID_M22_INJ_COR_CYL1);
    p.inj_cor_cyl2             = aggregator.get(PID_M22_INJ_COR_CYL2);
    p.inj_cor_cyl3             = aggregator.get(PID_M22_INJ_COR_CYL3);
    p.inj_cor_cyl4             = aggregator.get(PID_M22_INJ_COR_CYL4);

    p.flags = 0;
    if (aggregator.allRequiredPidsReceived()) {
        p.flags |= PAYLOAD_FLAG_DATA_VALID;
    }
    if (aggregator.get(PID_RPM) > 400.0f) {
        p.flags |= PAYLOAD_FLAG_ENGINE_RUNNING;
    }
    return p;
}
