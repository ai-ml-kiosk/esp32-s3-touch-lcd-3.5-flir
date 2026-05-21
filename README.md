# ESP32-S3 Touch LCD 3.5B FLIR

PlatformIO firmware scaffold for the Waveshare ESP32-S3-Touch-LCD-3.5B board
and a FLIR Lepton 2.5 module on breakout board v1.4.

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
