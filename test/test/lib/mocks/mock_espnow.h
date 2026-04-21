#pragma once

#include <stdint.h>
#include "espnow_receiver.h"

inline void simulateReceive(ESPNowReceiver& /*receiver*/,
                            const uint8_t* data,
                            int len) {
    ESPNowReceiver::onReceiveISR(nullptr, data, len);
}
