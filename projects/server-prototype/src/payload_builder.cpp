#include "payload_builder.h"
#include "pid_map.h"

Payload PayloadBuilder::build(const DataAggregator& aggregator,
                               const SessionAccumulator& session,
                               float consumption,
                               uint32_t timestamp_ms) {
    Payload p;
    p.version                  = PAYLOAD_VERSION;
    p.timestamp_ms             = timestamp_ms;
    p.rpm                      = (uint16_t)aggregator.get(PID_RPM);
    p.speed_kmh                = (uint8_t)aggregator.get(PID_SPEED);
    p.fuel_rate_l_per_h        = aggregator.get(PID_FUEL_RATE);
    p.consumption_km_per_l     = consumption;
    p.avg_consumption_km_per_l = session.getAvgConsumptionKmPerL();
    p.distance_km              = session.getDistanceKm();
    p.mil_on                   = aggregator.getMilStatus();
    p.dtc_count                = aggregator.getDtcCount();

    p.flags = 0;
    if (aggregator.allRequiredPidsReceived()) {
        p.flags |= PAYLOAD_FLAG_DATA_VALID;
    }
    if (aggregator.get(PID_RPM) > 400.0f) {
        p.flags |= PAYLOAD_FLAG_ENGINE_RUNNING;
    }
    return p;
}
