# ESP32-S3 Touch LCD 3.5B FLIR

PlatformIO firmware scaffold for the Waveshare ESP32-S3-Touch-LCD-3.5B board
and a FLIR Lepton 2.5 module on breakout board v1.4.

The board-specific source of truth is
[Waveshare 3.5B Board Profile](docs/waveshare-35b-board-profile.md). This
project keeps the first implementation on PlatformIO Arduino and targets the
board's AXS15231B QSPI display and I2C touch path.

The default firmware path reads real FLIR Lepton 2.x VoSPI frames. A synthetic
thermal frame source remains available only for bring-up builds with
`FLIR_USE_SYNTHETIC_FRAMES`.

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
on-device LCD/touch setup screen for settings such as TF-card save path,
orientation policy, calibration offset, raw/filename capture options, Auto FFC,
clip duration, sound volume, and reserved scrollable fields.

See [ESP32 Setup Button Design](docs/esp32-setup-button-design.md).

For hardware bring-up notes, known symptoms, and debugging actions already
performed on this board, see
[Troubleshooting Actions Log](docs/troubleshooting-actions.md).

## FLIR Wiring

| FLIR breakout signal | ESP32-S3 GPIO |
|---|---:|
| `CLK` / `SCK` / `SCLK` | `GPIO21` |
| `MISO` / `DATA` / `VoSPI` | `GPIO40` |
| `MOSI` | `GPIO41` |
| `CS` | `GPIO42` |
| `SDA` / CCI SDA | `GPIO17` |
| `SCL` / CCI SCL | `GPIO18` |

The Waveshare LCD, touch controller, backlight, and TF card are built in. Do not
reuse their GPIOs for external FLIR wiring. In particular, keep `GPIO7` and
`GPIO8` reserved for the board I2C path.
