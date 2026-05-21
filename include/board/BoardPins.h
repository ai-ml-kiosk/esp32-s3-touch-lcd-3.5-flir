#pragma once

#include <Arduino.h>

namespace BoardPins {

static constexpr int I2C_SDA = 8;
static constexpr int I2C_SCL = 7;
static constexpr uint32_t I2C_FREQUENCY = 400000;

static constexpr int LCD_QSPI_CS = 12;
static constexpr int LCD_QSPI_CLK = 5;
static constexpr int LCD_QSPI_D0 = 1;
static constexpr int LCD_QSPI_D1 = 2;
static constexpr int LCD_QSPI_D2 = 3;
static constexpr int LCD_QSPI_D3 = 4;
static constexpr int LCD_BACKLIGHT = 6;

static constexpr uint8_t TCA9554_ADDRESS = 0x20;
static constexpr uint8_t TCA9554_LCD_RESET_PIN = 1;

static constexpr uint16_t LCD_PHYSICAL_WIDTH = 320;
static constexpr uint16_t LCD_PHYSICAL_HEIGHT = 480;
static constexpr uint16_t LCD_LANDSCAPE_WIDTH = 480;
static constexpr uint16_t LCD_LANDSCAPE_HEIGHT = 320;

static constexpr int TF_CLK = 11;
static constexpr int TF_CMD = 10;
static constexpr int TF_D0 = 9;

}  // namespace BoardPins
