#pragma once

#include <stdint.h>

class DataAggregator {
public:
    DataAggregator();
    ~DataAggregator();

    void update(uint16_t pid, float value);
    void updateMilStatus(bool mil_on);
    void updateDtcCount(uint8_t count);

    float get(uint16_t pid) const;
    bool isValid(uint16_t pid) const;
    bool getMilStatus() const;
    uint8_t getDtcCount() const;

    bool allRequiredPidsReceived() const;
    void reset();

private:
    float   values_[256];
    bool    valid_[256];
    bool    mil_on_;
    uint8_t dtc_count_;
    void*   mutex_;
};
