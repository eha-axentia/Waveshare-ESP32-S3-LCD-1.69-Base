#pragma once

// ── LCD (ST7789V2, 4-wire SPI — internal traces, not on header) ───────────
#define LCD_DC    4
#define LCD_CS    5
#define LCD_SCK   6
#define LCD_MOSI  7
#define LCD_RST   8
#define LCD_BL    15
#define LCD_WIDTH  240
#define LCD_HEIGHT 280

// ── I²C bus (shared: QMI8658 @ 0x6B, PCF85063 @ 0x51) ───────────────────
#define IIC_SDA   11
#define IIC_SCL   10

// ── Battery voltage ADC (200 kΩ / 100 kΩ divider → ratio × 3) ───────────
#define VBAT_PIN  1
