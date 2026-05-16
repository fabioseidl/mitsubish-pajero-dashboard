#pragma once

#include "driver/gpio.h"

static constexpr gpio_num_t PIN_CAN_TX = GPIO_NUM_5; // TWAI TX <- ESP32 GPIO5
static constexpr gpio_num_t PIN_CAN_RX = GPIO_NUM_4; // TWAI RX -> ESP32 GPIO4
