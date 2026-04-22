#pragma once

#include <stdint.h>
#include "i_screen_controller.h"
#include "brightness_controller.h"

#ifndef UNIT_TEST
#include <src/lvgl.h>
#else
struct lv_obj_t;
#endif

class CYDScreenController : public IScreenController {
public:
    explicit CYDScreenController(BrightnessController& brightness);

    bool begin() override;
    void onPayloadReceived(const Payload& payload) override;
    void onServerStatusChanged(bool online) override;
    void tick() override;

private:
    BrightnessController& brightness_;

    lv_obj_t* rpm_arc_;
    lv_obj_t* rpm_label_;
    lv_obj_t* speed_label_;
    lv_obj_t* consumption_label_;
    lv_obj_t* avg_consumption_label_;
    lv_obj_t* distance_label_;
    lv_obj_t* mil_indicator_;
    lv_obj_t* server_status_label_;

    uint32_t last_tick_ms_;
    uint32_t last_touch_ms_;
    bool     touch_pressed_;
};
