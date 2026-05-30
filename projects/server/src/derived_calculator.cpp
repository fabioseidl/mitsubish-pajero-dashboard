#include "derived_calculator.h"
#include "pid_map.h"
#include <math.h>

// Diesel fuel properties used to derive fuel rate from MAF when PID 0x5E is
// unsupported. Stoichiometric AFR ~14.5:1, density ~835 g/L.
static constexpr float DIESEL_AFR_STOICH    = 14.5f;
static constexpr float DIESEL_DENSITY_G_L   = 835.0f;

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

    // Fallback: estimate from air mass flow and commanded AFR.
    //   fuel_mass (g/s) = MAF (g/s) / (AFR_stoich * lambda)
    //   fuel_rate (L/h) = fuel_mass * 3600 / density (g/L)
    float maf = aggregator.get(PID_MAF);   // g/s
    if (maf <= 0.0f) return 0.0f;

    float lambda = aggregator.get(PID_CMD_AFR);   // commanded AFR as lambda
    if (lambda <= 0.0f) lambda = 1.0f;            // assume stoichiometric if unavailable

    float afr      = DIESEL_AFR_STOICH * lambda;
    float fuel_g_s = maf / afr;
    return fuel_g_s * 3600.0f / DIESEL_DENSITY_G_L;
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
