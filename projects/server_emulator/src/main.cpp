#ifndef UNIT_TEST
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_timer.h>
#include <esp_log.h>

#include "simulation_data_generator.h"
#include "espnow_broadcaster.h"
#include "security_config.h"

static const char* TAG = "emulator";

static void emulator_task(void* /*param*/) {
    SimulationDataGenerator generator(DrivingProfile::CITY);
    ESPNowBroadcaster       broadcaster;
    uint32_t                last_tick_ms = 0;

    bool begin_ok = broadcaster.begin(PMK_KEY);
    ESP_LOGI(TAG, "broadcaster.begin=%d", begin_ok);

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

        if (send_count % 50 == 1 || fail_count > 0) {
            ESP_LOGI(TAG, "send_ok=%d sent=%lu failed=%lu status=%d",
                     sent, (unsigned long)send_count, (unsigned long)fail_count,
                     (int)broadcaster.lastSendStatus());
        }
        ESP_LOGI(TAG, "timestamp=%" PRIu32 " ms, rpm=%u, speed=%u km/h",
                 payload.timestamp_ms, (unsigned)payload.rpm, (unsigned)payload.speed_kmh);

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

extern "C" void app_main() {
    xTaskCreatePinnedToCore(emulator_task, "emulator", 4096, nullptr, 3, nullptr, 0);
}
#endif
