#include "can_driver.h"
#include "pin_config.h"

#ifndef UNIT_TEST
#include <Arduino.h>
#include "driver/twai.h"
#endif

bool CANDriver::begin() {
#ifndef UNIT_TEST
    twai_general_config_t gCfg = TWAI_GENERAL_CONFIG_DEFAULT(PIN_CAN_TX, PIN_CAN_RX, TWAI_MODE_LISTEN_ONLY);
    twai_timing_config_t  tCfg = TWAI_TIMING_CONFIG_500KBITS();
    twai_filter_config_t  fCfg = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    if (twai_driver_install(&gCfg, &tCfg, &fCfg) != ESP_OK) {
        Serial.println("TWAI driver install failed");
        return false;
    }
    if (twai_start() != ESP_OK) {
        Serial.println("TWAI start failed");
        twai_driver_uninstall();
        return false;
    }
    Serial.printf("TWAI ready: TX=GPIO%d RX=GPIO%d 500kbps\n", PIN_CAN_TX, PIN_CAN_RX);
#endif
    return true;
}

bool CANDriver::isFrameAvailable() {
#ifndef UNIT_TEST
    twai_status_info_t status;
    if (twai_get_status_info(&status) != ESP_OK) return false;
    return status.msgs_to_rx > 0;
#else
    return false;
#endif
}

bool CANDriver::readFrame(CANFrame& out_frame) {
#ifndef UNIT_TEST
    twai_message_t msg;
    if (twai_receive(&msg, pdMS_TO_TICKS(10)) != ESP_OK) return false;
    out_frame.id          = msg.identifier;
    out_frame.dlc         = msg.data_length_code;
    out_frame.is_extended = msg.extd;
    for (uint8_t i = 0; i < 8; i++) out_frame.data[i] = msg.data[i];
    return true;
#else
    (void)out_frame;
    return false;
#endif
}
