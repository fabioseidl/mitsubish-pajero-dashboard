#include "can_driver.h"
#include "pin_config.h"

#ifndef UNIT_TEST
#include <Arduino.h>
#ifdef USE_MCP2515
#include <SPI.h>
#include <mcp2515.h>

static MCP2515 mcp2515(PIN_MCP2515_CS, 10000000, &SPI);
#endif
#endif

bool CANDriver::begin() {
#ifndef UNIT_TEST
#ifdef USE_MCP2515
    // Hardware reset: HIGH → LOW → HIGH, matching supplier example
    pinMode(PIN_MCP2515_RST, OUTPUT);
    digitalWrite(PIN_MCP2515_RST, HIGH);
    delay(100);
    digitalWrite(PIN_MCP2515_RST, LOW);
    delay(100);
    digitalWrite(PIN_MCP2515_RST, HIGH);
    delay(100);

    SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, PIN_MCP2515_CS);

    mcp2515.reset();

    MCP2515::ERROR err = mcp2515.setBitrate(CAN_500KBPS);
    if (err != MCP2515::ERROR_OK) {
        Serial.printf("MCP2515 setBitrate failed (err=%d)\n", (int)err);
        return false;
    }
    if (mcp2515.setNormalMode() != MCP2515::ERROR_OK) {
        Serial.println("MCP2515 setNormalMode failed");
        return false;
    }
    Serial.printf("MCP2515 ready: SCK=%d MISO=%d MOSI=%d CS=%d RST=%d\n",
        PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, PIN_MCP2515_CS, PIN_MCP2515_RST);
#endif
#endif
    return true;
}

bool CANDriver::isFrameAvailable() {
#ifndef UNIT_TEST
#ifdef USE_MCP2515
    uint8_t stat = mcp2515.getStatus();
    return (stat & 0x03) != 0;
#endif
#endif
    return false;
}

bool CANDriver::readFrame(CANFrame& out_frame) {
#ifndef UNIT_TEST
#ifdef USE_MCP2515
    struct can_frame msg;
    if (mcp2515.readMessage(&msg) != MCP2515::ERROR_OK) return false;
    out_frame.id          = msg.can_id & CAN_EFF_MASK;
    out_frame.dlc         = msg.can_dlc;
    out_frame.is_extended = (msg.can_id & CAN_EFF_FLAG) != 0;
    for (uint8_t i = 0; i < 8; i++) out_frame.data[i] = msg.data[i];
    return true;
#endif
#endif
    (void)out_frame;
    return false;
}

bool CANDriver::sendFrame(const CANFrame& frame) {
#ifndef UNIT_TEST
#ifdef USE_MCP2515
    struct can_frame msg = {};
    msg.can_id  = frame.id;
    if (frame.is_extended) msg.can_id |= CAN_EFF_FLAG;
    msg.can_dlc = frame.dlc;
    for (uint8_t i = 0; i < frame.dlc; i++) msg.data[i] = frame.data[i];
    return mcp2515.sendMessage(&msg) == MCP2515::ERROR_OK;
#endif
#endif
    (void)frame;
    return true;
}
