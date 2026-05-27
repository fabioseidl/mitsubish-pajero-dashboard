#pragma once

#include "payload.h"
#include "i_screen_controller.h"

#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#include <lvgl.h>

// ---------------------------------------------------------------------------
// PidType — tells formatValue() how to interpret the bytes at a Payload offset
// ---------------------------------------------------------------------------
enum class PidType : uint8_t {
    FLOAT1,    // float, formatted as "%.1f"
    FLOAT2,    // float, formatted as "%.2f"
    UINT_DEC,  // any unsigned integer (uint8_t/uint16_t/uint32_t), formatted as "%u"
    BOOL_YESNO // bool, formatted as "YES" or "NO"
};

// ---------------------------------------------------------------------------
// PidDescriptor — one entry in the compile-time PID display table
// ---------------------------------------------------------------------------
struct PidDescriptor {
    const char* label;   // display name shown in the label column (≤ 24 chars)
    const char* unit;    // unit string shown in the unit column (≤ 6 chars)
    PidType     type;
    uint16_t    offset;  // byte offset of the field inside the Payload struct
                         // (computed via offsetof at compile time)
};

// ---------------------------------------------------------------------------
// DebugScreen — implements IScreenController for the 7" 1024×600 display.
//
// Thread-safety contract (same as CYDScreenController):
//   onPayloadReceived() and onServerStatusChanged() are called from the
//   WiFi/ESP-NOW task. They are safe because they only write to spinlock-
//   protected buffers and NEVER touch LVGL objects.
//
//   tick() is called from the Arduino loop() task. It drains the spinlock
//   buffers and is the ONLY place that calls LVGL APIs.
// ---------------------------------------------------------------------------
class DebugScreen : public IScreenController {
public:
    DebugScreen() = default;

    // IScreenController interface
    bool begin()                                    override;
    void onPayloadReceived(const Payload& payload)  override;  // WiFi-task safe
    void onServerStatusChanged(bool online)         override;  // WiFi-task safe
    void tick()                                     override;  // loop-task only

private:
    // ---- Hardware init helpers (all called once from begin()) ----
    bool initExpander();
    bool initPanel();
    bool initTouch();
    void initLvgl();

    // ---- LVGL UI construction (called once from begin(), after lv_init()) ----
    void buildUi();

    // ---- Applied inside tick() after spinlock drain ----
    void applyPayload(const Payload& p);
    void applyServerStatus(bool online);

    // ---- Format one PID field value into buf ----
    // Reads the field at desc.offset inside p via memcpy (safe for packed struct).
    static void formatValue(char* buf, size_t bufsz,
                            const PidDescriptor& desc,
                            const Payload& p);

    // ---- LVGL widget handles ----
    lv_obj_t* lbl_status_ = nullptr;  // "ONLINE" / "OFFLINE" in header
    lv_obj_t* lbl_ts_     = nullptr;  // last timestamp_ms in header
    lv_obj_t* table_      = nullptr;  // 6-column PID grid

    // ---- Spinlock-protected payload inbox ----
    // Receives data from WiFi task; drained by loop task in tick().
    portMUX_TYPE mux_                 = portMUX_INITIALIZER_UNLOCKED;
    Payload      pending_payload_     = {};
    bool         has_pending_payload_ = false;

    // ---- Spinlock-protected server status inbox ----
    bool pending_status_     = false;
    bool has_pending_status_ = false;

    // ---- Loop-task state ----
    bool     server_online_ = false;
    uint32_t last_tick_ms_  = 0;
};
