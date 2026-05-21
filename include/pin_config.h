#pragma once

#include <Arduino.h>

namespace Pins {

// Waveshare ESP32-S3-Touch-LCD-3.5B built-in devices are already wired on the
// PCB. Do not assign the built-in LCD/touch/TF-card pins to external FLIR
// signals.

// FLIR Lepton 2.5 breakout v1.4 VoSPI bus.
static constexpr int FLIR_SPI_SCLK = 38;
static constexpr int FLIR_SPI_MISO = 39;
static constexpr int FLIR_SPI_MOSI = 40;
static constexpr int FLIR_SPI_CS = 41;

// FLIR Lepton CCI/I2C control bus on the board's exposed I2C pins.
static constexpr int FLIR_CCI_SDA = 8;
static constexpr int FLIR_CCI_SCL = 7;

// Breakout v1.4 does not expose separate reset or power-enable pins.
static constexpr int FLIR_RESET = -1;
static constexpr int FLIR_POWER_ENABLE = -1;

}  // namespace Pins
