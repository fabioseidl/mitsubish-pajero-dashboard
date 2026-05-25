#ifndef UNIT_TEST
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_timer.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <esp_netif.h>
#include <esp_event.h>

#include "simulation_data_generator.h"
#include "espnow_broadcaster.h"
#include "security_config.h"

static const char* TAG = "emulator";

static void emulator_task(void* /*param*/) {
    SimulationDataGenerator generator(DrivingProfile::CITY);
    ESPNowBroadcaster       broadcaster;
    uint32_t                last_tick_ms = 0;

    bool begin_ok = broadcaster.begin(PMK_KEY);
    ESP_LOGI(TAG, "begin=%d add_peer_err=%d send_err_init=%d",
             begin_ok, (int)broadcaster.lastAddPeerErr(), (int)broadcaster.lastSendErr());

    uint32_t send_count = 0;
    uint32_t fail_count = 0;

    while (true) {
        uint32_t now_ms   = (uint32_t)(esp_timer_get_time() / 1000);
        uint32_t delta_ms = now_ms - last_tick_ms;
        last_tick_ms      = now_ms;

        generator.tick(delta_ms);
        Payload payload = generator.getPayload();
        bool sent = broadcaster.send(payload);
        sent ? ++send_count : ++fail_count;

        if (fail_count % 50 == 1) {
            ESP_LOGI(TAG, "send_ok=%d sent=%lu failed=%lu add_peer_err=%d last_send_err=%d",
                     sent, (unsigned long)send_count, (unsigned long)fail_count,
                     (int)broadcaster.lastAddPeerErr(), (int)broadcaster.lastSendErr());
        }
        ESP_LOGI(TAG,
                 "ts=%" PRIu32 "ms | rpm=%u spd=%u km/h fuel_rate=%.2f L/h cons=%.2f km/L avg_cons=%.2f km/L dist=%.3f km | "
                 "load=%.1f%% coolant=%.1fc map=%u kPa iat=%.1fc maf=%.2f g/s tps=%.1f%% runtime=%u s | "
                 "mil=%d dtc=%u dist_mil=%u km time_mil=%u min dist_clr=%u km time_clr=%u min warmups=%u | "
                 "fuel_rail=%.1f kPa egr_cmd=%.1f%% egr_err=%.1f%% baro=%u kPa cat=%.1fc vbat=%.2f V | "
                 "rel_tps=%.1f%% accel_d=%.1f%% accel_e=%.1f%% tps_act=%.1f%% | "
                 "stft=%.2f%% ltft=%.2f%% fp=%.1f kPa o2=%.3f abs_load=%.1f%% afr=%.3f amb=%.1fc tps_b=%.1f%% hv_batt=%.1f%% oil=%.1fc obd=%u | "
                 "at_gear=%.0f ratio=%.3f in_spd=%.0f out_spd=%.0f tc_slip=%.0f atf=%.1fc sol=%.0f lockup=%.0f prndl=%.0f tgt_gear=%.0f oil_pres=%.1f | "
                 "boost=%.1f egr_pos=%.1f%% dpf_soot=%.2f dpf_regen=%.0f rail_act=%.1f rail_des=%.1f inj=%.2f/%.2f/%.2f/%.2f | "
                 "flags=0x%02X",
                 payload.timestamp_ms,
                 (unsigned)payload.rpm, (unsigned)payload.speed_kmh,
                 payload.fuel_rate_l_per_h, payload.consumption_km_per_l,
                 payload.avg_consumption_km_per_l, payload.distance_km,
                 payload.engine_load_pct, payload.coolant_temp_c,
                 (unsigned)payload.map_pressure_kpa, payload.intake_air_temp_c,
                 payload.maf_g_per_s, payload.throttle_pct, (unsigned)payload.runtime_s,
                 (int)payload.mil_on, (unsigned)payload.dtc_count,
                 (unsigned)payload.dist_mil_km, (unsigned)payload.time_mil_min,
                 (unsigned)payload.dist_cleared_km, (unsigned)payload.time_cleared_min,
                 (unsigned)payload.warmups,
                 payload.fuel_rail_pres_kpa, payload.egr_cmd_pct, payload.egr_error_pct,
                 (unsigned)payload.baro_pressure_kpa, payload.catalyst_temp_c, payload.module_voltage_v,
                 payload.rel_throttle_pct, payload.accel_d_pct, payload.accel_e_pct, payload.throttle_act_pct,
                 payload.stft_pct, payload.ltft_pct, payload.fuel_pressure_kpa, payload.o2_sensor,
                 payload.abs_load_pct, payload.cmd_afr_lambda, payload.ambient_temp_c,
                 payload.throttle_b_pct, payload.hybrid_batt_pct, payload.oil_temp_c,
                 (unsigned)payload.obd_standards,
                 payload.at_gear_pos, payload.at_gear_ratio, payload.at_input_speed_rpm,
                 payload.at_output_speed_rpm, payload.at_tc_slip_rpm, payload.at_atf_temp_c,
                 payload.at_shift_sol_status, payload.at_lockup_status, payload.at_prndl,
                 payload.at_target_gear, payload.at_oil_pres,
                 payload.boost_pres, payload.egr_valve_pos_pct, payload.dpf_soot_load,
                 payload.dpf_regen_status, payload.rail_pres_act, payload.rail_pres_des,
                 payload.inj_cor_cyl1, payload.inj_cor_cyl2, payload.inj_cor_cyl3, payload.inj_cor_cyl4,
                 (unsigned)payload.flags);

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

extern "C" void app_main() {
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    xTaskCreatePinnedToCore(emulator_task, "emulator", 4096, nullptr, 3, nullptr, 0);
}
#endif
