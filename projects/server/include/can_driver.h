#pragma once

#include <stdint.h>
#include "ican_driver.h"

class CANDriver : public ICANDriver {
public:
    explicit CANDriver(uint8_t cs_pin, uint8_t int_pin);

    bool begin() override;
    bool isFrameAvailable() override;
    bool readFrame(CANFrame& out_frame) override;

private:
    uint8_t cs_pin_;
    uint8_t int_pin_;
};
