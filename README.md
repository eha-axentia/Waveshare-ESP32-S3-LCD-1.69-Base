# Waveshare ESP32-S3-LCD-1.69

Firmware project for the [Waveshare ESP32-S3-LCD-1.69](https://www.waveshare.com/esp32-s3-lcd-1.69.htm) development board.

---

## Board Overview

![Board overview](https://manuals.plus/wp-content/uploads/2025/05/WAVESHARE-ESP32-S3-LCD-1.69-Low-CostHigh-Performance-MCU-Board-fig-1.png)

| # | Component | Description |
|---|-----------|-------------|
| 1 | BOOT button | Holds GPIO0 low on reset to enter download mode |
| 2 | RST button | Hard reset |
| 3 | Display connector | FPC connector to 1.69" LCD |
| 4 | PWR LED | Power indicator |
| 5 | BAT connector | MX1.25 2P — 3.7 V lithium battery |
| 6 | Charge indicator LED | Lights during battery charging |
| 7 | USB-C connector | Programming, log output, and 5 V power input |
| 8 | Passive buzzer | Sound output |
| 9 | RTC battery header | SH1.0 — rechargeable RTC backup cell |
| 10 | Onboard antenna | 2.4 GHz Wi-Fi / BLE ceramic patch |
| 11 | PWM button | Configurable: single / double / multi / long press |
| 12 | ESP32-S3R8 | Main SoC |
| 13 | PCF85063 | RTC chip |
| 14 | 12-pin header | User GPIO, I²C, UART, power |

---

## Hardware Specifications

### ESP32-S3 SoC

| Parameter | Value |
|-----------|-------|
| CPU | Xtensa LX7 dual-core, up to 240 MHz |
| Internal SRAM | 512 KB |
| Internal ROM | 384 KB |
| External Flash | 16 MB — W25Q128JVSIQ (SPI) |
| External PSRAM | 8 MB — onboard Octal SPI |
| Wi-Fi | 802.11 b/g/n, 2.4 GHz |
| Bluetooth | Bluetooth 5 LE |
| USB | Full-speed USB 1.1 OTG (native, no bridge chip) |
| Operating voltage | 3.0 V – 3.6 V |
| Operating temperature | –40 °C to 85 °C |

### Onboard ICs

| IC | Part | Function |
|----|------|----------|
| SoC | ESP32-S3R8 | MCU + 8 MB PSRAM |
| Flash | W25Q128JVSIQ | 16 MB NOR flash |
| Display driver | ST7789V2 | LCD controller (SPI) |
| IMU | QMI8658 | 6-axis (3× accel + 3× gyro) |
| RTC | PCF85063 | Real-time clock (I²C) |
| Battery charger | ETA6098 | Li-ion charge management |

### Display

| Parameter | Value |
|-----------|-------|
| Size | 1.69 inch |
| Resolution | 240 × 280 |
| Driver IC | ST7789V2 |
| Interface | 4-wire SPI |
| Color depth | 16-bit RGB565 (262K colors) |

> **Note:** The physical display bevel masks the corners of the active area with an approximate radius of **30 px**. UI elements placed within ~30 px of any corner may be partially obscured.

### Board Dimensions

![Dimensions](https://manuals.plus/wp-content/uploads/2025/05/WAVESHARE-ESP32-S3-LCD-1.69-Low-CostHigh-Performance-MCU-Board-fig-3.png)

| Measurement | Value |
|-------------|-------|
| PCB | 30.0 × 29.0 mm |
| Overall (with display) | 30.0 × 37.2 mm |
| Mounting holes | 4× Ø1.0 mm |
| Unit | mm |

---

## Pin Reference

### 12-Pin User Header

![Pinout](https://manuals.plus/wp-content/uploads/2025/05/WAVESHARE-ESP32-S3-LCD-1.69-Low-CostHigh-Performance-MCU-Board-fig-2.png)

The header exposes general-purpose GPIO and the shared I²C / UART buses. The LCD SPI lines are **not** routed to the header — they are internal traces only.

| Pin | Signal | Notes |
|-----|--------|-------|
| 1 | GPIO18 | General purpose |
| 2 | GPIO17 | General purpose |
| 3 | GPIO16 | General purpose |
| 4 | GPIO3  | General purpose |
| 5 | GPIO2  | General purpose |
| 6 | SDA — GPIO11 | I²C data (QMI8658, PCF85063) |
| 7 | SCL — GPIO10 | I²C clock (QMI8658, PCF85063) |
| 8 | U0TXD — GPIO43 | UART0 TX |
| 9 | U0RXD — GPIO44 | UART0 RX |
| 10 | 3.3 V | — |
| 11 | GND | — |
| 12 | 5 V | USB / battery boost input |

### LCD SPI Pins (internal traces — confirmed from official demo code)

| Signal | GPIO | Notes |
|--------|------|-------|
| DC     | 4    | Data / command select |
| CS     | 5    | Chip select |
| SCK    | 6    | SPI clock |
| MOSI   | 7    | SPI data |
| RST    | 8    | Hardware reset |
| BL     | 15   | Backlight enable (GPIO HIGH = on) |

### Other Internal GPIO Assignments

| Signal | GPIO | Notes |
|--------|------|-------|
| VBAT ADC | 1 | Via 200 kΩ / 100 kΩ divider (ratio × 3) |
| Buzzer | 33 or 42 | Version-dependent |
| Power control | 39 / 40 / 41 | SYS_OUT / charge enable — version-dependent |

### I²C Devices

| Device | Address | Function |
|--------|---------|----------|
| QMI8658 | 0x6B | 6-axis IMU (accel + gyro) |
| PCF85063 | 0x51 | RTC |

---

## Firmware

### ClockAndVoltage

**[ClockAndVoltage/](ClockAndVoltage/)** — Arduino sketch combining:

- **Analog clock** — reads real time from the PCF85063 RTC. On first boot (or after backup battery loss) it seeds the RTC from the compile timestamp. Hands erase cleanly by restoring tick marks before redrawing.
- **Battery voltage gauge** — reads the VBAT rail via the onboard 200 kΩ / 100 kΩ divider on GPIO 1. Displays a colour-coded bar (green → yellow → red) with percentage and voltage, updated every 10 s.

**Build with PlatformIO** — libraries are fetched automatically:

```powershell
pio run -t upload && pio device monitor
```

---

## References

- [Waveshare Wiki — ESP32-S3-LCD-1.69](https://www.waveshare.com/wiki/ESP32-S3-LCD-1.69)
- [ESP32-S3 Datasheet](https://files.waveshare.com/wiki/common/Esp32-s3_datasheet_en.pdf)
- [ESP32-S3 Technical Reference Manual](https://files.waveshare.com/wiki/common/Esp32-s3_technical_reference_manual_en.pdf)
- [Espressif ESP-IDF Docs — ESP32-S3](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/)
- [Waveshare ESP32 Components (BSP)](https://github.com/waveshareteam/Waveshare-ESP32-components)
- [QMI8658A Datasheet (local notes)](Documents/QMI8658A_Datasheet.md) — IMU register map, STATUS1 flags, tap/motion config parameters
