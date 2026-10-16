#pragma once

#include <stdint.h>
#include <lvgl.h>

/**
 * The CYD HUD's LVGL screens for the 320x240 panel — pure LVGL, no board code,
 * so it builds for the firmware and for the native simulator (sim/).
 *
 * Boot screen (logo + "PAJERO DAKAR") fades into the HUD screen (speed, rpm,
 * server-offline icon) after BOOT_HOLD_MS.
 */
class SimpleHudUi {
public:
    static constexpr uint32_t BOOT_HOLD_MS = 3000;  // boot screen time before the fade starts
    static constexpr uint32_t BOOT_FADE_MS = 200;

    /** Builds both screens, shows the boot screen and schedules the fade to the HUD. */
    void build();

    /** Shows the speed in km/h. */
    void setSpeed(uint8_t kmh);

    /** Shows the engine speed in rpm. */
    void setRpm(uint16_t rpm);

    /** Hides the server-offline icon while the server is online. */
    void setServerOnline(bool online);

private:
    lv_obj_t* boot_        = nullptr;
    lv_obj_t* hud_         = nullptr;
    lv_obj_t* speed_       = nullptr;
    lv_obj_t* rpm_         = nullptr;
    lv_obj_t* server_icon_ = nullptr;
};
