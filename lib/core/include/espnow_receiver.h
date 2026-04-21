#pragma once

#include <stdint.h>
#include "payload.h"

using PayloadCallback = void (*)(const Payload& payload);

class ESPNowReceiver {
public:
    ESPNowReceiver() = default;

    bool begin(const uint8_t pmk[16]);
    void setCallback(PayloadCallback cb);

    static void onReceiveISR(const uint8_t* mac,
                              const uint8_t* data,
                              int len);

private:
    static ESPNowReceiver* instance_;

    PayloadCallback callback_ = nullptr;
};
