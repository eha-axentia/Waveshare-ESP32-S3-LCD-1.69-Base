/*
 * ClockAndVoltage.ino — Waveshare ESP32-S3-LCD-1.69
 *
 * Top:    Analog clock face driven by PCF85063 RTC.
 * Bottom: Li-ion battery voltage gauge read from GPIO-1 ADC
 *         via a 200 kΩ / 100 kΩ voltage divider (ratio × 3).
 *
 * Libraries required (install via Arduino IDE Library Manager or
 * copy from the official Waveshare demo-code package):
 *   • Arduino_GFX_Library  (moononournation)
 *   • SensorLib            (lewisxhe)
 */

#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include "Arduino_GFX_Library.h"
#include "SensorPCF85063.hpp"
#include "pin_config.h"

/* ── Display ─────────────────────────────────────────────────────────────── */
Arduino_DataBus *bus =
    new Arduino_ESP32SPI(LCD_DC, LCD_CS, LCD_SCK, LCD_MOSI);
Arduino_GFX *gfx =
    new Arduino_ST7789(bus, LCD_RST, 0 /*rotation*/, true /*IPS*/,
                       LCD_WIDTH, LCD_HEIGHT, 0, 20 /*y-offset*/, 0, 0);

/* ── RTC ─────────────────────────────────────────────────────────────────── */
SensorPCF85063 rtc;
bool rtcOk = false;

/* ── Clock geometry ──────────────────────────────────────────────────────── */
// Clock face occupies the top portion of the 240 × 280 display.
static const int16_t CX = 120;   // center x
static const int16_t CY = 112;   // center y (shifted up to leave room for gauge)
static const int16_t R  = 108;   // outer radius of clock ring

// Hand lengths — all shorter than ERASE_R so the erase fill does not touch
// the tick marks, which live between TICK_I and R.
static const int16_t S_LEN   = R * 87 / 100;   // second  — 93 px
static const int16_t M_LEN   = R * 73 / 100;   // minute  — 79 px
static const int16_t H_LEN   = R * 52 / 100;   // hour    — 56 px
static const int16_t ERASE_R = S_LEN;           // interior erase radius

// Tick marks start outside ERASE_R
static const int16_t TICK_MAJOR_I = R * 90 / 100;   // 97 px  (5-min)
static const int16_t TICK_MINOR_I = R * 95 / 100;   // 103 px (1-min)

/* ── Colors ──────────────────────────────────────────────────────────────── */
static const uint16_t C_BG     = BLACK;
static const uint16_t C_RING   = 0x2965;   // dark navy clock ring
static const uint16_t C_MARK   = WHITE;
static const uint16_t C_SUB    = 0x4A49;   // dim grey minor ticks
static const uint16_t C_HOUR   = WHITE;
static const uint16_t C_MIN    = 0x07FF;   // cyan
static const uint16_t C_SEC    = RED;
static const uint16_t C_DOT    = WHITE;
static const uint16_t C_SEP    = 0x2965;   // separator line

/* ── Voltage gauge geometry (bottom strip: y = 228 … 278) ───────────────── */
static const int16_t GX   = 18;    // bar left
static const int16_t GY   = 244;   // bar top
static const int16_t GW   = 204;   // bar width
static const int16_t GH   = 22;    // bar height
static const float   VMIN = 3.0f;  // ADC reading at 0 % (empty Li-ion)
static const float   VMAX = 4.2f;  // ADC reading at 100 % (full Li-ion)

/* ── ADC ─────────────────────────────────────────────────────────────────── */
static const int   VBAT_PIN = 1;
static const float V_DIV    = 3.0f;   // (200 kΩ + 100 kΩ) / 100 kΩ

/* ── Trig constants ──────────────────────────────────────────────────────── */
static const float SIXTIETH_RAD = 0.10471976f;   // 2π / 60
static const float TWELFTH_RAD  = 0.52359878f;   // 2π / 12
static const float HALF_PI_F    = 1.5707963f;

/* ── Hand state ──────────────────────────────────────────────────────────── */
static int16_t tH = 12, tM = 0, tS = 0;
static int16_t osx, osy, omx, omy, ohx, ohy;   // previous hand tips

static unsigned long nextSecond  = 0;
static unsigned long nextVoltage = 0;

/* ═════════════════════════════════════════════════════════════════════════ *
 *  Helpers
 * ═════════════════════════════════════════════════════════════════════════ */

// Angle in radians clockwise from 12 o'clock → pixel tip (x, y).
static void handTip(float rad, int16_t len, int16_t &x, int16_t &y)
{
    x = CX + (int16_t)(cosf(rad - HALF_PI_F) * len);
    y = CY + (int16_t)(sinf(rad - HALF_PI_F) * len);
}

static float secAngle(int s)         { return SIXTIETH_RAD * s; }
static float minAngle(int m, int s)  { return SIXTIETH_RAD * m + SIXTIETH_RAD * s / 60.0f; }
static float hrAngle (int h, int m)  { return TWELFTH_RAD  * (h % 12) + TWELFTH_RAD * m / 60.0f; }

/* ── Parse compile-time __TIME__ / __DATE__ fields ─────────────────────── */
static uint8_t parse2(const char *p) { return (p[0] - '0') * 10 + (p[1] - '0'); }

static uint8_t parseMonth(const char *d)
{
    const char *names[12] = {"Jan","Feb","Mar","Apr","May","Jun",
                              "Jul","Aug","Sep","Oct","Nov","Dec"};
    for (uint8_t i = 0; i < 12; i++)
        if (strncmp(d, names[i], 3) == 0) return i + 1;
    return 1;
}

/* ═════════════════════════════════════════════════════════════════════════ *
 *  Clock face drawing
 * ═════════════════════════════════════════════════════════════════════════ */

static void drawTicks()
{
    for (uint8_t i = 0; i < 60; i++) {
        float a  = SIXTIETH_RAD * i - HALF_PI_F;
        float ca = cosf(a), sa = sinf(a);
        bool  major = (i % 5 == 0);
        int16_t  ri = major ? TICK_MAJOR_I : TICK_MINOR_I;
        uint16_t c  = major ? C_MARK       : C_SUB;
        gfx->drawLine(
            CX + (int16_t)(ca * R),  CY + (int16_t)(sa * R),
            CX + (int16_t)(ca * ri), CY + (int16_t)(sa * ri), c);
    }
}

// Called once from setup() — paints the static parts of the clock.
static void drawClockFace()
{
    gfx->fillCircle(CX, CY, R,     C_RING);   // outer ring
    gfx->fillCircle(CX, CY, R - 3, C_BG);     // inner black field
    drawTicks();
}

/* ═════════════════════════════════════════════════════════════════════════ *
 *  Hand drawing / updating
 * ═════════════════════════════════════════════════════════════════════════ */

// Draw all three hands and the center dot; record their tips.
static void drawHands(int16_t h, int16_t m, int16_t s)
{
    handTip(secAngle(s),    S_LEN, osx, osy);
    handTip(minAngle(m, s), M_LEN, omx, omy);
    handTip(hrAngle(h, m),  H_LEN, ohx, ohy);

    gfx->drawLine(CX, CY, ohx, ohy, C_HOUR);
    gfx->drawLine(CX, CY, omx, omy, C_MIN);
    gfx->drawLine(CX, CY, osx, osy, C_SEC);
    gfx->fillCircle(CX, CY, 4, C_DOT);
}

// Called every second: erase old hands, restore ticks, draw new hands.
static void updateHands(int16_t h, int16_t m, int16_t s)
{
    int16_t sx, sy, mx, my, hx, hy;
    handTip(secAngle(s),    S_LEN, sx, sy);
    handTip(minAngle(m, s), M_LEN, mx, my);
    handTip(hrAngle(h, m),  H_LEN, hx, hy);

    if (sx == osx && sy == osy) return;   // nothing changed yet

    gfx->startWrite();

    // Erase changed hands with background lines
    gfx->writeLine(CX, CY, osx, osy, C_BG);
    if (mx != omx || my != omy) gfx->writeLine(CX, CY, omx, omy, C_BG);
    if (hx != ohx || hy != ohy) gfx->writeLine(CX, CY, ohx, ohy, C_BG);

    // Restore tick marks that the erased lines may have crossed
    for (uint8_t i = 0; i < 60; i++) {
        float a  = SIXTIETH_RAD * i - HALF_PI_F;
        float ca = cosf(a), sa = sinf(a);
        bool  major = (i % 5 == 0);
        int16_t  ri = major ? TICK_MAJOR_I : TICK_MINOR_I;
        uint16_t c  = major ? C_MARK       : C_SUB;
        gfx->writeLine(
            CX + (int16_t)(ca * R),  CY + (int16_t)(sa * R),
            CX + (int16_t)(ca * ri), CY + (int16_t)(sa * ri), c);
    }

    // Draw new hands — hour and minute beneath second
    gfx->writeLine(CX, CY, hx, hy, C_HOUR);
    gfx->writeLine(CX, CY, mx, my, C_MIN);
    gfx->writeLine(CX, CY, sx, sy, C_SEC);
    gfx->writePixel(CX, CY, C_DOT);
    for (int16_t dy = -4; dy <= 4; dy++)
        for (int16_t dx = -4; dx <= 4; dx++)
            if (dx*dx + dy*dy <= 16)
                gfx->writePixel(CX + dx, CY + dy, C_DOT);

    gfx->endWrite();

    ohx = hx; ohy = hy;
    omx = mx; omy = my;
    osx = sx; osy = sy;
}

/* ═════════════════════════════════════════════════════════════════════════ *
 *  Voltage gauge
 * ═════════════════════════════════════════════════════════════════════════ */

static float readVoltage()
{
    int32_t sum = 0;
    for (int i = 0; i < 16; i++) sum += analogRead(VBAT_PIN);
    float adcV = (sum / 16.0f) * (3.3f / 4095.0f);
    return adcV * V_DIV;
}

static void drawVoltageGauge(float v)
{
    float pct = constrain((v - VMIN) / (VMAX - VMIN), 0.0f, 1.0f);
    int16_t filled = (int16_t)(pct * GW);

    uint16_t barColor;
    if      (pct > 0.60f) barColor = 0x07E0;   // green
    else if (pct > 0.25f) barColor = 0xFFE0;   // yellow
    else                  barColor = RED;

    // Clear the entire bottom strip
    gfx->fillRect(0, 228, LCD_WIDTH, LCD_HEIGHT - 228, C_BG);

    // Separator
    gfx->drawFastHLine(0, 229, LCD_WIDTH, C_SEP);

    // "BATTERY" label and voltage reading
    gfx->setTextSize(1);
    gfx->setTextColor(0x8410);   // mid-grey label
    gfx->setCursor(GX, 234);
    gfx->print("BATTERY");

    char buf[12];
    snprintf(buf, sizeof(buf), "%.2f V", v);
    gfx->setTextColor(WHITE);
    gfx->setCursor(GX + GW - (int16_t)strlen(buf) * 6, 234);
    gfx->print(buf);

    // Bar — dark background, coloured fill
    gfx->fillRect(GX,          GY, GW,     GH, 0x18C3);   // dark bar bg
    if (filled > 0)
        gfx->fillRect(GX,      GY, filled, GH, barColor);

    // Thin border
    gfx->drawRect(GX, GY, GW, GH, 0x528A);

    // Percentage centred inside bar
    char pctBuf[6];
    snprintf(pctBuf, sizeof(pctBuf), "%d%%", (int)(pct * 100.0f + 0.5f));
    gfx->setTextColor(BLACK);
    // Draw shadow for legibility over both dark and light bar fills
    int16_t tx = GX + (GW - (int16_t)strlen(pctBuf) * 6) / 2;
    int16_t ty = GY + (GH - 8) / 2;
    gfx->setCursor(tx + 1, ty + 1);
    gfx->setTextColor(0x0000);   // shadow
    gfx->print(pctBuf);
    gfx->setCursor(tx, ty);
    gfx->setTextColor(WHITE);
    gfx->print(pctBuf);
}

/* ═════════════════════════════════════════════════════════════════════════ *
 *  Arduino entry points
 * ═════════════════════════════════════════════════════════════════════════ */

void setup()
{
    Serial.begin(115200);

    // Display
    gfx->begin();
    gfx->fillScreen(C_BG);
    pinMode(LCD_BL, OUTPUT);
    digitalWrite(LCD_BL, HIGH);

    // ADC
    analogReadResolution(12);
    pinMode(VBAT_PIN, INPUT);

    // RTC — SensorLib calls Wire.begin() internally
    rtcOk = rtc.begin(Wire, PCF85063_SLAVE_ADDRESS, IIC_SDA, IIC_SCL);
    if (rtcOk) {
        if (!rtc.isRunning()) {
            // First power-on or backup battery dead — seed from compile time.
            // __DATE__ format: "Mon DD YYYY"   __TIME__ format: "HH:MM:SS"
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

    // Static clock face
    drawClockFace();

    // Initial hands
    drawHands(tH, tM, tS);

    // Initial voltage gauge
    drawVoltageGauge(readVoltage());

    nextSecond  = ((millis() / 1000) + 1) * 1000;
    nextVoltage = millis() + 10000UL;
}

void loop()
{
    unsigned long now = millis();

    // ── Tick every second ────────────────────────────────────────────────
    if (now >= nextSecond) {
        nextSecond += 1000;

        if (rtcOk) {
            RTC_DateTime dt = rtc.getDateTime();
            tH = dt.hour; tM = dt.minute; tS = dt.second;
        } else {
            // Software fallback
            if (++tS >= 60) {
                tS = 0;
                if (++tM >= 60) {
                    tM = 0;
                    if (++tH >= 24) tH = 0;
                }
            }
        }

        updateHands(tH, tM, tS);
    }

    // ── Voltage update every 10 s ─────────────────────────────────────────
    if (now >= nextVoltage) {
        nextVoltage = now + 10000UL;
        drawVoltageGauge(readVoltage());
    }
}
