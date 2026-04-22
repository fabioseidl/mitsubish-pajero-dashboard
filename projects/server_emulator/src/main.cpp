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
        ESP_LOGI(TAG, "timestamp=%" PRIu32 " ms, rpm=%u, speed=%u km/h",
                 payload.timestamp_ms, (unsigned)payload.rpm, (unsigned)payload.speed_kmh);

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
