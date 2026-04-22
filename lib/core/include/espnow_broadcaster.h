#pragma once

#include <stdint.h>
#include "payload.h"

#ifndef UNIT_TEST
#include <esp_now.h>
#include <esp_err.h>
#else
typedef enum {
    ESP_NOW_SEND_SUCCESS = 0,
    ESP_NOW_SEND_FAIL    = 1,
} esp_now_send_status_t;
typedef int esp_err_t;
#define ESP_OK 0
#endif

class ESPNowBroadcaster {
public:
    ESPNowBroadcaster() = default;

    bool begin(const uint8_t pmk[16]);
    bool send(const Payload& payload);
    esp_now_send_status_t lastSendStatus() const;
    esp_err_t             lastAddPeerErr() const { return last_add_peer_err_; }
    esp_err_t             lastSendErr()    const { return last_send_err_; }

private:
    bool initialized_;
    esp_now_send_status_t last_send_status_;
    esp_err_t             last_add_peer_err_ = 0;
    esp_err_t             last_send_err_     = 0;

    static void onSendComplete(const uint8_t* mac, esp_now_send_status_t status);
};
