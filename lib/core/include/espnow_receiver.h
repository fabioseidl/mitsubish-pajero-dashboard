#pragma once

#include <stdint.h>
#include "payload.h"

using PayloadCallback = void (*)(const Payload& payload);

class ESPNowReceiver {
public:
    ESPNowReceiver() = default;

    bool begin(const uint8_t pmk[16]);
    void setCallback(PayloadCallback cb);

    // Restrict accepted broadcasts to one sender MAC.
    //
    // ESP-NOW cannot encrypt a broadcast — esp_now_add_peer() sets encrypt=false
    // for FF:FF:FF:FF:FF:FF — so the PMK protects nothing on this link and the
    // payload travels in the clear. Without a filter, ANY ESP32 in range can put
    // a fabricated speed on the windshield HUD simply by broadcasting a
    // well-formed Payload. Checking the source MAC is the cheap mitigation: it
    // stops accidents and casual spoofing, though not an attacker who bothers to
    // clone the address.
    //
    // Set it at compile time by defining ESPNOW_SERVER_MAC in security_config.h
    // (see security_config.h.example), or at runtime here. With neither, every
    // sender is accepted, which is the historical behaviour.
    void setExpectedSender(const uint8_t mac[6]);
    void clearExpectedSender();

    static void onReceiveISR(const uint8_t* mac,
                              const uint8_t* data,
                              int len);

    static uint32_t raw_rx_count_;
    // Frames dropped because the sender MAC did not match. A non-zero value with
    // a dead display means the filter is pointed at the wrong MAC.
    static uint32_t rejected_sender_count_;

private:
    static ESPNowReceiver* instance_;
    static uint8_t         expected_mac_[6];
    static bool            filter_sender_;

    PayloadCallback callback_ = nullptr;
};
