#include "derived_calculator.h"
#include "pid_map.h"
#include <math.h>

// Deriving diesel fuel rate from MAF (used when the direct fuel-rate PID 0x5E
// is unsupported).
//
// A diesel's air-fuel ratio swings hugely with load: very lean at idle (lambda
// ~7-8, AFR ~110:1) and rich under full load (AFR ~22:1). A single constant AFR
// therefore can't be right everywhere — tuned for cruise it over-reads idle by
// ~3x, tuned for idle it under-reads cruise. So we model the *effective* AFR as
// a line through two calibration points versus MAF (g/s of air), clamped at the
// rich end:
//        AFR(MAF) = AFR_IDLE + (AFR_CRUISE - AFR_IDLE) * (MAF - MAF_IDLE)
//                                                       / (MAF_CRUISE - MAF_IDLE)
//   fuel(g/s) = MAF / AFR(MAF) ;  fuel(L/h) = fuel(g/s) * 3600 / density
//
// CALIBRATION (tune against the server log: each TX line prints maf= and fuel=):
//   • Warm idle:  set MAF_IDLE to the logged idle MAF, AFR_IDLE so fuel reads
//     your known idle figure (~0.85 L/h). Raise AFR_IDLE to lower idle fuel.
//   • Steady cruise: set MAF_CRUISE to a logged cruise MAF and AFR_CRUISE so the
//     long-run AVG CONS matches your fuel-receipt economy. Raise it to lower fuel.
// AFR_RICH_CLAMP caps the richest (full-load) ratio so high MAF can't run away.
static constexpr float MAF_IDLE        = 22.0f;    // g/s air at warm idle
static constexpr float AFR_IDLE        = 110.0f;   // very lean at idle
static constexpr float MAF_CRUISE      = 60.0f;    // g/s air at steady cruise
static constexpr float AFR_CRUISE      = 37.0f;    // leaner-cruise AFR
static constexpr float AFR_RICH_CLAMP  = 22.0f;    // richest AFR (full load)
static constexpr float DIESEL_DENSITY_G_L = 835.0f;

// Sea-level reference pressure for the barometric altitude formula.
//
// CALIBRATION KNOB. The ISA standard value is 101.325 kPa, but actual sea-level
// pressure varies with weather (typ. ~98–104 kPa), which directly offsets the
// computed altitude. Tune this to your region/day so a known altitude reads
// correctly: raise it if altitude reads too low, lower it if it reads too high.
// (~0.1 kPa ≈ 8 m.) Measured locally: OBD baro ≈ 102 kPa near sea level, so the
// default is set to 102.0 to put that point near 0 m instead of −56 m.
//
// NOTE: OBD PID 0x33 reports whole kPa only (~84 m per step), so absolute
// altitude is inherently coarse no matter how this is calibrated — the EMA in
// the broadcast loop smooths the stepping but cannot add real resolution.
static constexpr float ALTITUDE_SEA_LEVEL_REF_KPA = 102.0f;

float DerivedCalculator::computeFuelRate(const DataAggregator& aggregator) {
    // Use the direct reading when the ECU actually answers PID 0x5E.
    float direct = aggregator.get(PID_FUEL_RATE);
    if (direct > 0.0f) return direct;

    // Fallback: estimate from air mass flow using a load-dependent diesel AFR.
    // NB: commanded AFR (PID 0x44) is intentionally NOT used here — it is
    // unsupported on this vehicle and would peg lambda at 1.0, producing a
    // stoichiometric (gasoline-like) over-estimate.
    float maf = aggregator.get(PID_MAF);   // g/s
    if (maf <= 0.0f) return 0.0f;

    // Effective AFR interpolated between the idle and cruise calibration points,
    // then clamped so it never runs leaner than idle or richer than full load.
    float afr = AFR_IDLE + (AFR_CRUISE - AFR_IDLE)
                           * (maf - MAF_IDLE) / (MAF_CRUISE - MAF_IDLE);
    if (afr > AFR_IDLE)       afr = AFR_IDLE;
    if (afr < AFR_RICH_CLAMP) afr = AFR_RICH_CLAMP;

    return maf * 3600.0f / (afr * DIESEL_DENSITY_G_L);
}

float DerivedCalculator::computeConsumption(const DataAggregator& aggregator) {
    float speed     = aggregator.get(PID_SPEED);
    float fuel_rate = computeFuelRate(aggregator);
    if (speed <= 0.0f || fuel_rate <= 0.0f) return 0.0f;
    return speed / fuel_rate;
}

float DerivedCalculator::computeAltitude(float baro_kpa) {
    // International barometric formula (troposphere):
    //   h = 44330 * (1 - (P / P0)^(1/5.255))
    if (baro_kpa <= 0.0f) return 0.0f;
    return 44330.0f * (1.0f - powf(baro_kpa / ALTITUDE_SEA_LEVEL_REF_KPA, 1.0f / 5.255f));
}
