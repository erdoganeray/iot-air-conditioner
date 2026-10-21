// WiFi yöneticisi: kopmalara karşı dayanıklı yeniden bağlanma, kurulum ağı (SoftAP),
// mDNS (klima.local), ağ taraması. Ayrıntı: docs/MIMARI.md §5.2
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

namespace hub::net {

enum class State : uint8_t { kNoConfig, kWaitRetry, kConnecting, kConnected };

void begin();
void loop();

bool connected();
State state();
const char* stateName();
String ssid();
int rssi();
String ip();
uint8_t channel();          // radyonun o anki kanalı (ESP-NOW için)
uint32_t outageSeconds();   // bağlantı kaçtır yok (bağlıysa 0)
uint32_t reconnectCount();
uint32_t nextRetryInS();

// Yeni ağ ekler (en başa) ve hemen bağlanmayı dener
void addNetwork(const String& ssid, const String& pass);
bool removeNetwork(const String& ssid);
void reconnectNow();

// Kurulum ağı: minutes=0 -> süresiz (WiFi ayarı yoksa)
void startAp(uint32_t minutes);
void stopAp();
bool apActive();
String apSsid();

// Asenkron tarama. Sonuçlar hazır olunca callback çağrılır (ana döngüde).
using ScanCallback = void (*)(JsonArray results, void* ctx);
bool startScan(ScanCallback cb, void* ctx);

void setInternetOk(bool ok);
bool internetOk();

}  // namespace hub::net
