#pragma once

#include <queue>
#include "ican_driver.h"
#include "can_frame.h"

class MockCANDriver : public ICANDriver {
public:
    bool begin() override { return begin_return_value; }

    bool isFrameAvailable() override {
        return !queued_frames.empty();
    }

    bool readFrame(CANFrame& out) override {
        if (queued_frames.empty()) return false;
        out = queued_frames.front();
        queued_frames.pop();
        return true;
    }

    void pushFrame(const CANFrame& frame) {
        queued_frames.push(frame);
    }

    bool begin_return_value = true;
    std::queue<CANFrame> queued_frames;
};
