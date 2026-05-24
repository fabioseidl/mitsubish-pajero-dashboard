#pragma once

// SPI bus — shared by both MCP2515 chips
static constexpr int PIN_SPI_SCK  = 12;
static constexpr int PIN_SPI_MISO = 13;
static constexpr int PIN_SPI_MOSI = 11;

// MCP2515 CAN A (OBD-II port in use)
static constexpr int PIN_MCP2515_CS  = 10;
static constexpr int PIN_MCP2515_RST =  9;
static constexpr int PIN_MCP2515_INT =  8;
