#pragma once

#include <stdint.h>

class DataAggregator {
public:
    DataAggregator();
    ~DataAggregator();

    // Advance the aggregator's clock. update() stamps each value with whatever
    // was last set here, and isFresh() measures age against it. Injected rather
    // than read from a system call so the whole class stays host-testable; the
    // server calls this once per CAN-loop iteration. Left at 0 (as the host tests
    // leave it) every stored value has age 0, i.e. always fresh.
    void setNow(uint32_t now_ms);

    void update(uint16_t pid, float value);
    void updateMilStatus(bool mil_on);
    void updateDtcCount(uint8_t count);

    float get(uint16_t pid) const;
    // "A value was received at some point since boot." Latches true forever.
    bool isValid(uint16_t pid) const;

    // "A value was received, AND it is no older than max_age_ms." This is the
    // one to use for a decision: valid_ alone cannot tell a live reading from one
    // the ECU stopped answering ten minutes ago, and the server has no way to
    // notice a PID going quiet — it just keeps re-broadcasting the last value.
    bool isFresh(uint16_t pid, uint32_t max_age_ms) const;

    // Age in ms of the stored value, or UINT32_MAX if never received.
    uint32_t ageMs(uint16_t pid) const;
    bool getMilStatus() const;
    uint8_t getDtcCount() const;

    bool allRequiredPidsReceived() const;
    void reset();

private:
    float    values_[256];
    bool     valid_[256];
    uint32_t last_update_ms_[256];
    uint32_t now_ms_;
    bool    mil_on_;
    uint8_t dtc_count_;
    void*   mutex_;
};
