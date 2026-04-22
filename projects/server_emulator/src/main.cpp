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

    broadcaster.begin(PMK_KEY);

    while (true) {
        uint32_t now_ms   = (uint32_t)(esp_timer_get_time() / 1000);
        uint32_t delta_ms = now_ms - last_tick_ms;
        last_tick_ms      = now_ms;

        generator.tick(delta_ms);
        Payload payload = generator.getPayload();
        broadcaster.send(payload);
        ESP_LOGI(TAG, "timestamp=%" PRIu32 " ms, rpm=%u, speed=%u km/h",
                 payload.timestamp_ms, (unsigned)payload.rpm, (unsigned)payload.speed_kmh);

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

extern "C" void app_main() {
    xTaskCreatePinnedToCore(emulator_task, "emulator", 4096, nullptr, 3, nullptr, 0);
}
#endif
