/*
 * Minimal BLE write-path probe for the Wio Terminal (rpcBLE / RTL8720).
 * Shows ON THE SCREEN: connection state, count of writes received on the RX
 * characteristic, and the last bytes. Echoes {"ack":"rx","n":N} on each write.
 * No filesystem, no sprite, no JSON — isolates the BLE path; results are read
 * off the LCD (no dependency on flaky USB serial).
 *
 * TFT_eSPI and rpcBLE can't share one translation unit (Arduino min()/max()
 * macros break rpcBLE's STL), so the BLE side lives in probe_ble.cpp and we
 * share state via the globals below.
 *
 * Build (force the Wio LCD lib):
 *   LCD=~/Library/Arduino15/packages/Seeeduino/hardware/samd/1.8.5/libraries/Seeed_Arduino_LCD
 *   arduino-cli compile --fqbn Seeeduino:samd:seeed_wio_terminal --library "$LCD" -u -p <port> ble_probe.ino
 */
#include <TFT_eSPI.h>

TFT_eSPI tft;

// Shared with probe_ble.cpp (defined here, extern there).
volatile bool     gConnected = false;
volatile uint32_t gWrites    = 0;
volatile uint32_t gRxBytes   = 0;
volatile uint32_t gDisc      = 0;
char              gLast[80]  = "(none)";

void probeBleInit();
void probeBleLoop();

void setup() {
  Serial.begin(115200); delay(400);
  Serial.println("\n[probe] boot");
  tft.init();
  tft.setRotation(3);
  pinMode(LCD_BACKLIGHT, OUTPUT); digitalWrite(LCD_BACKLIGHT, HIGH);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2); tft.drawString("BLE probe: init", 8, 8);
  probeBleInit();
}

static uint32_t lastW = 0xFFFFFFFF, lastD = 0xFFFFFFFF;
static bool     lastConn = true;

void loop() {
  probeBleLoop();
  if (gConnected != lastConn || gWrites != lastW || gDisc != lastD) {
    lastConn = gConnected; lastW = gWrites; lastD = gDisc;
    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(2);
    tft.setTextColor(gConnected ? TFT_GREEN : TFT_RED, TFT_BLACK);
    tft.drawString(gConnected ? "CONNECTED" : "advertising", 8, 8);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    char b[48];
    snprintf(b, sizeof(b), "writes: %lu", (unsigned long)gWrites);     tft.drawString(b, 8, 44);
    snprintf(b, sizeof(b), "rx bytes: %lu", (unsigned long)gRxBytes);  tft.drawString(b, 8, 76);
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    snprintf(b, sizeof(b), "disconnects: %lu", (unsigned long)gDisc);  tft.drawString(b, 8, 108);
    tft.setTextSize(1); tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("last rx:", 8, 150);
    tft.drawString(gLast, 8, 165);
  }
  delay(10);
}
