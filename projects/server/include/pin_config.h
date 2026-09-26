#pragma once

// SPI bus — shared by both MCP2515 chips
static constexpr int PIN_SPI_SCK  = 12;
static constexpr int PIN_SPI_MISO = 13;
static constexpr int PIN_SPI_MOSI = 11;

// MCP2515 CAN A (OBD-II port in use)
static constexpr int PIN_MCP2515_CS  = 10;
static constexpr int PIN_MCP2515_RST =  9;
static constexpr int PIN_MCP2515_INT =  8;

// Qwiic I2C ports (T-2CAN) — onboard IMU + environment sensors. Both are probed
// at boot, in this order, and the first port answering at a known address wins.
static constexpr int PIN_QWIIC_A_SDA = 2;    // verified by pin scan: IO2 = SDA, IO1 = SCL
static constexpr int PIN_QWIIC_A_SCL = 1;
static constexpr int PIN_QWIIC_B_SDA = 43;   // unverified; UART0 TX pin, free since Serial is USB-CDC
static constexpr int PIN_QWIIC_B_SCL = 44;   // unverified; UART0 RX pin
