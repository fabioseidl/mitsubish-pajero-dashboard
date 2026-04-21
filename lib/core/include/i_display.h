#pragma once

#include <stdint.h>

class IDisplay {
public:
    virtual ~IDisplay() = default;

    virtual bool begin() = 0;
    virtual void setBacklightPercent(uint8_t percent) = 0;
};
