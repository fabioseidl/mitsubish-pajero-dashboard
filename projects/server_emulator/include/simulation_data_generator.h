#pragma once

#include <stdint.h>
#include "payload.h"
#include "session_accumulator.h"

enum class DrivingProfile {
    IDLE,
    CITY,
    HIGHWAY,
};

class SimulationDataGenerator {
public:
    explicit SimulationDataGenerator(DrivingProfile profile = DrivingProfile::CITY);

    void tick(uint32_t delta_ms);
    Payload getPayload() const;
    void setProfile(DrivingProfile profile);

private:
    DrivingProfile     profile_;
    float              elapsed_ms_;
    SessionAccumulator session_;

    float computeRpm() const;
    float computeSpeedKmh() const;
    float computeFuelRateLPerH(float rpm, float speed) const;
};
