#include "pid_dictionary.h"

static const uint32_t OBD_RESPONSE_ID = 0x7E8;

const PidDefinition* PIDDictionary::lookup(uint32_t can_id, uint8_t pid_code) const {
    if (!isKnownId(can_id)) return nullptr;
    for (size_t i = 0; i < PID_MAP_SIZE; i++) {
        if ((uint8_t)PID_MAP[i].pid == pid_code) {
            return &PID_MAP[i];
        }
    }
    return nullptr;
}

bool PIDDictionary::isKnownId(uint32_t can_id) const {
    return can_id == OBD_RESPONSE_ID;
}

size_t PIDDictionary::size() const {
    return PID_MAP_SIZE;
}
