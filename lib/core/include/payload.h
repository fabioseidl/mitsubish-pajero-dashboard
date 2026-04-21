#pragma once

#include <stdint.h>
#include <stdbool.h>

#define PAYLOAD_VERSION 1

typedef struct __attribute__((packed)) {
    uint8_t  version;
    uint32_t timestamp_ms;

    uint16_t rpm;
    uint8_t  speed_kmh;
    float    fuel_rate_l_per_h;
    float    consumption_km_per_l;
    float    avg_consumption_km_per_l;
    float    distance_km;

    bool     mil_on;
    uint8_t  dtc_count;

    uint8_t  flags;
} Payload;

#define PAYLOAD_FLAG_DATA_VALID     (1 << 0)
#define PAYLOAD_FLAG_ENGINE_RUNNING (1 << 1)

static_assert(sizeof(Payload) == 27,
    "Payload size mismatch - check struct fields and packing");
