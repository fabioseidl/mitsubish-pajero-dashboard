// CAN sniffer / UDS discovery tool — runs on the SAME board as projects/server
// (ESP32-S3 + MCP2515 over SPI, CAN A = the OBD-II port).
//
// Modes (switch live over the serial monitor):
//   PASSIVE (default) — MCP2515 in listen-only mode; stays QUIET on the console
//                       until DUMP is enabled, then logs every frame in candump
//                       format `(sec.usec) can0 ID#DATA`.
//   WATCH           — listen-only like PASSIVE, but decodes specific free-running
//                     broadcast frames (default 0x608 diesel fuel-quantity and
//                     0x218 transmission/gear) into engineering values, rate-
//                     limited so the fast frames don't flood the console.
//   DIFF            — listen-only field isolator for ONE noisy frame: learns the
//                     bytes that churn while an input is held still, then prints
//                     only when a previously-stable byte changes (used to pin down
//                     the 0x218 gear nibble against its counter/toggle bytes).
//   ADIFF           — listen-only DIFF across the WHOLE bus at once: learns the
//                     stable bytes of every ID while everything is held still, then
//                     prints any stable byte on any ID that later changes. Finds an
//                     UNKNOWN discrete-state frame (e.g. 4WD mode dial, diff lock)
//                     in one toggle sweep without knowing its ID in advance.
//   SCAN            — sweeps UDS Mode 22 DIDs on an ECU and logs every positive
//                     (0x62) response; used to discover undocumented advanced
//                     PIDs such as the diesel injected-fuel-quantity channel.
//   SIG             — polls one DID at 10 Hz alongside rpm / accelerator / speed
//                     and prints a CSV row each cycle, so you can correlate a
//                     candidate value with throttle and overrun while driving.
//   FUELLOG         — correlates the passive 0x608 fuel broadcast (and 0x218
//                     gear) with actively-polled rpm / accelerator / speed, one
//                     CSV row per cycle — the drive test that confirms 0x608 is
//                     real injected fuel (drops to ~0 on overrun) so it can
//                     replace the MAF-based estimate in derived_calculator.cpp.
//
// SCAN, SIG, FUELLOG and RDLI transmit, so they put the MCP2515 into NORMAL mode
// (the node then ACKs bus traffic); PASSIVE and WATCH stay listen-only.
//
// Serial commands (one per line):
//   T<unix>                 set wall-clock base for timestamps (e.g. T1716394391)
//   DUMP                    toggle passive candump printing on/off (default off)
//   WATCH [id ...]          decode broadcast frames (hex IDs); no args = 608 218
//   DIFF [id]               isolate a slow field in one frame; no arg = 218 (gear)
//   ADIFF                   whole-bus DIFF: hold still to learn, then toggle an
//                           input; the changed ID/byte is printed (unknown frames)
//   OBD                     list standard Mode 01 PIDs the engine ECU supports
//   SCAN [eng|tcm] [s e] [sess]  sweep DIDs s..e (hex) on an ECU; defaults: both,
//                                2000 21FF. `sess` opens an extended diagnostic
//                                session (10 03) first and holds it with
//                                TesterPresent — for ECUs that gate Mode 22.
//   SIG <req> <did>         signature-log one DID, e.g. `SIG 7E0 2100`
//   FUELLOG                 CSV: 0x608 fuel + 0x218 gear vs rpm/accel/speed
//   RDLI <req> <lid>        KWP ReadDataByLocalIdentifier (service 0x21, 1-byte
//                           LID) — tests igkov bcomp11's Pajero AT/odometer reads
//                           (e.g. RDLI 7E1 02 = AT info, RDLI 7E1 03 = odometer)
//   STOP / PASV             stop SIG/SCAN/WATCH/DIFF/FUELLOG; return to passive logging

#include <Arduino.h>
#include <SPI.h>
#include <mcp2515.h>
#include "pid_map.h"   // shared PID_MAP / MODE22_ADVANCED_PIDS (lib/core/include)

// --- CAN hardware: identical wiring to projects/server (include/pin_config.h) ---
static constexpr int PIN_SPI_SCK  = 12;
static constexpr int PIN_SPI_MISO = 13;
static constexpr int PIN_SPI_MOSI = 11;
static constexpr int PIN_MCP2515_CS  = 10;   // CAN A — OBD-II port
static constexpr int PIN_MCP2515_RST =  9;
static constexpr int PIN_MCP2515_INT =  8;   // unused here (polled), kept for parity

static const uint32_t USB_BAUD = 115200;

// UDS physical request IDs (response = request + 8) come from pid_map.h:
//   UDS_REQ_ENGINE (0x7E0), UDS_REQ_TCM (0x7E1), UDS_RESP_OFFSET (0x008).

static MCP2515 mcp2515(PIN_MCP2515_CS, 10000000, &SPI);

static uint64_t unix_base_us   = 0;   // Unix time in microseconds at sync point
static uint64_t micros_at_sync = 0;   // micros() value when sync was received

enum class Mode { PASSIVE, WATCH, DIFF, ADIFF, SIG, FUELLOG };
static Mode     g_mode = Mode::PASSIVE;
static uint16_t g_sig_req = UDS_REQ_ENGINE;
static uint16_t g_sig_did = 0x0000;
static bool     g_dump    = false;   // passive candump printing; off until DUMP

// --- Broadcast frames of interest (igkov bcomp11 reverse-engineering) ---
// These are FREE-RUNNING frames already on this truck's bus (confirmed in
// dumps/candump.log: 0x608 ~5-10 Hz, 0x218 ~50 Hz), so WATCH/FUELLOG decode them
// without transmitting anything.
//   0x608 — diesel injected fuel-quantity broadcast. bcomp11 reads (D5<<8 | D6)
//           as the raw injection figure. WATCH captures confirm D5,D6 are the ONLY
//           moving bytes (rest constant 4D 00 FF BF FF .. .. 00); the value idles
//           ~340, peaks ~999 (≈10-bit) under load. D0 is a separate slow byte
//           (constant within a drive, ~0x4D; possibly a temperature), logged for
//           context — it is NOT load.
//   0x218 — transmission gear broadcast. D2 packs target gear (high nibble) and
//           current gear (low nibble); equal at rest, differ mid-shift. Codes
//           confirmed by a selector sweep: 0x0=N, 0x1..0x5=gears, 0xB=R, 0xD=P
//           (see gearName()). bcomp11's D2 & 0x0F = the current gear/state.
// If 0x608 really is injected fuel it should collapse to ~0 on overrun (closed
// throttle, elevated rpm) — exactly what FUELLOG checks — letting it replace the
// MAF→AFR estimate in projects/server/src/derived_calculator.cpp.
static constexpr uint16_t BCAST_FUEL = 0x608;
static constexpr uint16_t BCAST_GEAR = 0x218;

struct WatchId {
    uint16_t id;
    uint32_t last_ms;    // last time this id was printed (rate limiter)
    int32_t  last_val;   // last decoded headline value (change detector)
};
static constexpr uint8_t  WATCH_MAX = 8;
static WatchId  g_watch[WATCH_MAX] = { { BCAST_FUEL, 0, INT32_MIN },
                                       { BCAST_GEAR, 0, INT32_MIN } };
static uint8_t  g_watch_n = 2;
static constexpr uint32_t WATCH_MIN_INTERVAL_MS = 250;   // per-id print throttle

// Latest decoded broadcast values, refreshed from the bus each FUELLOG tick.
static int32_t g_fl_fuel = -1;   // (D5<<8|D6) from 0x608, -1 = not seen yet
static uint8_t g_fl_fuel_d0 = 0; // D0 of 0x608 (context)
static int8_t  g_fl_gear = -1;   // D2&0x0F from 0x218, -1 = not seen yet

// --- DIFF mode: isolate a slowly-changing field in one noisy broadcast frame ---
// Many status frames (e.g. 0x218) mix the field of interest with bytes that churn
// every frame (counters, alive-toggle bits). DIFF first LEARNS which bytes move
// while you hold the input still (selector in P), marks them volatile/ignored,
// then prints only when a previously-STABLE byte changes — so moving the gear
// selector P→R→N→D makes the gear byte stand out from the noise.
static constexpr uint32_t DIFF_LEARN_MS = 3000;
static uint16_t g_diff_id    = BCAST_GEAR;
static uint8_t  g_diff_last[8];
static uint8_t  g_diff_dlc   = 0;
static bool     g_diff_have  = false;        // g_diff_last holds a frame yet?
static bool     g_diff_vol[8] = { false };   // byte marked noisy during learn → ignored
static bool     g_diff_learning = true;
static uint32_t g_diff_learn_start = 0;

// --- ADIFF mode: DIFF across every ID on the bus at once -------------------
// Same idea as DIFF, but instead of tracking one known frame it keeps a small
// per-ID table. During the learn window it records each ID's bytes and marks any
// byte that churns (counters, alive bits) as volatile; afterwards it prints when a
// previously-stable byte on ANY ID changes. Used to FIND an unknown discrete-state
// frame (4WD mode dial, diff lock, handbrake...) by toggling the input once.
static constexpr uint8_t  ADIFF_MAX = 64;        // distinct IDs tracked (bus has ~14)
struct AdiffEntry {
    uint16_t id;
    uint8_t  last[8];
    uint8_t  dlc;
    bool     vol[8];     // byte churned during learn → ignored afterwards
};
static AdiffEntry g_adiff[ADIFF_MAX];
static uint8_t    g_adiff_n = 0;
static bool       g_adiff_learning = true;
static uint32_t   g_adiff_learn_start = 0;

// Find the table slot for an ID, allocating a new one (with the current frame as
// its baseline) on first sight. Returns -1 only if the table is full.
static int adiffSlot(uint16_t id, const uint8_t* data, uint8_t dlc) {
    for (uint8_t i = 0; i < g_adiff_n; ++i) if (g_adiff[i].id == id) return i;
    if (g_adiff_n >= ADIFF_MAX) return -1;
    AdiffEntry& e = g_adiff[g_adiff_n];
    e.id = id; e.dlc = dlc;
    memcpy(e.last, data, 8);
    for (int k = 0; k < 8; ++k) e.vol[k] = false;
    return g_adiff_n++;
}

// ---------------------------------------------------------------------------
// MCP2515 bring-up / mode switching
// ---------------------------------------------------------------------------
static void canReset() {
    pinMode(PIN_MCP2515_RST, OUTPUT);
    digitalWrite(PIN_MCP2515_RST, HIGH); delay(50);
    digitalWrite(PIN_MCP2515_RST, LOW);  delay(50);
    digitalWrite(PIN_MCP2515_RST, HIGH); delay(50);
    SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, PIN_MCP2515_CS);
    mcp2515.reset();
    mcp2515.setBitrate(CAN_500KBPS);
}

static void enterPassive() {
    mcp2515.setListenOnlyMode();   // never ACKs / transmits — safe passive tap
    g_mode = Mode::PASSIVE;
    Serial.println("# mode=PASSIVE (listen-only)");
}

static void enterActive() {
    mcp2515.setNormalMode();       // real node: can transmit and ACKs the bus
}

static void drainRx() {
    struct can_frame m;
    while (mcp2515.readMessage(&m) == MCP2515::ERROR_OK) { /* discard */ }
}

// ---------------------------------------------------------------------------
// Timestamps
// ---------------------------------------------------------------------------
static uint64_t currentTimestampUs() {
    if (unix_base_us == 0) return micros();   // fallback: time since boot
    return unix_base_us + (micros() - micros_at_sync);
}

// ---------------------------------------------------------------------------
// Generic ISO-TP request/response (single + multi frame). Sends `payload` as a
// single-frame request and reassembles the reply from (req_id + 8).
//   payload[0] is the service byte (0x22 ReadDataByIdentifier, 0x21 KWP
//   ReadDataByLocalIdentifier, ...); the rest is the DID/LID.
// Returns  1 = positive response (out[] = reassembled response starting at SID),
//          0 = negative response (0x7F); out[] = {0x7F, service, NRC}, out_len=3,
//         -1 = no/garbled response within timeout.
// NRC 0x78 (responsePending) is handled internally: the timeout is restarted and
// we keep waiting for the real answer instead of reporting it as negative.
// ---------------------------------------------------------------------------
static int isotpTransceive(uint16_t req_id, const uint8_t* payload, uint8_t plen,
                           uint8_t* out, uint8_t& out_len, uint32_t timeout_ms) {
    const uint16_t resp_id = req_id + UDS_RESP_OFFSET;
    const uint8_t  service = plen > 0 ? payload[0] : 0x00;
    drainRx();

    struct can_frame req = {};
    req.can_id  = req_id;
    req.can_dlc = 8;
    req.data[0] = plen & 0x0F;     // ISO-TP single frame, length = plen
    for (uint8_t i = 0; i < plen && i < 7; ++i) req.data[1 + i] = payload[i];
    if (mcp2515.sendMessage(&req) != MCP2515::ERROR_OK) return -1;

    uint8_t  buf[64];
    uint16_t total = 0, got = 0;
    bool     assembling = false;
    uint32_t start = millis();

    while (millis() - start < timeout_ms) {
        struct can_frame m;
        if (mcp2515.readMessage(&m) != MCP2515::ERROR_OK) { delayMicroseconds(200); continue; }
        if ((m.can_id & 0x7FF) != resp_id) continue;

        uint8_t pci = m.data[0] & 0xF0;
        if (pci == 0x00) {                          // single frame
            uint8_t len = m.data[0] & 0x0F;
            for (uint8_t k = 0; k < len && (uint8_t)(1 + k) < 8; ++k) buf[k] = m.data[1 + k];
            if (len >= 1 && buf[0] == 0x7F) {
                uint8_t nrc = len >= 3 ? buf[2] : 0xFF;
                if (nrc == 0x78) { start = millis(); continue; }   // responsePending: keep waiting
                out[0] = 0x7F; out[1] = len >= 2 ? buf[1] : service; out[2] = nrc;
                out_len = 3; return 0;
            }
            uint8_t n = len < sizeof(buf) ? len : (uint8_t)sizeof(buf);
            memcpy(out, buf, n); out_len = n; return 1;
        } else if (pci == 0x10) {                   // first frame
            total = (((uint16_t)(m.data[0] & 0x0F)) << 8) | m.data[1];
            got = 0;
            for (uint8_t k = 0; k < 6 && got < total && got < sizeof(buf); ++k) buf[got++] = m.data[2 + k];
            struct can_frame fc = {};               // flow control: clear to send
            fc.can_id = req_id; fc.can_dlc = 8; fc.data[0] = 0x30;
            mcp2515.sendMessage(&fc);
            assembling = true;
        } else if (pci == 0x20) {                   // consecutive frame
            if (!assembling) continue;
            for (uint8_t k = 0; k < 7 && got < total && got < sizeof(buf); ++k) buf[got++] = m.data[1 + k];
            if (got >= total) {
                if (total >= 1 && buf[0] == 0x7F) {
                    out[0] = 0x7F; out[1] = total >= 2 ? buf[1] : service;
                    out[2] = total >= 3 ? buf[2] : 0xFF; out_len = 3; return 0;
                }
                uint8_t n = total < sizeof(buf) ? (uint8_t)total : (uint8_t)sizeof(buf);
                memcpy(out, buf, n); out_len = n; return 1;
            }
        }
    }
    return -1;
}

// UDS Service 0x22 (ReadDataByIdentifier), 2-byte DID. Positive out[] = 62 DIDhi
// DIDlo D0 D1 ... (so D0 = out[3], the Torque 'A' byte).
static int udsRead(uint16_t req_id, uint16_t did,
                   uint8_t* out, uint8_t& out_len, uint32_t timeout_ms = 150) {
    const uint8_t payload[3] = { 0x22, (uint8_t)(did >> 8), (uint8_t)(did & 0xFF) };
    return isotpTransceive(req_id, payload, 3, out, out_len, timeout_ms);
}

// KWP Service 0x21 (ReadDataByLocalIdentifier), 1-byte LID. This is the legacy
// read bcomp11 uses for the Pajero TCM (request "02 21 LID"). Positive out[] =
// 61 LID D0 D1 ... — note only ONE echo byte, so D0 = out[2] (not out[3]).
static int kwpRead21(uint16_t req_id, uint8_t lid,
                     uint8_t* out, uint8_t& out_len, uint32_t timeout_ms = 200) {
    const uint8_t payload[2] = { 0x21, lid };
    return isotpTransceive(req_id, payload, 2, out, out_len, timeout_ms);
}

// Read a standard Mode 01 PID from the engine ECU; returns the first two data
// bytes (A,B). Used by SIG to log rpm / pedal / speed next to the candidate DID.
static bool obd01(uint8_t pid, uint8_t& a, uint8_t& b, uint32_t timeout_ms = 60) {
    drainRx();
    struct can_frame req = {};
    req.can_id = 0x7DF; req.can_dlc = 8;
    req.data[0] = 0x02; req.data[1] = 0x01; req.data[2] = pid;
    if (mcp2515.sendMessage(&req) != MCP2515::ERROR_OK) return false;

    uint32_t start = millis();
    while (millis() - start < timeout_ms) {
        struct can_frame m;
        if (mcp2515.readMessage(&m) != MCP2515::ERROR_OK) { delayMicroseconds(200); continue; }
        if ((m.can_id & 0x7FF) != 0x7E8) continue;
        if (m.data[1] == 0x41 && m.data[2] == pid) { a = m.data[3]; b = m.data[4]; return true; }
    }
    return false;
}

// Send a short UDS single-frame request and check for the expected positive SID.
// Returns 1 = positive (data[1] == sid_pos), 0 = negative (out_nrc set), -1 = none.
static int udsServiceSF(uint16_t req_id, const uint8_t* payload, uint8_t n,
                        uint8_t sid_pos, uint8_t& out_nrc, uint32_t timeout_ms = 200) {
    const uint16_t resp_id = req_id + UDS_RESP_OFFSET;
    drainRx();
    struct can_frame req = {};
    req.can_id = req_id; req.can_dlc = 8;
    req.data[0] = n;                                   // ISO-TP single frame, length n
    for (uint8_t i = 0; i < n && i < 7; ++i) req.data[1 + i] = payload[i];
    if (mcp2515.sendMessage(&req) != MCP2515::ERROR_OK) return -1;

    uint32_t start = millis();
    while (millis() - start < timeout_ms) {
        struct can_frame m;
        if (mcp2515.readMessage(&m) != MCP2515::ERROR_OK) { delayMicroseconds(200); continue; }
        if ((m.can_id & 0x7FF) != resp_id) continue;
        if ((m.data[0] & 0xF0) != 0x00) continue;       // only single-frame replies here
        if (m.data[1] == sid_pos) return 1;
        if (m.data[1] == 0x7F) {
            if (m.data[3] == 0x78) { start = millis(); continue; }   // responsePending
            out_nrc = m.data[3]; return 0;
        }
    }
    return -1;
}

// DiagnosticSessionControl (0x10). sub 0x03 = extended diagnostic session.
static int udsSession(uint16_t req_id, uint8_t sub, uint8_t& out_nrc) {
    uint8_t p[2] = {0x10, sub};
    return udsServiceSF(req_id, p, 2, 0x50, out_nrc);
}

// TesterPresent (0x3E 0x00) — keeps an open session from timing out mid-scan.
static void udsTesterPresent(uint16_t req_id) {
    uint8_t p[2] = {0x3E, 0x00}; uint8_t nrc = 0;
    udsServiceSF(req_id, p, 2, 0x7E, nrc, 40);   // best-effort, short timeout
}

// Read a standard Mode 01 PID, copying up to 4 data bytes (A,B,C,D) into d[].
// Returns 1 on a positive 0x41 response, -1 otherwise.
static int obd01n(uint8_t pid, uint8_t* d, uint32_t timeout_ms = 80) {
    drainRx();
    struct can_frame req = {};
    req.can_id = 0x7DF; req.can_dlc = 8;
    req.data[0] = 0x02; req.data[1] = 0x01; req.data[2] = pid;
    if (mcp2515.sendMessage(&req) != MCP2515::ERROR_OK) return -1;

    uint32_t start = millis();
    while (millis() - start < timeout_ms) {
        struct can_frame m;
        if (mcp2515.readMessage(&m) != MCP2515::ERROR_OK) { delayMicroseconds(200); continue; }
        if ((m.can_id & 0x7FF) != 0x7E8) continue;
        if (m.data[1] == 0x41 && m.data[2] == pid) {
            for (int i = 0; i < 4; ++i) d[i] = m.data[3 + i];
            return 1;
        }
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Decode a standard Mode 01 response using a PidDefinition's linear parameters.
// Mirrors PIDTranslator for FORMULA_LINEAR: value = (A*a_mult + B*b_mult)*scale
// + offset, using the first two data bytes. Non-linear formulas (BITMASK/COMPLEX)
// aren't decoded here — VERIFY just reports the raw bytes for those.
// ---------------------------------------------------------------------------
static float decodeLinear(const PidDefinition& p, const uint8_t* d) {
    float raw = d[0] * p.a_mult + d[1] * p.b_mult;
    return raw * p.scale + p.offset;
}

// ---------------------------------------------------------------------------
// VERIFY — poll every PID currently marked `verified = false` and report whether
// this truck actually answers. Drives straight off the shared tables so it stays
// in lock-step with lib/core/include/pid_map.h (never hardcode PID values).
//   • Mode 01 unverified PIDs  → 0x01 request on 0x7DF, decoded with the table's
//     linear formula (raw bytes for BITMASK/COMPLEX).
//   • Mode 22 advanced PIDs    → UDS 0x22 to the listed ECU, with the data byte
//     the formula uses highlighted.
// Run engine-running. A PID that answers here is a candidate to flip to
// `verified = true`; one that always returns NEG/NA on this vehicle stays false.
// ---------------------------------------------------------------------------
static void verifyMode01() {
    Serial.println("# VERIFY Mode 01 (unverified PIDs in PID_MAP)");
    uint16_t ok = 0, total = 0;
    for (size_t i = 0; i < PID_MAP_SIZE; ++i) {
        const PidDefinition& p = PID_MAP[i];
        if (p.verified) continue;            // only the unconfirmed ones
        if (p.pid > 0xFF) continue;          // Mode 22 slot IDs live in MODE22 table
        ++total;
        uint8_t d[4] = {0};
        int r = obd01n((uint8_t)p.pid, d);
        if (r == 1) {
            Serial.printf("POS PID=0x%02X %-28s raw=%02X %02X %02X %02X",
                          (unsigned)p.pid, p.name, d[0], d[1], d[2], d[3]);
            if (p.formula_type == FORMULA_LINEAR)
                Serial.printf("  = %.2f %s", decodeLinear(p, d), p.unit);
            Serial.println();
            ++ok;
        } else {
            Serial.printf("NA  PID=0x%02X %-28s (no response)\n", (unsigned)p.pid, p.name);
        }
        delay(20);
    }
    Serial.printf("# VERIFY Mode 01 done: %u/%u answered\n", ok, total);
}

static void verifyMode22() {
    Serial.println("# VERIFY Mode 22 advanced PIDs (MODE22_ADVANCED_PIDS)");
    uint16_t ok = 0, total = 0;
    for (size_t i = 0; i < MODE22_ADVANCED_PIDS_SIZE; ++i) {
        const Mode22AdvancedPid& m = MODE22_ADVANCED_PIDS[i];
        if (m.verified) continue;
        ++total;
        uint8_t buf[64]; uint8_t len = 0;
        int r = udsRead(m.req_id, m.did, buf, len);
        if (r == 1) {
            Serial.printf("POS ECU=0x%03X DID=0x%04X %-20s D0..=", m.req_id, m.did, m.name);
            for (uint8_t k = 3; k < len; ++k) Serial.printf("%02X ", buf[k]);   // data after 62 DIDhi DIDlo
            // highlight the byte(s) the formula consumes (data_index = index of D0)
            uint8_t di = 3 + m.data_index;                                       // absolute index of D0..
            if (di < len) Serial.printf(" [D%u=%02X]", m.data_index, buf[di]);
            Serial.printf(" -> %s\n", m.unit);
            ++ok;
        } else if (r == 0) {
            Serial.printf("NEG ECU=0x%03X DID=0x%04X %-20s nrc=0x%02X\n",
                          m.req_id, m.did, m.name, buf[2]);
        } else {
            Serial.printf("NA  ECU=0x%03X DID=0x%04X %-20s (no response)\n",
                          m.req_id, m.did, m.name);
        }
        delay(25);
    }
    Serial.printf("# VERIFY Mode 22 done: %u/%u answered\n", ok, total);
}

// ---------------------------------------------------------------------------
// OBD — enumerate the standard Mode 01 PIDs the engine ECU actually supports,
// by walking the "supported PID" bitmaps (0x00, 0x20, 0x40, ...). This is the
// path that works on this truck (Mode 01), unlike manufacturer UDS 0x22.
// ---------------------------------------------------------------------------
static void obdScan() {
    Serial.println("# OBD Mode 01 supported-PID scan");
    static const uint8_t bases[] = {0x00, 0x20, 0x40, 0x60, 0x80, 0xA0, 0xC0};
    for (uint8_t bi = 0; bi < sizeof(bases); ++bi) {
        uint8_t base = bases[bi];
        uint8_t d[4] = {0};
        if (obd01n(base, d) != 1) {
            Serial.printf("# bitmap PID 0x%02X: no response — stop\n", base);
            break;
        }
        uint32_t bits = ((uint32_t)d[0] << 24) | ((uint32_t)d[1] << 16) |
                        ((uint32_t)d[2] << 8)  |  (uint32_t)d[3];
        for (int i = 0; i < 32; ++i) {
            if (bits & (1UL << (31 - i))) Serial.printf("PID 0x%02X supported\n", base + i + 1);
        }
        if (!(bits & 0x01)) break;   // LSB = "next bitmap (base+0x20) supported"
        delay(20);
    }
    Serial.println("# OBD scan done");
}

// ---------------------------------------------------------------------------
// SCAN — sweep a DID range on one ECU. Prints every positive (0x62) response,
// the first occurrence of each distinct NRC, and an NRC histogram at the end
// (so a whole-range rejection shows up as one summary line, not 512 of them).
// open_session = send 0x10 0x03 first and hold it with TesterPresent during the
// sweep, for ECUs that gate Mode 22 behind an extended diagnostic session.
// ---------------------------------------------------------------------------
static void scanEcu(uint16_t req_id, uint16_t start_did, uint16_t end_did, bool open_session) {
    if (open_session) {
        uint8_t nrc = 0;
        int r = udsSession(req_id, 0x03, nrc);
        Serial.printf("# session 10 03 on 0x%03X -> %s", req_id,
                      r == 1 ? "OK (0x50)\n" : r == 0 ? "" : "no response\n");
        if (r == 0) Serial.printf("NEG nrc=0x%02X\n", nrc);
    }
    Serial.printf("# SCAN ECU=0x%03X range=0x%04X..0x%04X%s\n", req_id, start_did, end_did,
                  open_session ? " (session 10 03)" : "");

    uint16_t hits = 0;
    uint16_t nrc_count[256] = {0};
    bool     nrc_seen[256]  = {false};

    for (uint32_t did = start_did; did <= end_did; ++did) {
        if (Serial.available()) { Serial.println("# SCAN aborted by serial"); break; }
        if (open_session && (did & 0x001F) == 0) udsTesterPresent(req_id);   // keep session alive

        uint8_t buf[64]; uint8_t len = 0;
        int r = udsRead(req_id, (uint16_t)did, buf, len);
        if (r == 1) {
            Serial.printf("POS ECU=0x%03X DID=0x%04X len=%u data=", req_id, (unsigned)did, len);
            for (uint8_t i = 3; i < len; ++i) Serial.printf("%02X ", buf[i]);   // D0..Dn
            Serial.println();
            ++hits;
        } else if (r == 0) {
            uint8_t nrc = buf[2];
            if (!nrc_seen[nrc]) {     // log the first DID at which each NRC appears
                nrc_seen[nrc] = true;
                Serial.printf("# first nrc=0x%02X at DID=0x%04X\n", nrc, (unsigned)did);
            }
            ++nrc_count[nrc];
        }
        if ((did & 0x003F) == 0) Serial.printf("# .. at 0x%04X (%u hits)\n", (unsigned)did, hits);
        delay(15);   // pace the bus; keeps the ECU comfortable
    }

    Serial.printf("# SCAN done ECU=0x%03X hits=%u  NRC histogram:", req_id, hits);
    for (int i = 0; i < 256; ++i) if (nrc_count[i]) Serial.printf(" %02X=%u", i, nrc_count[i]);
    Serial.println();
}

// ---------------------------------------------------------------------------
// SIG — one CSV row per cycle: candidate DID bytes + rpm + accel% + speed.
// Look for the row where accel≈0 & rpm>1100 & speed>0: an injected-fuel channel
// drops to ~0 there (deceleration fuel cut-off), unlike air/MAF.
// ---------------------------------------------------------------------------
static void sigTick() {
    uint8_t buf[64]; uint8_t len = 0;
    int r = udsRead(g_sig_req, g_sig_did, buf, len);

    uint8_t a, b;
    float rpm   = obd01(0x0C, a, b) ? ((a * 256.0f + b) / 4.0f) : -1.0f;
    float accel = obd01(0x49, a, b) ? (a * 100.0f / 255.0f)     : -1.0f;
    float speed = obd01(0x0D, a, b) ? (float)a                  : -1.0f;

    uint64_t ts = currentTimestampUs();
    Serial.printf("SIG,%lu.%06lu,0x%04X,", (uint32_t)(ts / 1000000ULL),
                  (uint32_t)(ts % 1000000ULL), g_sig_did);
    if (r == 1)      { for (uint8_t i = 3; i < len; ++i) Serial.printf("%02X", buf[i]); }
    else if (r == 0) Serial.printf("NEG:%02X", buf[2]);   // NRC: 31=outOfRange 22=conditions 7F=session 33=security
    else             Serial.print("NA");
    Serial.printf(",rpm=%.0f,accel=%.1f,speed=%.0f\n", rpm, accel, speed);

    delay(100);   // ~10 Hz
}

// ---------------------------------------------------------------------------
// Broadcast frame decoding (igkov bcomp11 reverse-engineering). Returns a
// headline integer used for change-detection; prints the decoded interpretation.
// ---------------------------------------------------------------------------
static int32_t decodeBroadcast(uint16_t id, const uint8_t* d, uint8_t dlc) {
    if (id == BCAST_FUEL && dlc >= 7) return (d[5] << 8) | d[6];   // injected fuel raw
    if (id == BCAST_GEAR && dlc >= 3) return d[2];                 // full gear byte (tgt<<4|cur)
    return 0;
}

// 0x218 gear code (one nibble of D2). Confirmed on the Pajero IV 4M41 by sweeping
// the selector P→R→N→D→manual: 0x0=N, 0x1..0x5 = forward gears, 0xB=R, 0xD=P.
// D2 packs target gear in the high nibble and current gear in the low nibble; they
// match at rest and differ only mid-shift.
static const char* gearName(uint8_t nib) {
    switch (nib) {
        case 0x0: return "N";
        case 0x1: return "1";
        case 0x2: return "2";
        case 0x3: return "3";
        case 0x4: return "4";
        case 0x5: return "5";
        case 0xB: return "R";
        case 0xD: return "P";
        default:  return "?";
    }
}

static void printBroadcast(uint16_t id, const uint8_t* d, uint8_t dlc, uint64_t ts) {
    Serial.printf("WATCH %lu.%06lu %03X#", (uint32_t)(ts / 1000000ULL),
                  (uint32_t)(ts % 1000000ULL), id);
    for (uint8_t i = 0; i < dlc; ++i) Serial.printf("%02X", d[i]);

    if (id == BCAST_FUEL && dlc >= 7) {
        Serial.printf("  fuel_raw=%d (D5D6)  D0=%u", (d[5] << 8) | d[6], d[0]);
    } else if (id == BCAST_GEAR && dlc >= 3) {
        uint8_t cur = d[2] & 0x0F, tgt = d[2] >> 4;
        Serial.printf("  gear=%s", gearName(cur));
        if (tgt != cur) Serial.printf(" (shifting->%s)", gearName(tgt));
    }
    Serial.println();
}

// ---------------------------------------------------------------------------
// WATCH — listen-only; decode watched broadcast frames. Per-id rate limiter:
// print on a decoded-value change, else at most every WATCH_MIN_INTERVAL_MS, so
// the ~50 Hz 0x218 frame can't flood the console.
// ---------------------------------------------------------------------------
static void watchTick() {
    struct can_frame m;
    if (mcp2515.readMessage(&m) != MCP2515::ERROR_OK) return;
    uint16_t id = m.can_id & 0x7FF;
    for (uint8_t i = 0; i < g_watch_n; ++i) {
        if (g_watch[i].id != id) continue;
        int32_t  v   = decodeBroadcast(id, m.data, m.can_dlc);
        uint32_t now = millis();
        bool changed = (v != g_watch[i].last_val);
        bool elapsed = (now - g_watch[i].last_ms) >= WATCH_MIN_INTERVAL_MS;
        if (changed || elapsed) {
            printBroadcast(id, m.data, m.can_dlc, currentTimestampUs());
            g_watch[i].last_val = v;
            g_watch[i].last_ms  = now;
        }
        return;
    }
}

// ---------------------------------------------------------------------------
// DIFF — listen-only field isolator (see g_diff_* above). Hold the input still
// for the first DIFF_LEARN_MS to learn the noisy bytes, then change the input
// (move the selector) and watch the stable byte(s) change.
// ---------------------------------------------------------------------------
static void diffTick() {
    if (g_diff_learning && (millis() - g_diff_learn_start) >= DIFF_LEARN_MS) {
        g_diff_learning = false;
        Serial.print("# DIFF learn done; ignoring volatile bytes:");
        bool any = false;
        for (int i = 0; i < 8; ++i) if (g_diff_vol[i]) { Serial.printf(" D%d", i); any = true; }
        Serial.println(any ? "" : " (none)");
        Serial.println("# now change the input (move the selector) — stable-byte changes print below");
    }

    struct can_frame m;
    if (mcp2515.readMessage(&m) != MCP2515::ERROR_OK) return;
    if ((m.can_id & 0x7FF) != g_diff_id) return;

    uint8_t dlc = m.can_dlc;
    if (!g_diff_have) { memcpy(g_diff_last, m.data, 8); g_diff_dlc = dlc; g_diff_have = true; return; }

    uint64_t ts = currentTimestampUs();
    for (uint8_t i = 0; i < dlc && i < 8; ++i) {
        if (m.data[i] == g_diff_last[i]) continue;
        if (g_diff_learning) {
            g_diff_vol[i] = true;                       // changed while input held still → noise
        } else if (!g_diff_vol[i]) {
            Serial.printf("DIFF %lu.%06lu %03X D%u: %02X->%02X  frame=",
                          (uint32_t)(ts / 1000000ULL), (uint32_t)(ts % 1000000ULL),
                          g_diff_id, i, g_diff_last[i], m.data[i]);
            for (uint8_t k = 0; k < dlc; ++k) Serial.printf("%02X", m.data[k]);
            Serial.println();
        }
    }
    memcpy(g_diff_last, m.data, 8); g_diff_dlc = dlc;
}

// ---------------------------------------------------------------------------
// ADIFF — whole-bus DIFF (see g_adiff_* above). Hold everything still for the
// first DIFF_LEARN_MS so each ID's churning bytes get marked volatile, then toggle
// the input you're hunting (turn the 4WD dial, pull the diff-lock switch) and the
// ID/byte that carries it prints — even though its frame ID was unknown up front.
// ---------------------------------------------------------------------------
static void adiffTick() {
    if (g_adiff_learning && (millis() - g_adiff_learn_start) >= DIFF_LEARN_MS) {
        g_adiff_learning = false;
        uint16_t vol_total = 0;
        for (uint8_t i = 0; i < g_adiff_n; ++i)
            for (int k = 0; k < 8; ++k) if (g_adiff[i].vol[k]) ++vol_total;
        Serial.printf("# ADIFF learn done: %u IDs, %u volatile bytes ignored\n",
                      g_adiff_n, vol_total);
        Serial.println("# now toggle the input (4WD dial / diff lock / switch) — changes print below");
    }

    struct can_frame m;
    if (mcp2515.readMessage(&m) != MCP2515::ERROR_OK) return;
    uint16_t id  = m.can_id & 0x7FF;
    uint8_t  dlc = m.can_dlc;

    int s = adiffSlot(id, m.data, dlc);
    if (s < 0) return;                         // table full: ignore new IDs
    AdiffEntry& e = g_adiff[s];

    uint64_t ts = currentTimestampUs();
    for (uint8_t i = 0; i < dlc && i < 8; ++i) {
        if (m.data[i] == e.last[i]) continue;
        if (g_adiff_learning) {
            e.vol[i] = true;                   // changed while still → noise
        } else if (!e.vol[i]) {
            Serial.printf("ADIFF %lu.%06lu %03X D%u: %02X->%02X  frame=",
                          (uint32_t)(ts / 1000000ULL), (uint32_t)(ts % 1000000ULL),
                          id, i, e.last[i], m.data[i]);
            for (uint8_t k = 0; k < dlc; ++k) Serial.printf("%02X", m.data[k]);
            Serial.println();
        }
    }
    memcpy(e.last, m.data, 8); e.dlc = dlc;
}

// ---------------------------------------------------------------------------
// FUELLOG — active. Refresh the cached 0x608 fuel / 0x218 gear from any pending
// broadcast frames, then poll rpm / accelerator / speed and print one CSV row.
// The row to look for: accel≈0 & rpm>1100 & speed>0 (overrun) — a real injected-
// fuel channel drops to ~0 there while MAF-derived fuel would not.
// ---------------------------------------------------------------------------
static void fuelLogTick() {
    struct can_frame m;
    while (mcp2515.readMessage(&m) == MCP2515::ERROR_OK) {   // drain & cache latest
        uint16_t id = m.can_id & 0x7FF;
        if (id == BCAST_FUEL && m.can_dlc >= 7) {
            g_fl_fuel = (m.data[5] << 8) | m.data[6];
            g_fl_fuel_d0 = m.data[0];
        } else if (id == BCAST_GEAR && m.can_dlc >= 3) {
            g_fl_gear = m.data[2] & 0x0F;
        }
    }

    uint8_t a, b;
    float rpm   = obd01(0x0C, a, b) ? ((a * 256.0f + b) / 4.0f) : -1.0f;
    float accel = obd01(0x49, a, b) ? (a * 100.0f / 255.0f)     : -1.0f;
    float speed = obd01(0x0D, a, b) ? (float)a                  : -1.0f;

    uint64_t ts = currentTimestampUs();
    Serial.printf("FUEL,%lu.%06lu,fuel_raw=%ld,D0=%u,gear=", (uint32_t)(ts / 1000000ULL),
                  (uint32_t)(ts % 1000000ULL), (long)g_fl_fuel, g_fl_fuel_d0);
    if (g_fl_gear >= 0) Serial.printf("0x%X", g_fl_gear); else Serial.print("NA");
    Serial.printf(",rpm=%.0f,accel=%.1f,speed=%.0f\n", rpm, accel, speed);
    delay(100);   // ~3-4 Hz after the OBD polls
}

// ---------------------------------------------------------------------------
// RDLI — KWP Service 0x21 (ReadDataByLocalIdentifier) single read. Tests igkov
// bcomp11's Pajero transmission/odometer reads, which use this legacy service
// (request "02 21 LID") rather than UDS 0x22. Prints the full reassembled
// response plus, for the known LIDs, bcomp11's candidate decode as a HINT — the
// exact byte offsets are unverified on this vehicle, so the raw dump is the
// source of truth. Response layout: 61 LID D0 D1 ... (D0 = out[2]).
// ---------------------------------------------------------------------------
static void rdliRead(uint16_t req_id, uint8_t lid) {
    uint8_t buf[64]; uint8_t len = 0;
    int r = kwpRead21(req_id, lid, buf, len);
    if (r != 1) {
        if (r == 0) Serial.printf("RDLI ECU=0x%03X LID=0x%02X NEG nrc=0x%02X\n", req_id, lid, buf[2]);
        else        Serial.printf("RDLI ECU=0x%03X LID=0x%02X no response\n", req_id, lid);
        return;
    }
    Serial.printf("RDLI ECU=0x%03X LID=0x%02X POS resp=", req_id, lid);
    for (uint8_t i = 0; i < len; ++i) Serial.printf("%02X ", buf[i]);
    Serial.println();
    // Data payload starts after "61 LID": D0 = buf[2].
    const uint8_t* D = buf + 2;
    uint8_t nD = (len > 2) ? (uint8_t)(len - 2) : 0;
    Serial.print("#   data Dn:");
    for (uint8_t i = 0; i < nD; ++i) Serial.printf(" D%u=%02X", i, D[i]);
    Serial.println();

    if (lid == 0x02 && nD >= 7) {        // bcomp11 PAJERO_AT_INFO candidate decode
        int   input  = D[2] * 128 + D[3] / 2;
        int   output = D[4] * 128 + D[5] / 2;
        int   atf    = D[6] - 40;
        Serial.printf("#   [hint AT] input=%drpm output=%drpm ATF=%dC  (igkov, UNVERIFIED)\n",
                      input, output, atf);
    } else if (lid == 0x03 && nD >= 5) { // bcomp11 PAJERO_ODO_INFO candidate decode
        uint32_t odo = ((uint32_t)D[2] * 256 + D[3]) * 256 + D[4];
        Serial.printf("#   [hint ODO] odometer=%lu km  (igkov, UNVERIFIED)\n", (unsigned long)odo);
    }
}

// ---------------------------------------------------------------------------
// Passive candump logging (unchanged format).
// ---------------------------------------------------------------------------
static void passiveTick() {
    struct can_frame rx;
    if (mcp2515.readMessage(&rx) != MCP2515::ERROR_OK) return;
    if (!g_dump) return;   // listen-only: drain RX but stay quiet until DUMP

    uint64_t ts = currentTimestampUs();
    Serial.printf("(%lu.%06lu) can0 %X#", (uint32_t)(ts / 1000000ULL),
                  (uint32_t)(ts % 1000000ULL), (unsigned)(rx.can_id & 0x1FFFFFFF));
    for (uint8_t i = 0; i < rx.can_dlc; ++i) Serial.printf("%02X", rx.data[i]);
    Serial.println();
}

// ---------------------------------------------------------------------------
// Serial command handling
// ---------------------------------------------------------------------------
static void handleSerial() {
    if (!Serial.available()) return;
    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) return;

    // Time sync: T<unix-seconds>
    if (line[0] == 'T' && line.length() > 1 && isDigit(line[1])) {
        unix_base_us   = strtoull(line.c_str() + 1, nullptr, 10) * 1000000ULL;
        micros_at_sync = micros();
        Serial.println("# time synced");
        return;
    }

    String cmd = line;
    cmd.toUpperCase();

    if (cmd == "STOP" || cmd == "PASV") {
        enterPassive();
        return;
    }

    if (cmd == "DUMP") {
        g_dump = !g_dump;
        Serial.printf("# candump %s\n", g_dump ? "ON" : "OFF");
        return;
    }

    if (cmd == "OBD") {
        enterActive();
        obdScan();
        enterPassive();
        return;
    }

    if (cmd == "VERIFY" || cmd.startsWith("VERIFY ")) {
        bool m01 = cmd.indexOf("M22") < 0 || cmd.indexOf("M01") >= 0;
        bool m22 = cmd.indexOf("M01") < 0 || cmd.indexOf("M22") >= 0;
        enterActive();
        if (m01) verifyMode01();
        if (m22) verifyMode22();
        enterPassive();
        return;
    }

    if (cmd.startsWith("WATCH")) {
        // WATCH [id ...] — replace the watch list with the given hex IDs, or keep
        // the defaults (0x608, 0x218) when none are supplied.
        uint8_t n = 0;
        int i = 5;                                  // skip "WATCH"
        while (i < (int)cmd.length() && n < WATCH_MAX) {
            while (i < (int)cmd.length() && cmd[i] == ' ') ++i;
            int j = i;
            while (j < (int)cmd.length() && cmd[j] != ' ') ++j;
            if (j == i) break;
            uint16_t id = (uint16_t)strtoul(cmd.substring(i, j).c_str(), nullptr, 16) & 0x7FF;
            g_watch[n++] = { id, 0, INT32_MIN };
            i = j;
        }
        if (n > 0) g_watch_n = n;                   // no args → keep current list
        mcp2515.setListenOnlyMode();                // listen-only is enough; never TX
        g_mode = Mode::WATCH;
        Serial.print("# mode=WATCH ids=");
        for (uint8_t k = 0; k < g_watch_n; ++k) Serial.printf("%03X ", g_watch[k].id);
        Serial.println("(send STOP to end)");
        return;
    }

    if (cmd.startsWith("DIFF")) {
        // DIFF [id] — isolate a slow field in one noisy frame (default 0x218).
        long id = 0;
        g_diff_id = (sscanf(line.c_str() + 4, " %lx", &id) == 1) ? ((uint16_t)id & 0x7FF)
                                                                 : BCAST_GEAR;
        g_diff_have = false; g_diff_learning = true; g_diff_learn_start = millis();
        for (int k = 0; k < 8; ++k) g_diff_vol[k] = false;
        mcp2515.setListenOnlyMode();                // listen-only; never TX
        g_mode = Mode::DIFF;
        Serial.printf("# mode=DIFF id=%03X — hold the input STILL ~%lus while it learns noisy bytes...\n",
                      g_diff_id, (unsigned long)(DIFF_LEARN_MS / 1000));
        return;
    }

    if (cmd == "ADIFF") {
        // Whole-bus DIFF — no ID argument; learns every frame, then flags the one
        // that moves when you toggle an input (4WD dial, diff lock, ...).
        g_adiff_n = 0;
        g_adiff_learning = true;
        g_adiff_learn_start = millis();
        mcp2515.setListenOnlyMode();                // listen-only; never TX
        g_mode = Mode::ADIFF;
        Serial.printf("# mode=ADIFF — hold EVERYTHING still ~%lus while it learns the whole bus...\n",
                      (unsigned long)(DIFF_LEARN_MS / 1000));
        return;
    }

    if (cmd == "FUELLOG") {
        enterActive();
        g_mode = Mode::FUELLOG;
        Serial.println("# mode=FUELLOG (0x608 fuel + 0x218 gear vs rpm/accel/speed; STOP to end)");
        return;
    }

    if (cmd.startsWith("RDLI")) {
        // RDLI <reqHex> <lidHex>
        long req = 0, lid = 0;
        if (sscanf(line.c_str() + 4, " %lx %lx", &req, &lid) == 2) {
            enterActive();
            rdliRead((uint16_t)req, (uint8_t)lid);
            enterPassive();
        } else {
            Serial.println("# usage: RDLI <reqHex> <lidHex>   e.g. RDLI 7E1 02");
        }
        return;
    }

    if (cmd.startsWith("SCAN")) {
        // SCAN [eng|tcm] [startHex endHex] [sess] — tokenize so an "ENG"/"SESS"
        // keyword is never mistaken for a hex number (E/S aside, keep it explicit).
        bool     do_eng = true, do_tcm = true, ecu_chosen = false, sess = false;
        uint16_t s = 0x2000, e = 0x21FF;
        uint16_t hex_vals[2]; int hex_n = 0;

        int i = 4;                                  // skip "SCAN"
        while (i < (int)cmd.length()) {
            while (i < (int)cmd.length() && cmd[i] == ' ') ++i;
            int j = i;
            while (j < (int)cmd.length() && cmd[j] != ' ') ++j;
            if (j == i) break;
            String tok = cmd.substring(i, j);
            if (tok == "ENG")       { do_eng = true;  do_tcm = false; ecu_chosen = true; }
            else if (tok == "TCM")  { do_tcm = true;  do_eng = false; ecu_chosen = true; }
            else if (tok == "SESS") { sess = true; }
            else if (hex_n < 2)     { hex_vals[hex_n++] = (uint16_t)strtoul(tok.c_str(), nullptr, 16); }
            i = j;
        }
        (void)ecu_chosen;
        if (hex_n == 2) { s = hex_vals[0]; e = hex_vals[1]; }

        enterActive();
        if (do_eng) scanEcu(UDS_REQ_ENGINE, s, e, sess);
        if (do_tcm) scanEcu(UDS_REQ_TCM,    s, e, sess);
        enterPassive();
        return;
    }

    if (cmd.startsWith("SIG")) {
        // SIG <reqHex> <didHex>
        long req = 0, did = 0;
        if (sscanf(line.c_str() + 3, " %lx %lx", &req, &did) == 2) {
            g_sig_req = (uint16_t)req;
            g_sig_did = (uint16_t)did;
            enterActive();
            g_mode = Mode::SIG;
            Serial.printf("# mode=SIG req=0x%03X did=0x%04X (send STOP to end)\n",
                          g_sig_req, g_sig_did);
        } else {
            Serial.println("# usage: SIG <reqHex> <didHex>   e.g. SIG 7E0 2100");
        }
        return;
    }

    Serial.printf("# unknown command: %s\n", line.c_str());
}

void setup() {
    Serial.begin(USB_BAUD);
    delay(400);
    canReset();
    enterPassive();
    Serial.println("# sniffer ready (quiet) — commands: T<unix> | DUMP | WATCH [id..] | DIFF [id] | "
                   "ADIFF | OBD | VERIFY [m01|m22] | SCAN [eng|tcm] [s e] [sess] | SIG <req> <did> | "
                   "FUELLOG | RDLI <req> <lid> | STOP");
}

void loop() {
    handleSerial();
    switch (g_mode) {
        case Mode::SIG:     sigTick();     break;
        case Mode::FUELLOG: fuelLogTick(); break;
        case Mode::WATCH:   watchTick();   break;
        case Mode::DIFF:    diffTick();    break;
        case Mode::ADIFF:   adiffTick();   break;
        default:            passiveTick(); break;
    }
}
