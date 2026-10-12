#pragma once
// Pin map for the diagnostic node. Matches projects/server and the sniffer:
// ESP32-S3 + MCP2515 on CAN A (OBD-II port).

static constexpr int PIN_SPI_SCK  = 12;
static constexpr int PIN_SPI_MISO = 13;
static constexpr int PIN_SPI_MOSI = 11;

static constexpr int PIN_MCP2515_CS  = 10;   // CAN A — OBD-II port
static constexpr int PIN_MCP2515_RST = 14;
