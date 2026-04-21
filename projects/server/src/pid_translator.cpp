#include "pid_translator.h"

float PIDTranslator::translate(const CANFrame& frame, const PidDefinition& def) {
    if (def.formula_type == FORMULA_LINEAR) {
        return applyLinearFormula(frame, def);
    }
    return 0.0f;
}

float PIDTranslator::applyLinearFormula(const CANFrame& frame, const PidDefinition& def) {
    float byte_A = (float)frame.data[2];
    float byte_B = (float)frame.data[3];
    return (byte_A * def.a_mult + byte_B * def.b_mult) * def.scale + def.offset;
}

bool PIDTranslator::extractMilStatus(const CANFrame& frame) {
    return (frame.data[3] & 0x80) != 0;
}

uint8_t PIDTranslator::extractDtcCount(const CANFrame& frame) {
    return frame.data[3] & 0x7F;
}
