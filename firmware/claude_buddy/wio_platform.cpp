#include "wio_platform.h"
#include <Arduino.h>
#include <LIS3DHTR.h>
#include <Wire.h>
#include <math.h>

TFT_eSPI    tft = TFT_eSPI();
TFT_eSprite spr = TFT_eSprite(&tft);

// Wio Terminal's onboard LIS3DHTR is on Wire1 at I2C address 0x19.
static LIS3DHTR<TwoWire> _acc;

// ── soft RTC ──
static RTC_TimeTypeDef _t = {0, 0, 0};
static RTC_DateTypeDef _d = {0, 1, 1, 2000};
static uint32_t _rtcLastMs = 0;
static uint32_t _rtcCarryMs = 0;

void rtcSetTime(const RTC_TimeTypeDef* t) { _t = *t; _rtcLastMs = millis(); _rtcCarryMs = 0; }
void rtcSetDate(const RTC_DateTypeDef* d) { _d = *d; }
void rtcGetTime(RTC_TimeTypeDef* t) { *t = _t; }
void rtcGetDate(RTC_DateTypeDef* d) { *d = _d; }
void rtcTick() {
  uint32_t now = millis();
  uint32_t dt = now - _rtcLastMs; _rtcLastMs = now;
  _rtcCarryMs += dt;
  while (_rtcCarryMs >= 1000) {
    _rtcCarryMs -= 1000;
    if (++_t.Seconds >= 60) { _t.Seconds = 0;
      if (++_t.Minutes >= 60) { _t.Minutes = 0;
        if (++_t.Hours >= 24) { _t.Hours = 0; _d.WeekDay = (_d.WeekDay + 1) % 7; }
      }
    }
  }
}

// ── input ──
// Edge detection: remember last raw state, report a press on HIGH→LOW edge
// (buttons are active-low with INPUT_PULLUP).
struct Edge { uint8_t pin; bool last; };
static Edge _eA{WIO_KEY_A, true}, _eB{WIO_KEY_B, true}, _eC{WIO_KEY_C, true};
static Edge _eU{WIO_5S_UP, true}, _eD{WIO_5S_DOWN, true}, _eP{WIO_5S_PRESS, true};
static bool _pA = false, _pB = false, _pC = false, _pU = false, _pD = false, _pP = false;
static uint32_t _aDownMs = 0; static bool _aHeldFired = false; static bool _aDown = false;

static bool edge(Edge& e) { bool cur = digitalRead(e.pin); bool press = (e.last && !cur); e.last = cur; return press; }

void platformBegin() {
  tft.init();
  tft.setRotation(3);                 // landscape, top buttons up (matches claude-usage)
  pinMode(LCD_BACKLIGHT, OUTPUT);
  digitalWrite(LCD_BACKLIGHT, HIGH);  // ensure backlight on
  // 8-bit (RGB332) sprite = 320*240 = 76.8 KB. A 16-bit sprite would need
  // 150 KB, which does not fit alongside the rpcBLE FreeRTOS heap (48 KB) +
  // AnimatedGIF on the SAMD51's 192 KB — createSprite() would fail and the
  // screen would stay white. 8-bit halves it and fits.
  spr.setColorDepth(8);
  void* fb = spr.createSprite(SCREEN_W, SCREEN_H);
  Serial.print("[plat] sprite alloc "); Serial.println(fb ? "ok" : "FAILED");
  if (!fb) {                          // last-resort: tell the user on-screen
    tft.fillScreen(TFT_RED);
    tft.setTextColor(TFT_WHITE, TFT_RED);
    tft.drawString("sprite alloc failed", 10, 10);
  }
  spr.setTextDatum(TL_DATUM);
  uint8_t btnPins[] = {WIO_KEY_A, WIO_KEY_B, WIO_KEY_C,
                       WIO_5S_UP, WIO_5S_DOWN, WIO_5S_LEFT, WIO_5S_RIGHT, WIO_5S_PRESS};
  for (uint8_t i = 0; i < sizeof(btnPins); i++) pinMode(btnPins[i], INPUT_PULLUP);
  pinMode(LED_BUILTIN, OUTPUT); digitalWrite(LED_BUILTIN, LOW);
  pinMode(WIO_BUZZER, OUTPUT);
  _acc.begin(Wire1, 0x19);            // onboard accel: Wire1, addr 0x19
  _acc.setOutputDataRate(LIS3DHTR_DATARATE_50HZ);
  _rtcLastMs = millis();
}

void platformUpdate() {
  _pA = edge(_eA); _pB = edge(_eB); _pC = edge(_eC);
  _pU = edge(_eU); _pD = edge(_eD); _pP = edge(_eP);
  bool aRaw = !digitalRead(WIO_KEY_A);     // active-low → true when down
  if (aRaw && !_aDown) { _aDown = true; _aDownMs = millis(); _aHeldFired = false; }
  if (!aRaw) { _aDown = false; _aHeldFired = false; }
}
bool btnAPressed() { return _pA; }
bool btnBPressed() { return _pB; }
bool btnCPressed() { return _pC; }
bool btnAHeld(uint16_t ms) {
  if (_aDown && !_aHeldFired && millis() - _aDownMs >= ms) { _aHeldFired = true; return true; }
  return false;
}
bool joyUp() { return _pU; }
bool joyDown() { return _pD; }
bool joyPress() { return _pP; }

// ── accelerometer ──
bool platformShaken() {
  static float baseline = 1.0f;
  float x = _acc.getAccelerationX(), y = _acc.getAccelerationY(), z = _acc.getAccelerationZ();
  float mag = sqrtf(x * x + y * y + z * z);
  float delta = fabsf(mag - baseline);
  baseline = baseline * 0.95f + mag * 0.05f;
  return delta > 0.8f;
}
bool platformFaceDown() {
  float x = _acc.getAccelerationX(), y = _acc.getAccelerationY(), z = _acc.getAccelerationZ();
  return z < -0.7f && fabsf(x) < 0.4f && fabsf(y) < 0.4f;
}

// ── outputs ──
void beep(uint16_t freq, uint16_t durMs) {
  if (freq == 0) { analogWrite(WIO_BUZZER, 0); return; }
  tone(WIO_BUZZER, freq, durMs);
}
void backlight(bool on) { digitalWrite(LCD_BACKLIGHT, on ? HIGH : LOW); }
void attentionLed(bool on) { digitalWrite(LED_BUILTIN, on ? HIGH : LOW); }

// ── misc ──
extern "C" char* sbrk(int);
uint32_t freeHeapApprox() { char top; return (uint32_t)(&top - (char*)sbrk(0)); }
