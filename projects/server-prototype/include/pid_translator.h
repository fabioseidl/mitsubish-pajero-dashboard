#pragma once

#include <stdint.h>
#include "can_frame.h"
#include "pid_map.h"

class PIDTranslator {
public:
    static float translate(const CANFrame& frame, const PidDefinition& def);
    static bool extractMilStatus(const CANFrame& frame);
    static uint8_t extractDtcCount(const CANFrame& frame);

private:
    static float applyLinearFormula(const CANFrame& frame, const PidDefinition& def);
};
