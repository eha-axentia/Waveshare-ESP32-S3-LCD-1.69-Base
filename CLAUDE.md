# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Target Hardware

**Board:** Waveshare ESP32-S3-LCD-1.69
- MCU: ESP32-S3R8 (dual-core Xtensa LX7, up to 240 MHz, 8 MB PSRAM)
- Display: 1.69" LCD, 240×280 resolution, ST7789V2 driver, 4-wire SPI
- Flash: 16 MB (W25Q128JVSIQ), PSRAM: 8 MB (Octal SPI, on-chip)
- USB: USB-C (ESP32-S3 native USB OTG — no bridge chip)
- Connectivity: Wi-Fi 802.11 b/g/n 2.4 GHz, Bluetooth 5 LE
- Sensors: QMI8658 IMU (I²C), PCF85063 RTC (I²C)
- Power: ETA6098 Li-ion charger, MX1.25 battery header

**LCD SPI pins (internal traces — confirmed from official demo code):**
| Signal | GPIO |
|--------|------|
| DC     | 4    |
| CS     | 5    |
| SCK    | 6    |
| MOSI   | 7    |
| RST    | 8    |
| BL     | 15   |

**12-pin user header (general-purpose GPIO — not connected to LCD):**
| Pin | Signal | Notes |
|-----|--------|-------|
| 1 | GPIO18 | General purpose |
| 2 | GPIO17 | General purpose |
| 3 | GPIO16 | General purpose |
| 4 | GPIO3  | General purpose |
| 5 | GPIO2  | General purpose |
| 6 | GPIO11 | I²C SDA (shared with QMI8658 / PCF85063) |
| 7 | GPIO10 | I²C SCL (shared with QMI8658 / PCF85063) |
| 8 | GPIO43 | UART0 TX |
| 9 | GPIO44 | UART0 RX |
| 10 | 3.3 V | — |
| 11 | GND   | — |
| 12 | 5 V   | — |

**Other internal GPIO assignments:**
- GPIO1 — VBAT ADC input (200 kΩ / 100 kΩ divider, ratio × 3)
- GPIO11 / GPIO10 — I²C SDA / SCL (QMI8658 @ 0x6B, PCF85063 @ 0x51)

## Build System

This project uses **PlatformIO** with the Arduino framework.

### Common commands

```powershell
pio run                              # build
pio run -t upload                    # build and flash
pio device monitor                   # serial monitor (115200 baud)
pio run -t upload && pio device monitor  # flash then monitor
pio run -t clean                     # clean build artefacts
```

Specify the port explicitly if auto-detection fails:
```powershell
pio run -t upload --upload-port COM3
```

### Key platformio.ini settings

| Setting | Value | Reason |
|---------|-------|--------|
| `board` | `esp32-s3-devkitc-1` | generic ESP32-S3 target |
| `board_build.flash_size` | `16MB` | W25Q128JVSIQ |
| `board_build.arduino.memory_type` | `qio_opi` | 16 MB QIO flash + OPI PSRAM |
| `board_build.partitions` | `default_16MB.csv` | full 16 MB layout |
| `ARDUINO_USB_CDC_ON_BOOT` | `1` | native USB, no bridge chip |

### Project Structure

```
platformio.ini        # build configuration
include/
  pin_config.h        # all GPIO definitions
src/
  main.cpp            # application entry point
ClockAndVoltage/      # original Arduino sketch (reference copy)
```

## Display Driver Notes

The ST7789V2 display requires initialization via SPI. Key points:
- `Arduino_ST7789` constructor needs `y-offset = 20` for this panel.
- IPS mode must be `true`.
- Backlight (GPIO15) is GPIO-controlled (HIGH = on); no PWM in current sketch.
- `startWrite()` / `endWrite()` batch SPI transactions — use them around multi-step draws.
- `LV_COLOR_16_SWAP=1` required when driving the display via LVGL over SPI.

## Peripheral Notes

- **I²C bus** (SDA=11, SCL=10) is shared; both sensors are initialized through `Wire`.
- **PCF85063 RTC**: `SensorPCF85063::isRunning()` returns false on first boot or after backup battery loss — seed with compile time then.
- **QMI8658 IMU**: check `getDataReady()` before reading accelerometer / gyroscope data.
- **VBAT ADC**: average multiple `analogRead(1)` samples to reduce noise; apply ×3 divider ratio.

## LVGL Integration (if used)

- Flush callback: call `lv_disp_flush_ready()` after `draw16bitBeRGBBitmap()` completes.
- Tick source: `esp_timer` calling `lv_tick_inc(2)` every 2 ms.
- Display buffer: `lv_color_t buf[240 * 28]` (1/10 of screen).
