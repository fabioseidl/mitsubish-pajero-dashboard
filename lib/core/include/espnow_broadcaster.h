#pragma once

#include <stdint.h>
#include "payload.h"

#ifndef UNIT_TEST
#include <esp_now.h>
#else
typedef enum {
    ESP_NOW_SEND_SUCCESS = 0,
    ESP_NOW_SEND_FAIL    = 1,
} esp_now_send_status_t;
#endif

class ESPNowBroadcaster {
public:
    ESPNowBroadcaster() = default;

    bool begin(const uint8_t pmk[16]);
    bool send(const Payload& payload);
    esp_now_send_status_t lastSendStatus() const;

private:
    bool initialized_;
    esp_now_send_status_t last_send_status_;

    static void onSendComplete(const uint8_t* mac, esp_now_send_status_t status);
};
