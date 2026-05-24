#pragma once

#include <stdint.h>
#include <stddef.h>
#include "pid_map.h"

class PIDDictionary {
public:
    PIDDictionary() = default;

    const PidDefinition* lookup(uint32_t can_id, uint8_t pid_code) const;
    bool isKnownId(uint32_t can_id) const;
    size_t size() const;
};
