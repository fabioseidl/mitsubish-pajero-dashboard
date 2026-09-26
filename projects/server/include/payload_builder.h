#pragma once

#include <stdint.h>
#include "payload.h"
#include "data_aggregator.h"
#include "session_accumulator.h"

/** Assembles the broadcast Payload from the aggregator, session totals and derived values. */
class PayloadBuilder {
public:
    // Onboard sensor readings older than this are broadcast as NAN (sensor unplugged or hung).
    static constexpr uint32_t SENSOR_STALE_MS = 3000;

    /** Build one Payload snapshot stamped with timestamp_ms. */
    static Payload build(const DataAggregator& aggregator,
                         const SessionAccumulator& session,
                         float consumption,
                         uint32_t timestamp_ms);

private:
    /** Fresh value of an onboard sensor slot, or NAN when absent/stale. */
    static float sensorValue(const DataAggregator& aggregator, uint16_t slot);
};
