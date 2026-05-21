# ESP32-S3 Touch LCD 3.5B FLIR

PlatformIO firmware scaffold for the Waveshare ESP32-S3-Touch-LCD-3.5B board
and a FLIR Lepton 2.5 module on breakout board v1.4.

The board-specific source of truth is
[Waveshare 3.5B Board Profile](docs/waveshare-35b-board-profile.md). This
project keeps the first implementation on PlatformIO Arduino and targets the
board's AXS15231B QSPI display and I2C touch path.

## Build

```bash
pio run
```

## Upload

```bash
pio run --target upload
```

## Monitor

```bash
pio device monitor
```

## Setup Button Design

The `SETUP` button is an embedded ESP32 firmware feature. It should open an
on-device LCD/touch setup screen for settings such as TF-card save path, locale,
date/time format, and orientation.

See [ESP32 Setup Button Design](docs/esp32-setup-button-design.md).

## FLIR Wiring

| FLIR breakout signal | ESP32-S3 GPIO |
|---|---:|
| `CLK` / `SCK` / `SCLK` | `GPIO38` |
| `MISO` / `DATA` / `VoSPI` | `GPIO39` |
| `MOSI` | `GPIO40` |
| `CS` | `GPIO41` |
| `SDA` / CCI SDA | `GPIO8` |
| `SCL` / CCI SCL | `GPIO7` |

The Waveshare LCD, touch controller, backlight, and TF card are built in. Do not
reuse their GPIOs for external FLIR wiring.
