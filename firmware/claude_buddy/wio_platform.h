#pragma once
// GIF/filesystem character packs are an opt-in hardware build (-DBUDDY_GIF).
// They pull in Seeed_FS/SD, whose global `SD` object's constructor runs at boot
// and destabilizes cold-boot on this board, so the DEFAULT hardware build omits
// them: ASCII buddy + live BLE only. Define BUDDY_FS when GIF is wanted.
#if defined(BUDDY_GIF) && !defined(MOCK_DATA)
  #define BUDDY_FS 1
#endif
#include <TFT_eSPI.h>
#include <stdint.h>

// ── shared sprite (the ONLY full-screen sprite; 320x240x16 ≈ 150 KB) ──
extern TFT_eSPI    tft;
extern TFT_eSprite spr;

// Screen geometry (landscape)
static const int SCREEN_W = 320;
static const int SCREEN_H = 240;

// ── soft RTC ── (M5-compatible types so data.h compiles unchanged)
struct RTC_TimeTypeDef { uint8_t Hours, Minutes, Seconds; };
struct RTC_DateTypeDef { uint8_t WeekDay, Month, Date; uint16_t Year; };
void rtcSetTime(const RTC_TimeTypeDef* t);
void rtcSetDate(const RTC_DateTypeDef* d);
void rtcGetTime(RTC_TimeTypeDef* t);
void rtcGetDate(RTC_DateTypeDef* d);
void rtcTick();                 // advance the soft clock from millis()

// ── platform lifecycle ──
void platformBegin();           // tft.init, sprite, pins, accel, buzzer

// ── input ── (call platformUpdate() once per loop; getters return edge events)
void platformUpdate();
bool btnAPressed();             // WIO_KEY_A (rightmost top button) — "approve"/select
bool btnBPressed();             // WIO_KEY_B (middle top button)    — "scroll"/page
bool btnCPressed();             // WIO_KEY_C (leftmost top button)  — "deny"
bool btnAHeld(uint16_t ms);     // long-press A → menu
bool joyUp(); bool joyDown(); bool joyPress();

// ── accelerometer ──
bool platformShaken();          // transient shake spike
bool platformFaceDown();        // sustained face-down

// ── outputs ──
void beep(uint16_t freq, uint16_t durMs);
void backlight(bool on);
void attentionLed(bool on);     // Wio built-in LED

// ── misc ──
uint32_t freeHeapApprox();      // rough free RAM (for info screen parity)
