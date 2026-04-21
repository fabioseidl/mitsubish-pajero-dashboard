#pragma once

#include "can_frame.h"

class ICANDriver {
public:
    virtual ~ICANDriver() = default;

    virtual bool begin() = 0;
    virtual bool isFrameAvailable() = 0;
    virtual bool readFrame(CANFrame& out_frame) = 0;
};
