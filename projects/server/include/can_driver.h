#pragma once

#include <stdint.h>
#include "ican_driver.h"

class CANDriver : public ICANDriver {
public:
    bool begin() override;
    bool isFrameAvailable() override;
    bool readFrame(CANFrame& out_frame) override;
    bool sendFrame(const CANFrame& frame) override;

    // Drain any pending RX frames and clear all interrupt flags so the MCP2515
    // de-asserts its INT pin. Call right before entering deep sleep so the pin
    // only re-asserts (waking the MCU) when *new* bus traffic arrives.
    void prepareForSleep();
};
