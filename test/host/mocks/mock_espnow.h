#pragma once

#include <stdint.h>
#include "espnow_receiver.h"

// Deliver a frame as the ESP-NOW receive callback would. `mac` is the sender's
// address; nullptr stands for "unknown sender", which is what the real callback
// passes when no address is available.
inline void simulateReceive(ESPNowReceiver& /*receiver*/,
                            const uint8_t* data,
                            int len,
                            const uint8_t* mac = nullptr) {
    ESPNowReceiver::onReceiveISR(mac, data, len);
}
