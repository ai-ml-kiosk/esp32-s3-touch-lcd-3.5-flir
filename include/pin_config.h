#pragma once

#include <Arduino.h>

namespace Pins {

// Waveshare ESP32-S3-Touch-LCD-3.5B built-in devices are already wired on the
// PCB. Do not assign the built-in LCD/touch/TF-card pins to external FLIR
// signals.

// FLIR Lepton breakout v1.4 VoSPI bus.
// Keep FLIR off GPIO35-GPIO39 on ESP32-S3 boards because those pins can be
// tied to flash/PSRAM/FSPI behavior on some modules and are fragile for
// externally-driven camera signals.
static constexpr int FLIR_SPI_SCLK = 21;
static constexpr int FLIR_SPI_MISO = 40;
static constexpr int FLIR_SPI_MOSI = 41;
static constexpr int FLIR_SPI_CS = 42;

// FLIR Lepton CCI/I2C control bus on a dedicated exposed pin pair. The
// Waveshare onboard LCD/touch/control path owns GPIO8/GPIO7.
static constexpr int FLIR_CCI_SDA = 17;
static constexpr int FLIR_CCI_SCL = 18;

// Breakout v1.4 does not expose separate reset or power-enable pins.
static constexpr int FLIR_RESET = -1;
static constexpr int FLIR_POWER_ENABLE = -1;

}  // namespace Pins
