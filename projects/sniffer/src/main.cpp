// CAN sniffer / UDS discovery tool — runs on the SAME board as projects/server
// (ESP32-S3 + MCP2515 over SPI, CAN A = the OBD-II port).
//
// Modes (switch live over the serial monitor):
//   PASSIVE (default) — MCP2515 in listen-only mode; stays QUIET on the console
//                       until DUMP is enabled, then logs every frame in candump
//                       format `(sec.usec) can0 ID#DATA`.
//   SCAN            — sweeps UDS Mode 22 DIDs on an ECU and logs every positive
//                     (0x62) response; used to discover undocumented advanced
//                     PIDs such as the diesel injected-fuel-quantity channel.
//   SIG             — polls one DID at 10 Hz alongside rpm / accelerator / speed
//                     and prints a CSV row each cycle, so you can correlate a
//                     candidate value with throttle and overrun while driving.
//
// SCAN and SIG transmit, so they put the MCP2515 into NORMAL mode (the node then
// ACKs bus traffic); PASSIVE returns it to listen-only.
//
// Serial commands (one per line):
//   T<unix>                 set wall-clock base for timestamps (e.g. T1716394391)
//   DUMP                    toggle passive candump printing on/off (default off)
//   OBD                     list standard Mode 01 PIDs the engine ECU supports
//   SCAN [eng|tcm] [s e] [sess]  sweep DIDs s..e (hex) on an ECU; defaults: both,
//                                2000 21FF. `sess` opens an extended diagnostic
//                                session (10 03) first and holds it with
//                                TesterPresent — for ECUs that gate Mode 22.
//   SIG <req> <did>         signature-log one DID, e.g. `SIG 7E0 2100`
//   STOP / PASV             stop SIG/SCAN and return to passive logging

#include <Arduino.h>
#include <SPI.h>
#include <mcp2515.h>

// --- CAN hardware: identical wiring to projects/server (include/pin_config.h) ---
static constexpr int PIN_SPI_SCK  = 12;
static constexpr int PIN_SPI_MISO = 13;
static constexpr int PIN_SPI_MOSI = 11;
static constexpr int PIN_MCP2515_CS  = 10;   // CAN A — OBD-II port
static constexpr int PIN_MCP2515_RST =  9;
static constexpr int PIN_MCP2515_INT =  8;   // unused here (polled), kept for parity

static const uint32_t USB_BAUD = 115200;

// UDS physical request IDs (response = request + 8).
static constexpr uint16_t UDS_REQ_ENGINE = 0x7E0;
static constexpr uint16_t UDS_REQ_TCM    = 0x7E1;
static constexpr uint16_t UDS_RESP_OFFSET = 0x008;

static MCP2515 mcp2515(PIN_MCP2515_CS, 10000000, &SPI);

static uint64_t unix_base_us   = 0;   // Unix time in microseconds at sync point
static uint64_t micros_at_sync = 0;   // micros() value when sync was received

enum class Mode { PASSIVE, SIG };
static Mode     g_mode = Mode::PASSIVE;
static uint16_t g_sig_req = UDS_REQ_ENGINE;
static uint16_t g_sig_did = 0x0000;
static bool     g_dump    = false;   // passive candump printing; off until DUMP

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
// UDS Mode 22 read with ISO-TP reassembly (single + multi frame).
// Returns  1 = positive response (out[] = reassembled UDS starting at 0x62),
//          0 = negative response (0x7F); out[] = {0x7F, service, NRC}, out_len=3,
//         -1 = no/garbled response within timeout.
// NRC 0x78 (responsePending) is handled internally: the timeout is restarted and
// we keep waiting for the real answer instead of reporting it as negative.
// ---------------------------------------------------------------------------
static int udsRead(uint16_t req_id, uint16_t did,
                   uint8_t* out, uint8_t& out_len, uint32_t timeout_ms = 150) {
    const uint16_t resp_id = req_id + UDS_RESP_OFFSET;
    drainRx();

    struct can_frame req = {};
    req.can_id  = req_id;
    req.can_dlc = 8;
    req.data[0] = 0x03;            // ISO-TP single frame, length 3
    req.data[1] = 0x22;           // ReadDataByIdentifier
    req.data[2] = did >> 8;
    req.data[3] = did & 0xFF;
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
                out[0] = 0x7F; out[1] = len >= 2 ? buf[1] : 0x22; out[2] = nrc;
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
                    out[0] = 0x7F; out[1] = total >= 2 ? buf[1] : 0x22;
                    out[2] = total >= 3 ? buf[2] : 0xFF; out_len = 3; return 0;
                }
                uint8_t n = total < sizeof(buf) ? (uint8_t)total : (uint8_t)sizeof(buf);
                memcpy(out, buf, n); out_len = n; return 1;
            }
        }
    }
    return -1;
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
    Serial.println("# sniffer ready (quiet) — commands: T<unix> | DUMP | OBD | SCAN [eng|tcm] [s e] [sess] | SIG <req> <did> | STOP");
}

void loop() {
    handleSerial();
    if (g_mode == Mode::SIG) sigTick();
    else                     passiveTick();
}
