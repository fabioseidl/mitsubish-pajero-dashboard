#include "simulation_data_generator.h"
#include "payload.h"
#include "pid_map.h"
#include <cmath>
#include <string.h>

SimulationDataGenerator::SimulationDataGenerator(DrivingProfile profile)
    : profile_(profile), elapsed_ms_(0.0f) {}

void SimulationDataGenerator::tick(uint32_t delta_ms) {
    elapsed_ms_ += (float)delta_ms;
    float speed     = computeSpeedKmh();
    float rpm       = computeRpm();
    float fuel_rate = computeFuelRateLPerH(rpm, speed);
    session_.update(speed, fuel_rate, delta_ms);
}

Payload SimulationDataGenerator::getPayload() const {
    float speed     = computeSpeedKmh();
    float rpm       = computeRpm();
    float fuel_rate = computeFuelRateLPerH(rpm, speed);
    float consumption = (speed > 0.0f && fuel_rate > 0.0f) ? speed / fuel_rate : 0.0f;

    Payload p;
    memset(&p, 0, sizeof(p));
    p.version                  = PAYLOAD_VERSION;
    p.timestamp_ms             = (uint32_t)elapsed_ms_;
    p.rpm                      = (uint16_t)rpm;
    p.speed_kmh                = (uint8_t)speed;
    p.fuel_rate_l_per_h        = fuel_rate;
    p.consumption_km_per_l     = consumption;
    p.avg_consumption_km_per_l = session_.getAvgConsumptionKmPerL();
    p.distance_km              = session_.getDistanceKm();
    p.mil_on                   = false;
    p.dtc_count                = 0;
    p.flags                    = PAYLOAD_FLAG_DATA_VALID | PAYLOAD_FLAG_ENGINE_RUNNING;
    return p;
}

void SimulationDataGenerator::setProfile(DrivingProfile profile) {
    profile_ = profile;
}

float SimulationDataGenerator::computeRpm() const {
    float t = elapsed_ms_ / 1000.0f;
    switch (profile_) {
        case DrivingProfile::IDLE:
            return 800.0f + 50.0f * sinf(t * 0.2f);
        case DrivingProfile::CITY:
            return 1500.0f + 1000.0f * sinf(t * 0.3f);
        case DrivingProfile::HIGHWAY:
            return 2000.0f + 250.0f * sinf(t * 0.1f);
    }
    return 800.0f;
}

float SimulationDataGenerator::computeSpeedKmh() const {
    float t = elapsed_ms_ / 1000.0f;
    switch (profile_) {
        case DrivingProfile::IDLE:
            return 0.0f;
        case DrivingProfile::CITY:
            return 30.0f + 30.0f * (0.5f + 0.5f * sinf(t * 0.3f));
        case DrivingProfile::HIGHWAY:
            return 100.0f + 10.0f * sinf(t * 0.1f);
    }
    return 0.0f;
}

float SimulationDataGenerator::computeFuelRateLPerH(float /*rpm*/, float speed) const {
    switch (profile_) {
        case DrivingProfile::IDLE:
            return 0.8f;
        case DrivingProfile::CITY:
            return 1.0f + (speed / 10.0f);
        case DrivingProfile::HIGHWAY:
            return 6.0f + (speed - 100.0f) * 0.05f;
    }
    return 0.8f;
}
