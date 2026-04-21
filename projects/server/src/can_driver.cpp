#include "can_driver.h"

#ifndef UNIT_TEST
#include <SPI.h>
#include <mcp_can.h>
#endif

CANDriver::CANDriver(uint8_t cs_pin, uint8_t int_pin)
    : cs_pin_(cs_pin), int_pin_(int_pin) {}

bool CANDriver::begin() {
#ifndef UNIT_TEST
    SPI.begin();
    MCP_CAN mcp_can(cs_pin_);
    if (mcp_can.begin(MCP_ANY, CAN_500KBPS, MCP_8MHZ) != CAN_OK) {
        return false;
    }
    mcp_can.setMode(MCP_NORMAL);
    pinMode(int_pin_, INPUT);
#endif
    return true;
}

bool CANDriver::isFrameAvailable() {
#ifndef UNIT_TEST
    return digitalRead(int_pin_) == LOW;
#else
    return false;
#endif
}

bool CANDriver::readFrame(CANFrame& out_frame) {
#ifndef UNIT_TEST
    MCP_CAN mcp_can(cs_pin_);
    uint8_t buf[8] = {};
    uint8_t dlc    = 0;
    uint32_t id    = 0;
    if (mcp_can.readMsgBuf(&id, &dlc, buf) != CAN_OK) {
        return false;
    }
    out_frame.id          = id;
    out_frame.dlc         = dlc;
    out_frame.is_extended = (mcp_can.isExtendedFrame() == 1);
    for (uint8_t i = 0; i < 8; i++) out_frame.data[i] = buf[i];
    return true;
#else
    (void)out_frame;
    return false;
#endif
}
