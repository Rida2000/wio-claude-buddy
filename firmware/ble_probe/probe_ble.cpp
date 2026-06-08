// BLE side of the probe — kept in its own translation unit so it never shares
// with TFT_eSPI (Arduino min()/max() macros vs rpcBLE's STL).
#include <Arduino.h>
#include <string>
#include "rpcBLEDevice.h"
#include <BLEServer.h>
#include <BLE2902.h>

#define NUS_SVC "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define NUS_RX  "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
#define NUS_TX  "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

// Shared state (defined in ble_probe.ino).
extern volatile bool     gConnected;
extern volatile uint32_t gWrites;
extern volatile uint32_t gRxBytes;
extern volatile uint32_t gDisc;
extern char              gLast[80];

static BLECharacteristic* txc = nullptr;
static volatile bool needAdv = false;

class RxCb : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    std::string v = c->getValue();
    gWrites++; gRxBytes += v.size();
    size_t n = v.size(); if (n > 79) n = 79;
    memcpy(gLast, v.data(), n); gLast[n] = 0;
    Serial.printf("[onWrite] %u: %s\n", (unsigned)v.size(), gLast);
    if (txc && gConnected) {
      char a[48];
      int l = snprintf(a, sizeof(a), "{\"ack\":\"rx\",\"n\":%lu}\n", (unsigned long)gWrites);
      txc->setValue((uint8_t*)a, l);
      txc->notify();
    }
  }
};

class SrvCb : public BLEServerCallbacks {
  void onConnect(BLEServer*) override { gConnected = true; Serial.println("[conn]"); }
  void onDisconnect(BLEServer*) override {
    gConnected = false; gDisc++; needAdv = true;
    Serial.println("[disc]");
  }
};

void probeBleInit() {
  BLEDevice::init(std::string("Claude Wio"));
  BLEServer* s = BLEDevice::createServer();
  s->setCallbacks(new SrvCb());
  BLEService* svc = s->createService(NUS_SVC);
  txc = svc->createCharacteristic(NUS_TX, BLECharacteristic::PROPERTY_NOTIFY);
  txc->addDescriptor(new BLE2902());
  BLECharacteristic* rxc = svc->createCharacteristic(
    NUS_RX, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  rxc->setCallbacks(new RxCb());
  svc->start();
  BLEAdvertising* adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(NUS_SVC);
  adv->setScanResponse(true);
  BLEDevice::startAdvertising();
  Serial.println("[probe] advertising as 'Claude Wio'");
}

void probeBleLoop() {
  if (needAdv) { needAdv = false; BLEDevice::startAdvertising(); }
}
