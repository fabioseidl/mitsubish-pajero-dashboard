#pragma once

#include <stdint.h>
#include "i_display.h"

class MockDisplay : public IDisplay {
public:
    bool begin() override {
        begin_called = true;
        return true;
    }

    void setBacklightPercent(uint8_t percent) override {
        last_percent = percent;
        call_count++;
    }

    bool    begin_called = false;
    uint8_t last_percent = 0;
    int     call_count   = 0;
};
