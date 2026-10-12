// Standalone OBD-II diagnostic scanner for the Pajero Sport II (KH8W, 4M41).
//
// Plug into the OBD-II port, connect a phone to the "pajero-diag" access point,
// open http://192.168.4.1 and press Scan. The page shows scan progress and any
// trouble codes found, with descriptions for generic SAE codes.
//
// Scope is standard OBD-II (SAE J1979 over ISO 15765-4) against the engine ECU
// at 0x7E0/0x7E8. That is what this vehicle answers on CAN. ABS, SRS, 4WD and
// ETACS are K-line (MUT-II) on this platform and are NOT reachable here — see
// .claude/skills/can-reverse-engineering/references/mut3-database.md.
//
// Mode 22 is deliberately absent: these ECUs answer NRC 0x11 to every DID.

#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <WebServer.h>
#include <mcp2515.h>

#include "dtc.h"
#include "pin_config.h"
#include "diag_config.h"
#include "web_page.h"

// ---------------------------------------------------------------------------
// CAN / OBD-II constants
// ---------------------------------------------------------------------------
static constexpr uint16_t OBD_REQ_ID      = 0x7E0;   // engine ECU request
static constexpr uint16_t OBD_RESP_OFFSET = 8;       // responses at req + 8

static constexpr uint8_t MODE_CURRENT_DATA   = 0x01;
static constexpr uint8_t MODE_FREEZE_FRAME   = 0x02;
static constexpr uint8_t MODE_STORED_DTC     = 0x03;
static constexpr uint8_t MODE_CLEAR_DTC      = 0x04;
static constexpr uint8_t MODE_PENDING_DTC    = 0x07;
static constexpr uint8_t MODE_PERMANENT_DTC  = 0x0A;

static MCP2515   mcp2515(PIN_MCP2515_CS, 10000000, &SPI);
static WebServer server(80);

// ---------------------------------------------------------------------------
// Scan state, read by the HTTP handlers and written by the scan step machine
// ---------------------------------------------------------------------------
enum ScanStep : uint8_t {
    STEP_IDLE = 0,
    STEP_CAN,
    STEP_ECU,
    STEP_STATUS,
    STEP_STORED,
    STEP_PENDING,
    STEP_PERMANENT,
    STEP_FREEZE,
    STEP_DONE,
};

static const char* kStepName[] = {
    "Idle",
    "Bringing up CAN",
    "Contacting engine ECU",
    "Reading MIL and readiness",
    "Reading stored codes",
    "Reading pending codes",
    "Reading permanent codes",
    "Reading freeze frame",
    "Done",
};

struct DiagState {
    bool          running   = false;
    uint8_t       step      = STEP_IDLE;
    bool          ecu_ok    = false;
    bool          have_status = false;
    MonitorStatus status{};
    uint8_t       n_codes  = 0;
    Dtc           codes[DIAG_MAX_CODES];
    bool          have_freeze = false;
    char          freeze_code[6] = { 0 };
    char          message[96] = "Ready. Press Scan.";
    uint32_t      finished_ms = 0;
};

static DiagState g;

/// Append a code, de-duplicating by raw value but keeping the strongest kind.
/// A code can legitimately appear in both the stored and permanent lists.
static void addCode(const Dtc& d) {
    for (uint8_t i = 0; i < g.n_codes; ++i) {
        if (g.codes[i].raw_hi == d.raw_hi && g.codes[i].raw_lo == d.raw_lo) {
            if (d.kind > g.codes[i].kind) g.codes[i].kind = d.kind;
            return;
        }
    }
    if (g.n_codes < DIAG_MAX_CODES) g.codes[g.n_codes++] = d;
}

// ---------------------------------------------------------------------------
// CAN bring-up
// ---------------------------------------------------------------------------
static void canReset() {
    pinMode(PIN_MCP2515_RST, OUTPUT);
    digitalWrite(PIN_MCP2515_RST, HIGH); delay(50);
    digitalWrite(PIN_MCP2515_RST, LOW);  delay(50);
    digitalWrite(PIN_MCP2515_RST, HIGH); delay(50);
    SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, PIN_MCP2515_CS);
    mcp2515.reset();
    mcp2515.setBitrate(CAN_500KBPS);
    mcp2515.setNormalMode();   // must transmit and ACK to talk to the ECU
}

static void drainRx() {
    struct can_frame m;
    while (mcp2515.readMessage(&m) == MCP2515::ERROR_OK) { /* discard */ }
}

// ---------------------------------------------------------------------------
// ISO-TP request / response, single and multi frame.
//
// Mirrors the proven implementation in projects/sniffer: send `payload` as an
// ISO-TP single frame, reassemble the reply from (OBD_REQ_ID + 8), answer a
// first frame with a clear-to-send flow control, and keep waiting through
// NRC 0x78 (responsePending) rather than reporting it as a failure.
//
// Returns  1 positive (out[] starts at the service id),
//          0 negative (out[] = {0x7F, service, NRC}),
//         -1 nothing intelligible before the timeout.
// ---------------------------------------------------------------------------
static int isotpTransceive(const uint8_t* payload, uint8_t plen,
                           uint8_t* out, uint16_t& out_len,
                           uint32_t timeout_ms = 1200) {
    const uint16_t resp_id = OBD_REQ_ID + OBD_RESP_OFFSET;
    const uint8_t  service = plen > 0 ? payload[0] : 0x00;
    drainRx();

    struct can_frame req = {};
    req.can_id  = OBD_REQ_ID;
    req.can_dlc = 8;
    req.data[0] = (uint8_t)(plen & 0x0F);
    for (uint8_t i = 0; i < plen && i < 7; ++i) req.data[1 + i] = payload[i];
    if (mcp2515.sendMessage(&req) != MCP2515::ERROR_OK) return -1;

    static uint8_t buf[512];
    uint16_t total = 0, got = 0;
    bool     assembling = false;
    uint32_t start = millis();

    while (millis() - start < timeout_ms) {
        struct can_frame m;
        if (mcp2515.readMessage(&m) != MCP2515::ERROR_OK) { delayMicroseconds(200); continue; }
        if ((m.can_id & 0x7FF) != resp_id) continue;

        const uint8_t pci = m.data[0] & 0xF0;
        if (pci == 0x00) {                               // single frame
            const uint8_t len = m.data[0] & 0x0F;
            for (uint8_t k = 0; k < len && (uint8_t)(1 + k) < 8; ++k) buf[k] = m.data[1 + k];
            if (len >= 1 && buf[0] == 0x7F) {
                const uint8_t nrc = len >= 3 ? buf[2] : 0xFF;
                if (nrc == 0x78) { start = millis(); continue; }
                out[0] = 0x7F; out[1] = len >= 2 ? buf[1] : service; out[2] = nrc;
                out_len = 3; return 0;
            }
            memcpy(out, buf, len);
            out_len = len; return 1;
        } else if (pci == 0x10) {                        // first frame
            total = (uint16_t)((((uint16_t)(m.data[0] & 0x0F)) << 8) | m.data[1]);
            got = 0;
            for (uint8_t k = 0; k < 6 && got < total && got < sizeof(buf); ++k) buf[got++] = m.data[2 + k];
            struct can_frame fc = {};
            fc.can_id = OBD_REQ_ID; fc.can_dlc = 8; fc.data[0] = 0x30;
            mcp2515.sendMessage(&fc);
            assembling = true;
        } else if (pci == 0x20) {                        // consecutive frame
            if (!assembling) continue;
            for (uint8_t k = 0; k < 7 && got < total && got < sizeof(buf); ++k) buf[got++] = m.data[1 + k];
            if (got >= total) {
                if (total >= 1 && buf[0] == 0x7F) {
                    out[0] = 0x7F; out[1] = total >= 2 ? buf[1] : service;
                    out[2] = total >= 3 ? buf[2] : 0xFF; out_len = 3; return 0;
                }
                const uint16_t n = total < sizeof(buf) ? total : (uint16_t)sizeof(buf);
                memcpy(out, buf, n);
                out_len = n; return 1;
            }
        }
    }
    return -1;
}

/// Request one of the DTC-list modes and fold the codes into state.
/// Returns true when the ECU answered positively, whether or not it had codes.
static bool readDtcList(uint8_t mode, uint8_t kind) {
    const uint8_t req[1] = { mode };
    static uint8_t resp[512];
    uint16_t len = 0;
    if (isotpTransceive(req, 1, resp, len) != 1) return false;
    if (len < 1 || resp[0] != (uint8_t)(mode + 0x40)) return false;

    Dtc found[DIAG_MAX_CODES];
    const uint8_t clipped = len > 255u ? 255u : (uint8_t)len;
    const uint8_t n = dtcParseResponse(resp, clipped, kind, found, DIAG_MAX_CODES);
    for (uint8_t i = 0; i < n; ++i) addCode(found[i]);
    return true;
}

// ---------------------------------------------------------------------------
// Scan step machine — one step per loop() pass so HTTP stays responsive
// ---------------------------------------------------------------------------
static void startScan() {
    g = DiagState{};
    g.running = true;
    g.step    = STEP_CAN;
    snprintf(g.message, sizeof(g.message), "Scanning…");
}

static void scanTick() {
    if (!g.running) return;

    switch (g.step) {
    case STEP_CAN:
        canReset();
        g.step = STEP_ECU;
        break;

    case STEP_ECU: {
        // Mode 01 PID 00 is the supported-PID bitmap: the cheapest proof the
        // engine ECU is awake and speaking OBD-II.
        const uint8_t req[2] = { MODE_CURRENT_DATA, 0x00 };
        uint8_t resp[64]; uint16_t len = 0;
        g.ecu_ok = (isotpTransceive(req, 2, resp, len) == 1 && len >= 2 && resp[0] == 0x41);
        if (!g.ecu_ok) {
            snprintf(g.message, sizeof(g.message),
                     "No reply from the engine ECU. Ignition on? Adapter seated?");
            g.step = STEP_DONE;
        } else {
            g.step = STEP_STATUS;
        }
        break;
    }

    case STEP_STATUS: {
        const uint8_t req[2] = { MODE_CURRENT_DATA, 0x01 };
        uint8_t resp[64]; uint16_t len = 0;
        if (isotpTransceive(req, 2, resp, len) == 1 && len >= 6 && resp[0] == 0x41) {
            g.status = decodeMonitorStatus(resp[2], resp[3], resp[4], resp[5]);
            g.have_status = true;
        }
        g.step = STEP_STORED;
        break;
    }

    case STEP_STORED:
        readDtcList(MODE_STORED_DTC, DTC_STORED);
        g.step = STEP_PENDING;
        break;

    case STEP_PENDING:
        readDtcList(MODE_PENDING_DTC, DTC_PENDING);
        g.step = STEP_PERMANENT;
        break;

    case STEP_PERMANENT:
        readDtcList(MODE_PERMANENT_DTC, DTC_PERMANENT);
        g.step = STEP_FREEZE;
        break;

    case STEP_FREEZE: {
        // Mode 02 PID 02 reports the DTC that captured the freeze frame.
        // Nothing to ask for when no code is stored.
        if (g.n_codes > 0) {
            const uint8_t req[3] = { MODE_FREEZE_FRAME, 0x02, 0x00 };
            uint8_t resp[64]; uint16_t len = 0;
            if (isotpTransceive(req, 3, resp, len) == 1 && len >= 5 && resp[0] == 0x42) {
                if (!dtcIsPadding(resp[3], resp[4])) {
                    dtcToString(resp[3], resp[4], g.freeze_code);
                    g.have_freeze = true;
                }
            }
        }
        g.step = STEP_DONE;
        break;
    }

    case STEP_DONE:
        g.running     = false;
        g.finished_ms = millis();
        if (g.ecu_ok) {
            if (g.n_codes == 0) {
                snprintf(g.message, sizeof(g.message), "No trouble codes found.");
            } else {
                snprintf(g.message, sizeof(g.message), "%u trouble code%s found.",
                         (unsigned)g.n_codes, g.n_codes == 1 ? "" : "s");
            }
        }
        g.step = STEP_IDLE;
        break;

    default:
        g.running = false;
        break;
    }
}

// ---------------------------------------------------------------------------
// HTTP
// ---------------------------------------------------------------------------
static const char* kindName(uint8_t kind) {
    switch (kind) {
    case DTC_PENDING:   return "pending";
    case DTC_PERMANENT: return "permanent";
    default:            return "stored";
    }
}

/// Copy `in` into `out` with JSON string escaping applied.
static void jsonEscape(const char* in, char* out, size_t out_size) {
    size_t o = 0;
    for (size_t i = 0; in[i] != '\0' && o + 2 < out_size; ++i) {
        const char c = in[i];
        if (c == '"' || c == '\\') {
            out[o++] = '\\';
            out[o++] = c;
        } else if ((uint8_t)c >= 0x20) {
            out[o++] = c;
        }
    }
    out[o] = '\0';
}

static void handleState() {
    static char json[4096];
    char msg[192];
    jsonEscape(g.message, msg, sizeof(msg));

    int n = snprintf(json, sizeof(json),
        "{\"running\":%s,\"step\":%u,\"total\":%u,\"step_name\":\"%s\","
        "\"ecu_ok\":%s,\"message\":\"%s\",\"have_status\":%s,"
        "\"mil\":%s,\"dtc_count\":%u,"
        "\"readiness\":{\"misfire\":{\"sup\":%s,\"ok\":%s},"
        "\"fuel\":{\"sup\":%s,\"ok\":%s},"
        "\"components\":{\"sup\":%s,\"ok\":%s}},"
        "\"freeze\":%s%s%s,\"codes\":[",
        g.running ? "true" : "false",
        (unsigned)g.step, (unsigned)STEP_DONE, kStepName[g.step],
        g.ecu_ok ? "true" : "false", msg,
        g.have_status ? "true" : "false",
        g.status.mil ? "true" : "false", (unsigned)g.status.dtc_count,
        g.status.misfire_supported ? "true" : "false", g.status.misfire_ready ? "true" : "false",
        g.status.fuel_supported ? "true" : "false", g.status.fuel_ready ? "true" : "false",
        g.status.components_supported ? "true" : "false", g.status.components_ready ? "true" : "false",
        g.have_freeze ? "\"" : "null", g.have_freeze ? g.freeze_code : "", g.have_freeze ? "\"" : "");

    for (uint8_t i = 0; i < g.n_codes && n > 0 && n < (int)sizeof(json); ++i) {
        const char* desc = dtcDescribe(g.codes[i].code);
        char esc[128];
        jsonEscape(desc ? desc : "", esc, sizeof(esc));
        n += snprintf(json + n, sizeof(json) - n,
                      "%s{\"code\":\"%s\",\"kind\":\"%s\",\"desc\":%s%s%s}",
                      i ? "," : "", g.codes[i].code, kindName(g.codes[i].kind),
                      desc ? "\"" : "null", desc ? esc : "", desc ? "\"" : "");
    }
    if (n > 0 && n < (int)sizeof(json)) snprintf(json + n, sizeof(json) - n, "]}");

    server.send(200, "application/json", json);
}

static void handleScan() {
    if (g.running) {
        server.send(409, "text/plain", "scan already running");
        return;
    }
    startScan();
    server.send(200, "text/plain", "started");
}

static void handleClear() {
    if (g.running) {
        server.send(409, "text/plain", "scan running");
        return;
    }
    // Mode 04 erases codes, freeze frames and readiness monitors. Require the
    // browser to have asked the user, and say so plainly rather than trusting a
    // bare button press.
    if (server.arg("confirm") != DIAG_CLEAR_TOKEN) {
        server.send(400, "text/plain", "missing confirmation");
        return;
    }
    canReset();
    const uint8_t req[1] = { MODE_CLEAR_DTC };
    uint8_t resp[64]; uint16_t len = 0;
    const int r = isotpTransceive(req, 1, resp, len);
    if (r == 1 && len >= 1 && resp[0] == 0x44) {
        g = DiagState{};
        snprintf(g.message, sizeof(g.message),
                 "Codes cleared. Readiness monitors reset — rescan after a drive cycle.");
        server.send(200, "text/plain", "cleared");
    } else {
        server.send(502, "text/plain", "ECU refused the clear request");
    }
}

static void handleRoot() {
    server.send_P(200, "text/html", DIAG_PAGE);
}

// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(200);

    WiFi.mode(WIFI_AP);
    WiFi.softAP(DIAG_AP_SSID, DIAG_AP_PASSWORD, DIAG_AP_CHANNEL);
    Serial.printf("# AP \"%s\" up at %s\n", DIAG_AP_SSID, WiFi.softAPIP().toString().c_str());

    server.on("/", handleRoot);
    server.on("/api/state", handleState);
    server.on("/api/scan", HTTP_POST, handleScan);
    server.on("/api/clear", HTTP_POST, handleClear);
    server.begin();

    canReset();
    Serial.println("# diagnostic ready — open http://192.168.4.1");
}

void loop() {
    server.handleClient();
    scanTick();
}
