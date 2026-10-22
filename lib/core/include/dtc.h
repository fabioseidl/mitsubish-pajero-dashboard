#pragma once
// OBD-II diagnostic trouble code decoding (SAE J1979 / ISO 15765-4).
//
// Pure functions over fixed-width types: no Arduino, no CAN driver, no
// allocation. Everything here is testable on the host, and is — see
// test/host/test_lib/test_dtc.cpp.

#include <stdint.h>
#include <stddef.h>

// Where a code was read from. A code can be stored and permanent at once.
enum DtcKind : uint8_t {
    DTC_STORED    = 0,   // Mode 03 — confirmed, MIL-illuminating
    DTC_PENDING   = 1,   // Mode 07 — seen once, not yet confirmed
    DTC_PERMANENT = 2,   // Mode 0A — cannot be cleared by Mode 04
};

// A decoded code: "P0401" plus which list it came from.
struct Dtc {
    char    code[6];   // 5 chars + NUL
    uint8_t kind;      // DtcKind
    uint8_t raw_hi;
    uint8_t raw_lo;
};

/// Decode the two raw DTC bytes into "P0401" form. `out` needs 6 bytes.
/// Bits 15-14 pick the letter, bits 13-12 the first digit, the low 12 bits
/// are three hex nibbles.
inline void dtcToString(uint8_t hi, uint8_t lo, char* out) {
    static const char kLetter[4] = { 'P', 'C', 'B', 'U' };
    static const char kHex[17]   = "0123456789ABCDEF";
    out[0] = kLetter[(hi >> 6) & 0x03u];
    out[1] = (char)('0' + ((hi >> 4) & 0x03u));
    out[2] = kHex[hi & 0x0Fu];
    out[3] = kHex[(lo >> 4) & 0x0Fu];
    out[4] = kHex[lo & 0x0Fu];
    out[5] = '\0';
}

/// True when a DTC word is the all-zero padding ECUs use to fill a frame.
inline bool dtcIsPadding(uint8_t hi, uint8_t lo) {
    return hi == 0u && lo == 0u;
}

/// Parse a Mode 03 / 07 / 0A positive response into `out`.
///
/// `resp` starts at the service byte (0x43, 0x47 or 0x4A). On CAN, J1979 puts a
/// DTC count byte after the service byte — but not every ECU does, so the count
/// is inferred from the payload parity instead of trusted: an odd number of
/// bytes after the service id means the first one is a count.
///
/// Returns the number of codes written, never more than `max_out`.
inline uint8_t dtcParseResponse(const uint8_t* resp, uint8_t resp_len,
                                uint8_t kind, Dtc* out, uint8_t max_out) {
    if (resp == nullptr || out == nullptr || resp_len < 1u) return 0u;

    uint8_t i = 1u;                       // skip the service byte
    uint8_t payload = (uint8_t)(resp_len - 1u);
    if ((payload & 0x01u) != 0u) {        // odd => leading count byte
        i = 2u;
    }

    uint8_t n = 0u;
    while ((uint8_t)(i + 1u) < resp_len && n < max_out) {
        const uint8_t hi = resp[i];
        const uint8_t lo = resp[i + 1u];
        i = (uint8_t)(i + 2u);
        if (dtcIsPadding(hi, lo)) continue;
        dtcToString(hi, lo, out[n].code);
        out[n].kind   = kind;
        out[n].raw_hi = hi;
        out[n].raw_lo = lo;
        ++n;
    }
    return n;
}

// Mode 01 PID 01 (monitor status since DTCs cleared).
struct MonitorStatus {
    bool    mil;             // malfunction indicator lamp commanded on
    uint8_t dtc_count;       // confirmed DTCs stored
    bool    misfire_ready;   // continuous monitors: false = not yet complete
    bool    fuel_ready;
    bool    components_ready;
    bool    misfire_supported;
    bool    fuel_supported;
    bool    components_supported;
};

/// Decode the four bytes of Mode 01 PID 01.
///
/// Only the three continuous monitors in byte B are decoded. The non-continuous
/// bits in C and D mean different things on compression- and spark-ignition
/// engines, and reporting them wrongly is worse than not reporting them.
///
/// Note the readiness bits are inverted on the wire: 1 means *not* complete.
inline MonitorStatus decodeMonitorStatus(uint8_t a, uint8_t b,
                                         uint8_t c, uint8_t d) {
    (void)c;
    (void)d;
    MonitorStatus s{};
    s.mil       = (a & 0x80u) != 0u;
    s.dtc_count = (uint8_t)(a & 0x7Fu);
    s.misfire_supported    = (b & 0x01u) != 0u;
    s.fuel_supported       = (b & 0x02u) != 0u;
    s.components_supported = (b & 0x04u) != 0u;
    s.misfire_ready    = (b & 0x10u) == 0u;
    s.fuel_ready       = (b & 0x20u) == 0u;
    s.components_ready = (b & 0x40u) == 0u;
    return s;
}

/// Short text for a generic SAE code, or nullptr when it is not in the table.
///
/// Generic P0xxx/U0xxx definitions are standardised. Manufacturer-specific
/// codes (P1xxx, and P0xxx in ranges the maker extends) are NOT in this table:
/// the scan still reports the code number, it just carries no description.
/// Codes here lean toward what a 4M41 diesel actually sets.
inline const char* dtcDescribe(const char* code) {
    struct Entry { const char* code; const char* text; };
    static const Entry kTable[] = {
        { "P0087", "Fuel rail pressure too low" },
        { "P0088", "Fuel rail pressure too high" },
        { "P0089", "Fuel pressure regulator performance" },
        { "P0101", "MAF sensor range/performance" },
        { "P0102", "MAF sensor circuit low" },
        { "P0103", "MAF sensor circuit high" },
        { "P0106", "MAP/baro sensor range/performance" },
        { "P0107", "MAP sensor circuit low" },
        { "P0108", "MAP sensor circuit high" },
        { "P0112", "Intake air temp sensor low" },
        { "P0113", "Intake air temp sensor high" },
        { "P0117", "Coolant temp sensor low" },
        { "P0118", "Coolant temp sensor high" },
        { "P0122", "Throttle/pedal position sensor A low" },
        { "P0123", "Throttle/pedal position sensor A high" },
        { "P0128", "Coolant thermostat below regulating temp" },
        { "P0191", "Fuel rail pressure sensor range/performance" },
        { "P0192", "Fuel rail pressure sensor circuit low" },
        { "P0193", "Fuel rail pressure sensor circuit high" },
        { "P0201", "Injector circuit cylinder 1" },
        { "P0202", "Injector circuit cylinder 2" },
        { "P0203", "Injector circuit cylinder 3" },
        { "P0204", "Injector circuit cylinder 4" },
        { "P0217", "Engine over-temperature" },
        { "P0234", "Turbocharger overboost" },
        { "P0235", "Turbo boost sensor A circuit" },
        { "P0238", "Turbo boost sensor A circuit high" },
        { "P0243", "Turbo wastegate solenoid A" },
        { "P0299", "Turbocharger underboost" },
        { "P0301", "Cylinder 1 misfire detected" },
        { "P0302", "Cylinder 2 misfire detected" },
        { "P0303", "Cylinder 3 misfire detected" },
        { "P0304", "Cylinder 4 misfire detected" },
        { "P0335", "Crankshaft position sensor A circuit" },
        { "P0340", "Camshaft position sensor A circuit" },
        { "P0380", "Glow plug circuit A" },
        { "P0401", "EGR flow insufficient" },
        { "P0402", "EGR flow excessive" },
        { "P0403", "EGR control circuit" },
        { "P0404", "EGR control circuit range/performance" },
        { "P0470", "Exhaust pressure sensor" },
        { "P0471", "Exhaust pressure sensor range/performance" },
        { "P0480", "Cooling fan 1 control circuit" },
        { "P0500", "Vehicle speed sensor A" },
        { "P0504", "Brake switch A/B correlation" },
        { "P0560", "System voltage" },
        { "P0562", "System voltage low" },
        { "P0563", "System voltage high" },
        { "P0606", "ECM/PCM processor fault" },
        { "P062F", "Internal control module EEPROM error" },
        { "P0651", "Sensor reference voltage B circuit" },
        { "P0700", "Transmission control system (see TCM)" },
        { "P2002", "DPF efficiency below threshold" },
        { "P2003", "DPF efficiency below threshold bank 2" },
        { "P2031", "Exhaust gas temp sensor circuit" },
        { "P2033", "Exhaust gas temp sensor circuit high" },
        { "P2084", "Exhaust gas temp sensor circuit range" },
        { "P2100", "Throttle actuator control motor circuit" },
        { "P2146", "Injector supply voltage group A" },
        { "P2149", "Injector supply voltage group B" },
        { "P2263", "Turbo boost system performance" },
        { "P242F", "DPF restriction - ash accumulation" },
        { "P2452", "DPF pressure sensor circuit" },
        { "P2453", "DPF pressure sensor range/performance" },
        { "P2455", "DPF pressure sensor circuit high" },
        { "P2459", "DPF regeneration frequency" },
        { "U0001", "High speed CAN communication bus" },
        { "U0019", "Low speed CAN communication bus" },
        { "U0100", "Lost communication with ECM/PCM" },
        { "U0101", "Lost communication with TCM" },
        { "U0121", "Lost communication with ABS module" },
        { "U0155", "Lost communication with instrument cluster" },
    };
    if (code == nullptr) return nullptr;
    for (size_t i = 0; i < sizeof(kTable) / sizeof(kTable[0]); ++i) {
        const char* a = kTable[i].code;
        const char* b = code;
        size_t k = 0;
        while (k < 5u && a[k] != '\0' && a[k] == b[k]) ++k;
        if (k == 5u) return kTable[i].text;
    }
    return nullptr;
}
