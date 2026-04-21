#pragma once

#include <stdint.h>

struct CANFrame {
    uint32_t id;
    uint8_t  data[8];
    uint8_t  dlc;
    bool     is_extended;
};
