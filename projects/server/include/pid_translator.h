#pragma once

#include <stdint.h>
#include "can_frame.h"
#include "pid_map.h"

class PIDTranslator {
public:
    static float translate(const CANFrame& frame, const PidDefinition& def);
    static bool extractMilStatus(const CANFrame& frame);
    static uint8_t extractDtcCount(const CANFrame& frame);

    // Apply a Mode 22 advanced-PID formula to a reassembled UDS payload.
    // `uds` starts at the positive-response service byte: uds[0]=0x62,
    // uds[1..2]=DID echo, uds[3+k]=data byte Dk. Returns NaN when the buffer is
    // too short to hold the byte(s) the formula needs.
    static float translateMode22(const uint8_t* uds, uint8_t uds_len,
                                 const Mode22AdvancedPid& def);

private:
    static float applyLinearFormula(const CANFrame& frame, const PidDefinition& def);
};
