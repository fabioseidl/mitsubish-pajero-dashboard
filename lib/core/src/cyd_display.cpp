#include "cyd_display.h"

#ifndef UNIT_TEST
#include <driver/ledc.h>
#endif

CYDDisplay::CYDDisplay(uint8_t backlight_pin, uint8_t pwm_channel)
    : backlight_pin_(backlight_pin), pwm_channel_(pwm_channel) {}

bool CYDDisplay::begin() {
#ifndef UNIT_TEST
    ledc_timer_config_t timer = {};
    timer.speed_mode       = LEDC_LOW_SPEED_MODE;
    timer.timer_num        = LEDC_TIMER_0;
    timer.duty_resolution  = LEDC_TIMER_13_BIT;
    timer.freq_hz          = 5000;
    timer.clk_cfg          = LEDC_AUTO_CLK;
    ledc_timer_config(&timer);

    ledc_channel_config_t channel = {};
    channel.speed_mode     = LEDC_LOW_SPEED_MODE;
    channel.channel        = (ledc_channel_t)pwm_channel_;
    channel.timer_sel      = LEDC_TIMER_0;
    channel.gpio_num       = backlight_pin_;
    channel.duty           = percentToDuty(75);
    channel.hpoint         = 0;
    ledc_channel_config(&channel);
#endif
    return true;
}

void CYDDisplay::setBacklightPercent(uint8_t percent) {
    if (percent > 100) percent = 100;
#ifndef UNIT_TEST
    ledc_set_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)pwm_channel_, percentToDuty(percent));
    ledc_update_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)pwm_channel_);
#endif
}

uint32_t CYDDisplay::percentToDuty(uint8_t percent) const {
    return (uint32_t)((percent / 100.0f) * 8191);
}
