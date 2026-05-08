/*
 * main.cpp — Waveshare ESP32-S3-LCD-1.69
 *
 * Top:    Analog clock face (lv_meter) driven by PCF85063 RTC.
 * Bottom: Li-ion battery voltage gauge (lv_bar).
 */

#include <Arduino.h>
#include <Wire.h>
#include "Arduino_GFX_Library.h"
#include "SensorPCF85063.hpp"
#include "pin_config.h"
#include "lvgl.h"
#include "esp_timer.h"

/* ── Display ─────────────────────────────────────────────────────────────── */
static Arduino_DataBus *bus =
    new Arduino_ESP32SPI(LCD_DC, LCD_CS, LCD_SCK, LCD_MOSI);
static Arduino_GFX *gfx =
    new Arduino_ST7789(bus, LCD_RST, 0 /*rotation*/, true /*IPS*/,
                       LCD_WIDTH, LCD_HEIGHT, 0, 20 /*y-offset*/, 0, 0);

/* ── LVGL draw buffer (1/10 of screen height) ────────────────────────────── */
static lv_color_t lvBuf[LCD_WIDTH * 28];

/* ── RTC ─────────────────────────────────────────────────────────────────── */
static SensorPCF85063 rtc;
static bool rtcOk = false;

/* ── UI handles ──────────────────────────────────────────────────────────── */
static lv_obj_t           *clockMeter = nullptr;
static lv_meter_indicator_t *indH     = nullptr;
static lv_meter_indicator_t *indM     = nullptr;
static lv_meter_indicator_t *indS     = nullptr;
static lv_obj_t *batBar   = nullptr;
static lv_obj_t *batVolts = nullptr;
static lv_obj_t *batPct   = nullptr;

/* ── Time state ──────────────────────────────────────────────────────────── */
static int16_t tH = 12, tM = 0, tS = 0;

static unsigned long nextSecond  = 0;
static unsigned long nextVoltage = 0;

/* ═════════════════════════════════════════════════════════════════════════ *
 *  LVGL integration
 * ═════════════════════════════════════════════════════════════════════════ */

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

static uint8_t parse2(const char *p) { return (p[0] - '0') * 10 + (p[1] - '0'); }

static uint8_t parseMonth(const char *d)
{
    const char *names[12] = {"Jan","Feb","Mar","Apr","May","Jun",
                              "Jul","Aug","Sep","Oct","Nov","Dec"};
    for (uint8_t i = 0; i < 12; i++)
        if (strncmp(d, names[i], 3) == 0) return i + 1;
    return 1;
}

/* Map clock time to 0-60 meter scale */
static int16_t hourVal(int h, int m) { return (h % 12) * 5 + m / 12; }

static float readVoltage()
{
    int32_t sum = 0;
    for (int i = 0; i < 16; i++) sum += analogRead(VBAT_PIN);
    return (sum / 16.0f) * (3.3f / 4095.0f) * 3.0f;
}

/* ═════════════════════════════════════════════════════════════════════════ *
 *  UI construction
 * ═════════════════════════════════════════════════════════════════════════ */

static void createClockMeter()
{
    clockMeter = lv_meter_create(lv_scr_act());
    lv_obj_set_size(clockMeter, 224, 224);
    lv_obj_align(clockMeter, LV_ALIGN_TOP_MID, 0, 2);

    /* Black background, no border */
    lv_obj_set_style_bg_color(clockMeter, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(clockMeter, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(clockMeter, lv_color_make(0x29, 0x65, 0x00), LV_PART_MAIN);
    lv_obj_set_style_radius(clockMeter, 30, LV_PART_MAIN);
    lv_obj_set_style_pad_all(clockMeter, 6, LV_PART_MAIN);

    lv_meter_scale_t *scale = lv_meter_add_scale(clockMeter);

    /* Minor ticks: 61 marks (0..60), one per second position */
    lv_meter_set_scale_ticks(clockMeter, scale,
                             61, 1, 7, lv_color_make(0x40, 0x40, 0x40));

    /* Major ticks: every 5th (12 hour marks) */
    lv_meter_set_scale_major_ticks(clockMeter, scale,
                                   5, 3, 13, lv_color_white(), 0);

    /* Full 360° scale, value 0 starts at 12 o'clock (270° from 3 o'clock) */
    lv_meter_set_scale_range(clockMeter, scale, 0, 60, 360, 270);

    /* Hide numeric labels on major ticks */
    lv_obj_set_style_text_opa(clockMeter, LV_OPA_TRANSP, LV_PART_TICKS);

    /* Hour hand — thick, white */
    indH = lv_meter_add_needle_line(clockMeter, scale, 4, lv_color_white(), -38);
    /* Minute hand — medium, cyan */
    indM = lv_meter_add_needle_line(clockMeter, scale, 2,
                                    lv_color_make(0x00, 0xFF, 0xFF), -22);
    /* Second hand — thin, red */
    indS = lv_meter_add_needle_line(clockMeter, scale, 1,
                                    lv_color_make(0xFF, 0x00, 0x00), -8);
}

static void createBatteryBar()
{
    /* Thin separator */
    lv_obj_t *sep = lv_obj_create(lv_scr_act());
    lv_obj_set_size(sep, LCD_WIDTH, 1);
    lv_obj_set_pos(sep, 0, 229);
    lv_obj_set_style_bg_color(sep, lv_color_make(0x29, 0x65, 0x00), LV_PART_MAIN);
    lv_obj_set_style_border_width(sep, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(sep, 0, LV_PART_MAIN);

    /* Container for battery widgets */
    lv_obj_t *cont = lv_obj_create(lv_scr_act());
    lv_obj_set_size(cont, LCD_WIDTH, 49);
    lv_obj_set_pos(cont, 0, 231);
    lv_obj_set_style_bg_color(cont, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(cont, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(cont, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(cont, 0, LV_PART_MAIN);

    /* "BATTERY" label */
    lv_obj_t *title = lv_label_create(cont);
    lv_label_set_text(title, "BATTERY");
    lv_obj_set_style_text_color(title, lv_color_make(0x80, 0x80, 0x80), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 10, 3);

    /* Voltage value (right-aligned) */
    batVolts = lv_label_create(cont);
    lv_label_set_text(batVolts, "-.-- V");
    lv_obj_set_style_text_color(batVolts, lv_color_white(), 0);
    lv_obj_set_style_text_font(batVolts, &lv_font_montserrat_12, 0);
    lv_obj_align(batVolts, LV_ALIGN_TOP_RIGHT, -10, 3);

    /* Progress bar */
    batBar = lv_bar_create(cont);
    lv_obj_set_size(batBar, 204, 22);
    lv_obj_align(batBar, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_bar_set_range(batBar, 0, 100);
    lv_obj_set_style_bg_color(batBar, lv_color_make(0x18, 0x18, 0x18), LV_PART_MAIN);
    lv_obj_set_style_bg_color(batBar, lv_palette_main(LV_PALETTE_GREEN), LV_PART_INDICATOR);
    lv_obj_set_style_radius(batBar, 3, LV_PART_MAIN);
    lv_obj_set_style_radius(batBar, 3, LV_PART_INDICATOR);

    /* Percentage centered on bar */
    batPct = lv_label_create(batBar);
    lv_label_set_text(batPct, "0%");
    lv_obj_set_style_text_color(batPct, lv_color_white(), 0);
    lv_obj_set_style_text_font(batPct, &lv_font_montserrat_12, 0);
    lv_obj_center(batPct);
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
    if (pct < 0.0f) pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;

    lv_color_t barColor = (pct > 60.0f) ? lv_palette_main(LV_PALETTE_GREEN)
                        : (pct > 25.0f) ? lv_palette_main(LV_PALETTE_YELLOW)
                                        : lv_palette_main(LV_PALETTE_RED);
    lv_obj_set_style_bg_color(batBar, barColor, LV_PART_INDICATOR);
    lv_bar_set_value(batBar, (int32_t)pct, LV_ANIM_OFF);

    char buf[12];
    snprintf(buf, sizeof(buf), "%.2f V", v);
    lv_label_set_text(batVolts, buf);

    snprintf(buf, sizeof(buf), "%d%%", (int)(pct + 0.5f));
    lv_label_set_text(batPct, buf);
    lv_obj_center(batPct);
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

    /* ── LVGL ───────────────────────────────────────────────────────────── */
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

    /* 2 ms tick via esp_timer */
    esp_timer_handle_t tickTimer;
    const esp_timer_create_args_t timerArgs = { .callback = tickCb, .name = "lv_tick" };
    esp_timer_create(&timerArgs, &tickTimer);
    esp_timer_start_periodic(tickTimer, 2000);

    /* ── RTC ─────────────────────────────────────────────────────────────── */
    rtcOk = rtc.begin(Wire, PCF85063_SLAVE_ADDRESS, IIC_SDA, IIC_SCL);
    if (rtcOk) {
        if (!rtc.isRunning()) {
            uint8_t day = (__DATE__[4] == ' ') ? __DATE__[5] - '0'
                                               : parse2(__DATE__ + 4);
            rtc.setDateTime(
                (uint16_t)(parse2(__DATE__ + 9) + 2000),
                parseMonth(__DATE__),
                day,
                parse2(__TIME__),
                parse2(__TIME__ + 3),
                parse2(__TIME__ + 6));
        }
        RTC_DateTime dt = rtc.getDateTime();
        tH = dt.hour; tM = dt.minute; tS = dt.second;
        Serial.printf("RTC OK  %02d:%02d:%02d\n", tH, tM, tS);
    } else {
        Serial.println("RTC not found — running on millis()");
    }

    /* ── Screen ─────────────────────────────────────────────────────────── */
    lv_theme_t *th = lv_theme_basic_init(lv_disp_get_default());
    lv_disp_set_theme(lv_disp_get_default(), th);
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_black(), 0);
    lv_obj_set_style_radius(lv_scr_act(), 16, 0);
    lv_obj_set_style_clip_corner(lv_scr_act(), true, 0);

    createClockMeter();
    createBatteryBar();
    updateClock();
    updateVoltage();

    nextSecond  = ((millis() / 1000) + 1) * 1000;
    nextVoltage = millis() + 10000UL;
}

void loop()
{
    lv_timer_handler();

    unsigned long now = millis();

    if (now >= nextSecond) {
        nextSecond += 1000;

        if (rtcOk) {
            RTC_DateTime dt = rtc.getDateTime();
            tH = dt.hour; tM = dt.minute; tS = dt.second;
        } else {
            if (++tS >= 60) { tS = 0; if (++tM >= 60) { tM = 0; if (++tH >= 24) tH = 0; } }
        }

        updateClock();
    }

    if (now >= nextVoltage) {
        nextVoltage = now + 10000UL;
        updateVoltage();
    }
}
