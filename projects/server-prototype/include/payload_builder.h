#pragma once

#include <stdint.h>
#include "payload.h"
#include "data_aggregator.h"
#include "session_accumulator.h"

class PayloadBuilder {
public:
    static Payload build(const DataAggregator& aggregator,
                         const SessionAccumulator& session,
                         float consumption,
                         uint32_t timestamp_ms);
};
