#pragma once

#include <stdint.h>
#include "ican_driver.h"

class CANDriver : public ICANDriver {
public:
    bool begin() override;
    bool isFrameAvailable() override;
    bool readFrame(CANFrame& out_frame) override;
    bool sendFrame(const CANFrame& frame) override;
};
