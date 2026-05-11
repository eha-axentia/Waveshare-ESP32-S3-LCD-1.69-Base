/*
 * main.cpp — Waveshare ESP32-S3-LCD-1.69
 *
 * Analog clock (lv_meter) + battery gauge (lv_bar).
 * 8 colour palettes × day / night variants.
 *
 * Gestures (QMI8658):
 *   Single tap  → next palette  (8 moods: Forest → Ocean → Sunset → Midnight
 *                                           → Fire → Arctic → Neon → Copper)
 *   Double tap  → toggle day / night
 *   Shake       → animate clock hands to a random time
 */

#include <Arduino.h>
#include <Wire.h>
#include "Arduino_GFX_Library.h"
#include "SensorPCF85063.hpp"
#include "SensorQMI8658.hpp"
#include "pin_config.h"
#include "lvgl.h"
#include "esp_timer.h"

/* ── Display ─────────────────────────────────────────────────────────────── */
static Arduino_DataBus *bus =
    new Arduino_ESP32SPI(LCD_DC, LCD_CS, LCD_SCK, LCD_MOSI);
static Arduino_GFX *gfx =
    new Arduino_ST7789(bus, LCD_RST, 0, true,
                       LCD_WIDTH, LCD_HEIGHT, 0, 20, 0, 0);

static lv_color_t lvBuf[LCD_WIDTH * 28];

/* ── Peripherals ─────────────────────────────────────────────────────────── */
static SensorPCF85063 rtc;
static SensorQMI8658  imu;
static bool rtcOk = false;
static bool imuOk = false;

/* ═════════════════════════════════════════════════════════════════════════ *
 *  Colour palettes
 * ═════════════════════════════════════════════════════════════════════════ */

struct Palette {
    lv_color_t bg;          /* screen + battery panel background */
    lv_color_t clockBg;     /* clock face fill                   */
    lv_color_t accent;      /* border, separator, major ticks    */
    lv_color_t tickMinor;   /* minor tick marks                  */
    lv_color_t handH;       /* hour hand                         */
    lv_color_t handM;       /* minute hand                       */
    lv_color_t handS;       /* second hand                       */
    lv_color_t barBg;       /* battery bar track                 */
    lv_color_t barFill;     /* battery bar indicator             */
    lv_color_t textDim;     /* "BATTERY" label                   */
    lv_color_t textBright;  /* voltage + percentage text         */
};

/* PALETTES[mood][0 = day, 1 = night]  —  initialised in initPalettes() */
static Palette PALETTES[8][2];

static void initPalettes()
{
    /* ── 0  Forest ─────────────────────────────────────────────────────── */
    PALETTES[0][0] = {                              /* day   */
        lv_color_make(0xEB,0xF5,0xE8),  /* bg         */
        lv_color_make(0xF5,0xFF,0xF5),  /* clockBg    */
        lv_color_make(0x22,0x8B,0x22),  /* accent     */
        lv_color_make(0x4A,0x8A,0x4A),  /* tickMinor  */
        lv_color_make(0x1A,0x3A,0x1A),  /* handH      */
        lv_color_make(0x00,0x66,0x22),  /* handM      */
        lv_color_make(0xCC,0x22,0x22),  /* handS      */
        lv_color_make(0xC8,0xE0,0xC8),  /* barBg      */
        lv_color_make(0x22,0x8B,0x22),  /* barFill    */
        lv_color_make(0x5A,0x8A,0x5A),  /* textDim    */
        lv_color_make(0x1A,0x3A,0x1A),  /* textBright */
    };
    PALETTES[0][1] = {                              /* night */
        lv_color_make(0x0D,0x1A,0x0D),
        lv_color_make(0x0A,0x14,0x0A),
        lv_color_make(0x1E,0x7A,0x1E),
        lv_color_make(0x2A,0x40,0x20),
        lv_color_make(0xE0,0xFF,0xE0),
        lv_color_make(0x00,0xEE,0x77),
        lv_color_make(0xFF,0x40,0x40),
        lv_color_make(0x15,0x22,0x15),
        lv_color_make(0x22,0xBB,0x44),
        lv_color_make(0x3A,0x6A,0x3A),
        lv_color_make(0xAA,0xFF,0xAA),
    };

    /* ── 1  Ocean ──────────────────────────────────────────────────────── */
    PALETTES[1][0] = {                              /* day   */
        lv_color_make(0xE0,0xF0,0xFF),
        lv_color_make(0xF0,0xF8,0xFF),
        lv_color_make(0x00,0x66,0xBB),
        lv_color_make(0x3A,0x7A,0xBB),
        lv_color_make(0x00,0x22,0x44),
        lv_color_make(0x00,0x55,0xAA),
        lv_color_make(0xFF,0x44,0x00),
        lv_color_make(0xB0,0xCC,0xE8),
        lv_color_make(0x00,0x66,0xAA),
        lv_color_make(0x3A,0x6A,0xA0),
        lv_color_make(0x00,0x22,0x44),
    };
    PALETTES[1][1] = {                              /* night */
        lv_color_make(0x03,0x0E,0x1A),
        lv_color_make(0x04,0x0F,0x20),
        lv_color_make(0x00,0x55,0xBB),
        lv_color_make(0x1A,0x30,0x50),
        lv_color_make(0xBB,0xEE,0xFF),
        lv_color_make(0x00,0xCC,0xFF),
        lv_color_make(0xFF,0x77,0x00),
        lv_color_make(0x0A,0x1A,0x2A),
        lv_color_make(0x00,0x88,0xCC),
        lv_color_make(0x2A,0x5A,0x7A),
        lv_color_make(0x88,0xDD,0xFF),
    };

    /* ── 2  Sunset ─────────────────────────────────────────────────────── */
    PALETTES[2][0] = {                              /* day   */
        lv_color_make(0xFF,0xF5,0xE8),
        lv_color_make(0xFF,0xFA,0xF5),
        lv_color_make(0xBB,0x44,0x00),
        lv_color_make(0x9A,0x55,0x20),
        lv_color_make(0x33,0x11,0x00),
        lv_color_make(0xBB,0x44,0x00),
        lv_color_make(0xCC,0x00,0x44),
        lv_color_make(0xE8,0xC8,0xA0),
        lv_color_make(0xCC,0x55,0x00),
        lv_color_make(0x9A,0x55,0x30),
        lv_color_make(0x33,0x11,0x00),
    };
    PALETTES[2][1] = {                              /* night */
        lv_color_make(0x1A,0x08,0x00),
        lv_color_make(0x1F,0x0A,0x00),
        lv_color_make(0xCC,0x44,0x00),
        lv_color_make(0x4A,0x20,0x00),
        lv_color_make(0xFF,0xCC,0x88),
        lv_color_make(0xFF,0x77,0x22),
        lv_color_make(0xFF,0x00,0x66),
        lv_color_make(0x2A,0x12,0x00),
        lv_color_make(0xFF,0x77,0x22),
        lv_color_make(0x7A,0x3A,0x1A),
        lv_color_make(0xFF,0xCC,0x88),
    };

    /* ── 3  Midnight ───────────────────────────────────────────────────── */
    PALETTES[3][0] = {                              /* day   */
        lv_color_make(0xF0,0xE8,0xFF),
        lv_color_make(0xFA,0xF5,0xFF),
        lv_color_make(0x66,0x22,0xBB),
        lv_color_make(0x66,0x44,0xAA),
        lv_color_make(0x1A,0x00,0x44),
        lv_color_make(0x55,0x22,0xAA),
        lv_color_make(0xBB,0x00,0x44),
        lv_color_make(0xD8,0xC0,0xEE),
        lv_color_make(0x66,0x22,0xBB),
        lv_color_make(0x77,0x55,0xAA),
        lv_color_make(0x1A,0x00,0x44),
    };
    PALETTES[3][1] = {                              /* night */
        lv_color_make(0x08,0x00,0x10),
        lv_color_make(0x0D,0x00,0x18),
        lv_color_make(0x55,0x00,0xCC),
        lv_color_make(0x2A,0x10,0x50),
        lv_color_make(0xCC,0xAA,0xFF),
        lv_color_make(0x99,0x44,0xFF),
        lv_color_make(0xFF,0x44,0xAA),
        lv_color_make(0x14,0x08,0x2A),
        lv_color_make(0x77,0x22,0xDD),
        lv_color_make(0x5A,0x3A,0x7A),
        lv_color_make(0xCC,0xAA,0xFF),
    };

    /* ── 4  Fire ───────────────────────────────────────────────────────── */
    PALETTES[4][0] = {                              /* day   */
        lv_color_make(0xFF,0xF0,0xF0),
        lv_color_make(0xFF,0xF8,0xF8),
        lv_color_make(0xCC,0x00,0x00),
        lv_color_make(0xAA,0x44,0x44),
        lv_color_make(0x44,0x00,0x00),
        lv_color_make(0xCC,0x00,0x00),
        lv_color_make(0xBB,0x66,0x00),
        lv_color_make(0xEE,0xCC,0xCC),
        lv_color_make(0xCC,0x22,0x00),
        lv_color_make(0xAA,0x55,0x55),
        lv_color_make(0x44,0x00,0x00),
    };
    PALETTES[4][1] = {                              /* night */
        lv_color_make(0x14,0x00,0x00),
        lv_color_make(0x1A,0x00,0x00),
        lv_color_make(0xCC,0x00,0x00),
        lv_color_make(0x4A,0x10,0x10),
        lv_color_make(0xFF,0xAA,0xAA),
        lv_color_make(0xFF,0x44,0x44),
        lv_color_make(0xFF,0xAA,0x00),
        lv_color_make(0x28,0x08,0x08),
        lv_color_make(0xDD,0x22,0x00),
        lv_color_make(0x7A,0x22,0x22),
        lv_color_make(0xFF,0xAA,0xAA),
    };

    /* ── 5  Arctic ─────────────────────────────────────────────────────── */
    PALETTES[5][0] = {                              /* day   */
        lv_color_make(0xF0,0xFC,0xFF),
        lv_color_make(0xFA,0xFF,0xFF),
        lv_color_make(0x00,0x99,0xCC),
        lv_color_make(0x2A,0x8A,0xAA),
        lv_color_make(0x00,0x33,0x44),
        lv_color_make(0x00,0x88,0xBB),
        lv_color_make(0xFF,0x44,0x00),
        lv_color_make(0xA8,0xDD,0xE8),
        lv_color_make(0x00,0x88,0xBB),
        lv_color_make(0x3A,0x8A,0x9A),
        lv_color_make(0x00,0x22,0x33),
    };
    PALETTES[5][1] = {                              /* night */
        lv_color_make(0x00,0x0A,0x14),
        lv_color_make(0x00,0x0D,0x1A),
        lv_color_make(0x00,0x99,0xCC),
        lv_color_make(0x1A,0x3A,0x4A),
        lv_color_make(0xAA,0xEE,0xFF),
        lv_color_make(0x44,0xDD,0xFF),
        lv_color_make(0xFF,0x44,0x00),
        lv_color_make(0x0A,0x1F,0x2A),
        lv_color_make(0x00,0x99,0xCC),
        lv_color_make(0x2A,0x6A,0x7A),
        lv_color_make(0xAA,0xEE,0xFF),
    };

    /* ── 6  Neon ───────────────────────────────────────────────────────── */
    PALETTES[6][0] = {                              /* day   */
        lv_color_make(0xF0,0xF0,0xF8),
        lv_color_make(0xFF,0xFF,0xFF),
        lv_color_make(0x00,0xAA,0xCC),
        lv_color_make(0x99,0xBB,0xCC),
        lv_color_make(0x1A,0x1A,0x2A),
        lv_color_make(0x00,0xAA,0xCC),
        lv_color_make(0xCC,0x00,0xCC),
        lv_color_make(0xD0,0xD8,0xE8),
        lv_color_make(0x00,0xAA,0xCC),
        lv_color_make(0x66,0x88,0xAA),
        lv_color_make(0x1A,0x1A,0x2A),
    };
    PALETTES[6][1] = {                              /* night */
        lv_color_make(0x00,0x00,0x00),
        lv_color_make(0x03,0x03,0x06),
        lv_color_make(0x00,0xFF,0xCC),
        lv_color_make(0x0A,0x3A,0x2A),
        lv_color_make(0xFF,0xFF,0xFF),
        lv_color_make(0x00,0xFF,0xCC),
        lv_color_make(0xFF,0x00,0xFF),
        lv_color_make(0x0A,0x0A,0x0A),
        lv_color_make(0x00,0xFF,0xCC),
        lv_color_make(0x00,0x88,0x66),
        lv_color_make(0x00,0xFF,0xCC),
    };

    /* ── 7  Copper ─────────────────────────────────────────────────────── */
    PALETTES[7][0] = {                              /* day   */
        lv_color_make(0xFF,0xF5,0xE8),
        lv_color_make(0xFF,0xFA,0xF5),
        lv_color_make(0x8B,0x5A,0x1A),
        lv_color_make(0x8B,0x60,0x30),
        lv_color_make(0x3A,0x1A,0x00),
        lv_color_make(0x8B,0x5A,0x1A),
        lv_color_make(0xCC,0x22,0x00),
        lv_color_make(0xDD,0xB8,0x88),
        lv_color_make(0x8B,0x5A,0x1A),
        lv_color_make(0x8B,0x60,0x40),
        lv_color_make(0x3A,0x1A,0x00),
    };
    PALETTES[7][1] = {                              /* night */
        lv_color_make(0x0F,0x0A,0x05),
        lv_color_make(0x16,0x0E,0x06),
        lv_color_make(0xB8,0x73,0x33),
        lv_color_make(0x3A,0x20,0x10),
        lv_color_make(0xD4,0x9A,0x6A),
        lv_color_make(0xB8,0x73,0x33),
        lv_color_make(0xFF,0x44,0x00),
        lv_color_make(0x1F,0x12,0x08),
        lv_color_make(0xB8,0x73,0x33),
        lv_color_make(0x6A,0x40,0x20),
        lv_color_make(0xD4,0x9A,0x6A),
    };
}

static uint8_t paletteIdx = 0;
static bool    isDark     = true;

static inline const Palette &pal()
{
    return PALETTES[paletteIdx][isDark ? 1 : 0];
}

/* ── UI handles ──────────────────────────────────────────────────────────── */
static lv_obj_t             *clockMeter = nullptr;
static lv_meter_indicator_t *indH       = nullptr;
static lv_meter_indicator_t *indM       = nullptr;
static lv_meter_indicator_t *indS       = nullptr;
static lv_obj_t *sep      = nullptr;
static lv_obj_t *cont     = nullptr;
static lv_obj_t *batBar   = nullptr;
static lv_obj_t *batVolts = nullptr;
static lv_obj_t *batPct   = nullptr;

/* ── Level bubble ─────────────────────────────────────────────────────────── */
static lv_obj_t *levelBubble = nullptr;
static lv_obj_t *levelCenter = nullptr;

static constexpr int16_t BUBBLE_R     =  8;   /* bubble radius px            */
static constexpr int16_t BUBBLE_RANGE = 68;   /* max offset from clock centre */
static constexpr int16_t CLOCK_CX     = 120;  /* 240/2                        */
static constexpr int16_t CLOCK_CY     = 114;  /* 2 (y_ofs) + 224/2            */

/* ── Time state ──────────────────────────────────────────────────────────── */
static int16_t tH = 12, tM = 0, tS = 0;
static unsigned long nextSecond  = 0;
static unsigned long nextVoltage = 0;

/* ── Gesture state ───────────────────────────────────────────────────────── */
static unsigned long tapDebounceUntil   = 0;
static unsigned long shakeDebounceUntil = 0;
static unsigned long shakeRtcResumeAt  = 0;

/* ═════════════════════════════════════════════════════════════════════════ *
 *  LVGL integration
 * ═════════════════════════════════════════════════════════════════════════ */

/* ── Direct QMI8658 register read (SensorLib has no public TAP_STATUS getter) */
static uint8_t readQMIReg(uint8_t reg)
{
    Wire.beginTransmission(QMI8658_L_SLAVE_ADDRESS);
    Wire.write(reg);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)QMI8658_L_SLAVE_ADDRESS, (uint8_t)1);
    return Wire.available() ? (uint8_t)Wire.read() : 0;
}

static void flushCb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *buf)
{
    uint32_t w = area->x2 - area->x1 + 1;
    uint32_t h = area->y2 - area->y1 + 1;
    gfx->draw16bitBeRGBBitmap(area->x1, area->y1, (uint16_t *)buf, w, h);
    lv_disp_flush_ready(drv);
}

static void tickCb(void *) { lv_tick_inc(2); }

/* ═════════════════════════════════════════════════════════════════════════ *
 *  Helpers
 * ═════════════════════════════════════════════════════════════════════════ */

static uint8_t parse2(const char *p) { return (p[0]-'0')*10 + (p[1]-'0'); }

static uint8_t parseMonth(const char *d)
{
    const char *names[12] = { "Jan","Feb","Mar","Apr","May","Jun",
                               "Jul","Aug","Sep","Oct","Nov","Dec" };
    for (uint8_t i = 0; i < 12; i++)
        if (strncmp(d, names[i], 3) == 0) return i + 1;
    return 1;
}

static int16_t hourVal(int h, int m) { return (h % 12) * 5 + m / 12; }

static float readVoltage()
{
    int32_t sum = 0;
    for (int i = 0; i < 16; i++) sum += analogRead(VBAT_PIN);
    return (sum / 16.0f) * (3.3f / 4095.0f) * 3.0f;
}

/* ═════════════════════════════════════════════════════════════════════════ *
 *  Animation callbacks (shake → random time)
 * ═════════════════════════════════════════════════════════════════════════ */

static void setHourHand(void *obj, int32_t v)
{
    lv_meter_set_indicator_value((lv_obj_t *)obj, indH, v);
}
static void setMinHand(void *obj, int32_t v)
{
    lv_meter_set_indicator_value((lv_obj_t *)obj, indM, v);
}
static void setSecHand(void *obj, int32_t v)
{
    lv_meter_set_indicator_value((lv_obj_t *)obj, indS, v);
}

static void startHandAnim(lv_anim_exec_xcb_t cb, int32_t from, int32_t to, uint32_t ms)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, clockMeter);
    lv_anim_set_exec_cb(&a, cb);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_time(&a, ms);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

/* ═════════════════════════════════════════════════════════════════════════ *
 *  UI construction  (reads current palette)
 * ═════════════════════════════════════════════════════════════════════════ */

static void createClockMeter()
{
    const Palette &p = pal();

    clockMeter = lv_meter_create(lv_scr_act());
    lv_obj_set_size(clockMeter, 224, 224);
    lv_obj_align(clockMeter, LV_ALIGN_TOP_MID, 0, 2);

    lv_obj_set_style_bg_color(clockMeter,     p.clockBg, LV_PART_MAIN);
    lv_obj_set_style_border_width(clockMeter, 2,         LV_PART_MAIN);
    lv_obj_set_style_border_color(clockMeter, p.accent,  LV_PART_MAIN);
    lv_obj_set_style_radius(clockMeter,       30,        LV_PART_MAIN);
    lv_obj_set_style_pad_all(clockMeter,      6,         LV_PART_MAIN);

    lv_meter_scale_t *scale = lv_meter_add_scale(clockMeter);
    lv_meter_set_scale_ticks(clockMeter, scale, 61, 1, 7,  p.tickMinor);
    lv_meter_set_scale_major_ticks(clockMeter, scale, 5, 3, 13, p.accent, 0);
    lv_meter_set_scale_range(clockMeter, scale, 0, 60, 360, 270);

    lv_obj_set_style_text_opa(clockMeter, LV_OPA_TRANSP, LV_PART_TICKS);

    indH = lv_meter_add_needle_line(clockMeter, scale, 4, p.handH, -38);
    indM = lv_meter_add_needle_line(clockMeter, scale, 2, p.handM, -22);
    indS = lv_meter_add_needle_line(clockMeter, scale, 1, p.handS,  -8);
}

static void createBatteryBar()
{
    const Palette &p = pal();

    sep = lv_obj_create(lv_scr_act());
    lv_obj_set_size(sep, LCD_WIDTH, 1);
    lv_obj_set_pos(sep, 0, 229);
    lv_obj_set_style_bg_color(sep,    p.accent, LV_PART_MAIN);
    lv_obj_set_style_border_width(sep, 0,       LV_PART_MAIN);
    lv_obj_set_style_radius(sep,       0,       LV_PART_MAIN);

    cont = lv_obj_create(lv_scr_act());
    lv_obj_set_size(cont, LCD_WIDTH, 49);
    lv_obj_set_pos(cont, 0, 231);
    lv_obj_set_style_bg_color(cont,    p.bg, LV_PART_MAIN);
    lv_obj_set_style_border_width(cont, 0,   LV_PART_MAIN);
    lv_obj_set_style_radius(cont,       0,   LV_PART_MAIN);
    lv_obj_set_style_pad_all(cont,      0,   LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(cont);
    lv_label_set_text(title, "BATTERY");
    lv_obj_set_style_text_color(title, p.textDim, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 10, 3);

    batVolts = lv_label_create(cont);
    lv_label_set_text(batVolts, "-.-- V");
    lv_obj_set_style_text_color(batVolts, p.textBright, 0);
    lv_obj_set_style_text_font(batVolts, &lv_font_montserrat_12, 0);
    lv_obj_align(batVolts, LV_ALIGN_TOP_RIGHT, -10, 3);

    batBar = lv_bar_create(cont);
    lv_obj_set_size(batBar, 204, 22);
    lv_obj_align(batBar, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_bar_set_range(batBar, 0, 100);
    lv_obj_set_style_bg_color(batBar, p.barBg,   LV_PART_MAIN);
    lv_obj_set_style_bg_color(batBar, p.barFill,  LV_PART_INDICATOR);
    lv_obj_set_style_radius(batBar,   3,          LV_PART_MAIN);
    lv_obj_set_style_radius(batBar,   3,          LV_PART_INDICATOR);

    batPct = lv_label_create(batBar);
    lv_label_set_text(batPct, "0%");
    lv_obj_set_style_text_color(batPct, lv_color_white(), 0);
    lv_obj_set_style_text_font(batPct, &lv_font_montserrat_12, 0);
    lv_obj_center(batPct);
}

static void createLevelBubble()
{
    const Palette &p = pal();

    /* Faint center ring — indicates the "level" position */
    levelCenter = lv_obj_create(lv_scr_act());
    lv_obj_set_size(levelCenter, 22, 22);
    lv_obj_clear_flag(levelCenter, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(levelCenter,      LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(levelCenter,      LV_OPA_TRANSP,    0);
    lv_obj_set_style_border_color(levelCenter, p.accent,         0);
    lv_obj_set_style_border_width(levelCenter, 1,                0);
    lv_obj_set_style_opa(levelCenter,          LV_OPA_40,        0);
    lv_obj_set_pos(levelCenter, CLOCK_CX - 11, CLOCK_CY - 11);

    /* Moving bubble */
    levelBubble = lv_obj_create(lv_scr_act());
    lv_obj_set_size(levelBubble, BUBBLE_R * 2, BUBBLE_R * 2);
    lv_obj_clear_flag(levelBubble, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(levelBubble,       LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(levelBubble,     p.handS,          0);
    lv_obj_set_style_bg_opa(levelBubble,       LV_OPA_COVER,     0);
    lv_obj_set_style_border_color(levelBubble, p.accent,         0);
    lv_obj_set_style_border_width(levelBubble, 1,                0);
    lv_obj_set_style_opa(levelBubble,          LV_OPA_70,        0);
    lv_obj_set_pos(levelBubble, CLOCK_CX - BUBBLE_R, CLOCK_CY - BUBBLE_R);
}

static void updateBubble()
{
    if (!levelBubble || !imuOk) return;
    if (!imu.getDataReady()) return;

    float ax, ay, az;
    imu.getAccelerometer(ax, ay, az);

    /* Smooth to reduce jitter at 20 Hz polling rate */
    static float sax = 0.0f, say = 0.0f;
    sax = 0.25f * ax + 0.75f * sax;
    say = 0.25f * ay + 0.75f * say;

    /* Axes swapped relative to physical mounting: ay drives left/right,
     * ax drives up/down.  Scale × 5 so ~0.2 g reaches the full boundary. */
    float dx =  say * (float)BUBBLE_RANGE * 5.0f;
    float dy = -sax * (float)BUBBLE_RANGE * 5.0f;

    float dist = sqrtf(dx * dx + dy * dy);
    if (dist > (float)BUBBLE_RANGE) {
        float s = (float)BUBBLE_RANGE / dist;
        dx *= s;
        dy *= s;
    }

    lv_obj_set_pos(levelBubble,
                   CLOCK_CX - BUBBLE_R + (int16_t)roundf(dx),
                   CLOCK_CY - BUBBLE_R + (int16_t)roundf(dy));
}

/* ═════════════════════════════════════════════════════════════════════════ *
 *  Palette application  (delete + recreate UI, preserve time/voltage)
 * ═════════════════════════════════════════════════════════════════════════ */

/* Forward declaration — applyPalette calls update functions defined below */
static void updateClock();
static void updateVoltage();

static void applyPalette()
{
    if (levelBubble) { lv_obj_del(levelBubble); levelBubble = nullptr; }
    if (levelCenter) { lv_obj_del(levelCenter); levelCenter = nullptr; }
    if (clockMeter) {
        lv_anim_del(clockMeter, nullptr);   /* cancel running hand animations */
        lv_obj_del(clockMeter);
        clockMeter = nullptr;
        indH = indM = indS = nullptr;
    }
    if (sep)  { lv_obj_del(sep);  sep  = nullptr; }
    if (cont) {
        lv_obj_del(cont);
        cont = batBar = batVolts = batPct = nullptr;
    }

    lv_obj_set_style_bg_color(lv_scr_act(), pal().bg, 0);

    createClockMeter();
    createBatteryBar();
    createLevelBubble();   /* created last so it renders on top */
    updateClock();
    updateVoltage();

    Serial.printf("Palette %u %s\n", paletteIdx, isDark ? "night" : "day");
}

/* ═════════════════════════════════════════════════════════════════════════ *
 *  Update functions
 * ═════════════════════════════════════════════════════════════════════════ */

static void updateClock()
{
    lv_meter_set_indicator_value(clockMeter, indH, hourVal(tH, tM));
    lv_meter_set_indicator_value(clockMeter, indM, tM);
    lv_meter_set_indicator_value(clockMeter, indS, tS);
}

static void updateVoltage()
{
    float v   = readVoltage();
    float pct = (v - 3.0f) / (4.2f - 3.0f) * 100.0f;
    if (pct < 0.0f)   pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;

    lv_bar_set_value(batBar, (int32_t)pct, LV_ANIM_OFF);

    char buf[12];
    snprintf(buf, sizeof(buf), "%.2f V", v);
    lv_label_set_text(batVolts, buf);

    snprintf(buf, sizeof(buf), "%d%%", (int)(pct + 0.5f));
    lv_label_set_text(batPct, buf);
    lv_obj_center(batPct);
}

/* ═════════════════════════════════════════════════════════════════════════ *
 *  Gesture handlers
 * ═════════════════════════════════════════════════════════════════════════ */

static void cyclePalette()
{
    paletteIdx = (paletteIdx + 1) % 8;
    applyPalette();
}

static void toggleDayNight()
{
    isDark = !isDark;
    applyPalette();
}

static void handleTap()
{
    unsigned long now = millis();
    if (now < tapDebounceUntil) return;
    tapDebounceUntil = now + 300;

    /* QMI8658_REG_TAP_STATUS 0x59: bits 0-1 = 0 none, 1 single, 2 double.
     * The chip waits for dTapWindow after the first tap before firing STATUS1,
     * so the type is already resolved when we read it here. */
    uint8_t tapType = readQMIReg(0x59) & 0x03;
    Serial.printf("TAP type=%u\n", tapType);

    if (tapType == 2) {
        toggleDayNight();
    } else {
        cyclePalette();
    }
}

static void handleShake()
{
    unsigned long now = millis();
    if (now < shakeDebounceUntil) return;
    shakeDebounceUntil = now + 2000;

    int16_t newH = (int16_t)(esp_random() % 12);
    int16_t newM = (int16_t)(esp_random() % 60);
    int16_t newS = (int16_t)(esp_random() % 60);

    startHandAnim(setHourHand, hourVal(tH, tM), hourVal(newH, newM), 900);
    startHandAnim(setMinHand,  tM,               newM,               750);
    startHandAnim(setSecHand,  tS,               newS,               600);

    tH = newH;  tM = newM;  tS = newS;
    shakeRtcResumeAt = now + 10000;
}

/* ═════════════════════════════════════════════════════════════════════════ *
 *  Entry points
 * ═════════════════════════════════════════════════════════════════════════ */

void setup()
{
    Serial.begin(115200);

    gfx->begin();
    gfx->fillScreen(0x0000);
    pinMode(LCD_BL, OUTPUT);
    digitalWrite(LCD_BL, HIGH);

    analogReadResolution(12);
    pinMode(VBAT_PIN, INPUT);

    initPalettes();

    /* ── LVGL ─────────────────────────────────────────────────────────────── */
    lv_init();

    static lv_disp_draw_buf_t drawBuf;
    lv_disp_draw_buf_init(&drawBuf, lvBuf, nullptr, LCD_WIDTH * 28);

    static lv_disp_drv_t dispDrv;
    lv_disp_drv_init(&dispDrv);
    dispDrv.hor_res  = LCD_WIDTH;
    dispDrv.ver_res  = LCD_HEIGHT;
    dispDrv.flush_cb = flushCb;
    dispDrv.draw_buf = &drawBuf;
    lv_disp_drv_register(&dispDrv);

    const esp_timer_create_args_t timerArgs = { .callback = tickCb, .name = "lv_tick" };
    esp_timer_handle_t tickTimer;
    esp_timer_create(&timerArgs, &tickTimer);
    esp_timer_start_periodic(tickTimer, 2000);

    /* ── RTC ──────────────────────────────────────────────────────────────── */
    rtcOk = rtc.begin(Wire, PCF85063_SLAVE_ADDRESS, IIC_SDA, IIC_SCL);
    if (rtcOk) {
        if (!rtc.isRunning()) {
            uint8_t day = (__DATE__[4] == ' ') ? __DATE__[5] - '0'
                                               : parse2(__DATE__ + 4);
            rtc.setDateTime(
                (uint16_t)(parse2(__DATE__ + 9) + 2000),
                parseMonth(__DATE__), day,
                parse2(__TIME__), parse2(__TIME__ + 3), parse2(__TIME__ + 6));
        }
        RTC_DateTime dt = rtc.getDateTime();
        tH = dt.hour;  tM = dt.minute;  tS = dt.second;
        Serial.printf("RTC OK  %02d:%02d:%02d\n", tH, tM, tS);
    } else {
        Serial.println("RTC not found — running on millis()");
    }

    /* ── IMU ──────────────────────────────────────────────────────────────── */
    imuOk = imu.begin(Wire, QMI8658_L_SLAVE_ADDRESS, IIC_SDA, IIC_SCL);
    if (imuOk) {
        imu.configAccelerometer(SensorQMI8658::ACC_RANGE_4G,
                                SensorQMI8658::ACC_ODR_500Hz,
                                SensorQMI8658::LPF_MODE_0, false);
        /* peakMagThr lowered to 0x00C8 (~0.2 g²) — original 0x0320 was too high.
         * dTapWindow = 100 samples @ 500 Hz = 200 ms double-tap window. */
        imu.configTap(SensorQMI8658::PRIORITY0,
                      15,     /* peakWindow  */
                      25,     /* tapWindow   */
                      100,    /* dTapWindow  */
                      16,     /* alpha       */
                      64,     /* gamma       */
                      0x00C8, /* peakMagThr  */
                      0x0050  /* UDMThr      */);
        imu.enableTap();
        imu.enableAccelerometer();
        Serial.printf("IMU OK  id=0x%02X\n", imu.getChipID());
    } else {
        Serial.println("IMU not found");
    }

    /* ── Screen ───────────────────────────────────────────────────────────── */
    lv_theme_t *th = lv_theme_basic_init(lv_disp_get_default());
    lv_disp_set_theme(lv_disp_get_default(), th);
    lv_obj_set_style_radius(lv_scr_act(), 16, 0);
    lv_obj_set_style_clip_corner(lv_scr_act(), true, 0);

    applyPalette();   /* builds UI with palette 0 night */

    nextSecond  = ((millis() / 1000) + 1) * 1000;
    nextVoltage = millis() + 10000UL;
}

void loop()
{
    lv_timer_handler();

    unsigned long now = millis();

    /* ── IMU polling (every 50 ms) ────────────────────────────────────────── */
    static unsigned long nextImuPoll = 0;
    if (imuOk && now >= nextImuPoll) {
        nextImuPoll = now + 50;
        uint8_t status = (uint8_t)imu.getStatusRegister();
        if (status) Serial.printf("STATUS1=0x%02X\n", status);
        if (status & SensorQMI8658::EVENT_TAP_MOTION) handleTap();
        updateBubble();
    }

    /* ── Clock tick ───────────────────────────────────────────────────────── */
    if (now >= nextSecond) {
        nextSecond += 1000;

        if (rtcOk && now >= shakeRtcResumeAt) {
            RTC_DateTime dt = rtc.getDateTime();
            tH = dt.hour;  tM = dt.minute;  tS = dt.second;
        } else {
            if (++tS >= 60) { tS = 0; if (++tM >= 60) { tM = 0; if (++tH >= 24) tH = 0; } }
        }

        updateClock();
    }

    /* ── Voltage gauge (every 10 s) ───────────────────────────────────────── */
    if (now >= nextVoltage) {
        nextVoltage = now + 10000UL;
        updateVoltage();
    }
}
