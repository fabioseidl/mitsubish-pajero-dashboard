#include "pid_translator.h"
#include <math.h>

float PIDTranslator::translateMode22(const uint8_t* uds, uint8_t uds_len,
                                     const Mode22AdvancedPid& def) {
    // Data byte Dk sits at uds[3 + k] (after 0x62 + 2-byte DID echo).
    const uint8_t hi_idx = 3 + def.data_index;

    if (def.formula_type == M22_U8) {
        if (uds_len <= hi_idx) return NAN;
        return (float)uds[hi_idx] * def.scale + def.offset;
    }

    // U16 / S16 need a second byte.
    const uint8_t lo_idx = hi_idx + 1;
    if (uds_len <= lo_idx) return NAN;
    int32_t raw = ((int32_t)uds[hi_idx] << 8) | uds[lo_idx];
    if (def.formula_type == M22_S16 && raw >= 0x8000) {
        raw -= 0x10000;
    }
    return (float)raw * def.scale + def.offset;
}

float PIDTranslator::translate(const CANFrame& frame, const PidDefinition& def) {
    if (def.formula_type == FORMULA_LINEAR) {
        return applyLinearFormula(frame, def);
    }
    return 0.0f;
}

float PIDTranslator::applyLinearFormula(const CANFrame& frame, const PidDefinition& def) {
    // data[0]=PCI, data[1]=0x41 service, data[2]=PID code, data[3]=byte A, data[4]=byte B
    float byte_A = (float)frame.data[3];
    float byte_B = (float)frame.data[4];
    return (byte_A * def.a_mult + byte_B * def.b_mult) * def.scale + def.offset;
}

bool PIDTranslator::extractMilStatus(const CANFrame& frame) {
    return (frame.data[3] & 0x80) != 0;
}

uint8_t PIDTranslator::extractDtcCount(const CANFrame& frame) {
    return frame.data[3] & 0x7F;
}
