#pragma once

#include "payload.h"

// Manual LVGL 9.5 dashboard for the 7-inch RGB display.
//
// Clean three-function API, fully decoupled from the display/touch plumbing:
//   - create()            builds the widget tree on the active screen (call once,
//                         after lv_init() and the LVGL display is registered)
//   - update()            pushes a received Payload into the value labels
//   - set_server_status() toggles the ONLINE/OFFLINE indicator
//
// All functions must be called from the LVGL thread (the Arduino loop / tick).
// No LVGL object pointers are exposed in this header — they are private to the
// implementation, as required by the architecture spec.
namespace dashboard {

void create();
void update(const Payload& p);
void set_server_status(bool online);

}  // namespace dashboard
