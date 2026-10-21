// Klima sistemi — HUB (Seeed XIAO ESP32-S3)
//
// Modüller:
//   wifi_mgr   : dayanıklı WiFi bağlantısı, kurulum ağı, mDNS
//   espnow_hub : sensörlerden ESP-NOW raporları + ACK
//   sensors    : hub SHT31 + uzak sensör kayıtları
//   ir_ctrl    : klima IR gönderme / öğrenme / kumanda senkronu
//   automation : termostat, nem (kurutma), zamanlayıcı
//   api        : JSON komutları (BLE, WebSocket, USB seri)
//   ble_srv    : Bluetooth arayüzü
//   web_srv    : yerel web uygulaması + WebSocket + OTA
//   telegram   : uzaktan komut ve bildirim
//   history    : 7 günlük geçmiş, klima olay günlüğü
//   power, led, touch, timekeeper
//
// Mimari: docs/MIMARI.md    API: docs/API.md

#include <Arduino.h>
#include <LittleFS.h>
#include <esp_task_wdt.h>

#include "api.h"
#include "app_config.h"
#include "automation.h"
#include "ble_srv.h"
#include "espnow_hub.h"
#include "history.h"
#include "ir_ctrl.h"
#include "led.h"
#include "log.h"
#include "power.h"
#include "runtime.h"
#include "sensors.h"
#include "telegram.h"
#include "timekeeper.h"
#include "touch.h"
#include "version.h"
#include "web_srv.h"
#include "wifi_mgr.h"

namespace hub {
Runtime g_rt;
}

using namespace hub;

namespace {

constexpr uint32_t kWatchdogMs = 30000;
String s_serial_line;
bool s_last_usb = true;

void onGesture(touch::Gesture g) {
  String err;
  JsonDocument d;
  switch (g) {
    case touch::Gesture::kTap:
      LOGI("Dokunma: klima ac/kapa");
      d["power"] = !g_rt.ac.state.power;
      if (!api::setAc(d.as<JsonObjectConst>(), uint8_t(Source::kTouch), err)) {
        LOGW("Klima komutu basarisiz: %s", err.c_str());
        led::flashRed();
      }
      break;
    case touch::Gesture::kDoubleTap:
      LOGI("Cift dokunma: kurulum agi + Bluetooth 10 dk");
      net::startAp(10);
      ble::requestOn(15);
      break;
    case touch::Gesture::kLong:
      LOGI("Uzun basma: IR ogrenme");
      if (!ir::startLearning("", "", false, err)) led::flashRed();
      break;
    case touch::Gesture::kVeryLong:
      LOGW("10 sn basma: FABRIKA AYARLARI");
      LittleFS.format();
      delay(200);
      ESP.restart();
      break;
  }
}

void readSerial() {
  while (Serial.available()) {
    char c = char(Serial.read());
    if (c == '\n' || c == '\r') {
      if (s_serial_line.length()) {
        api::enqueue(api::Transport::kSerial, 0, s_serial_line.c_str(), s_serial_line.length());
        s_serial_line = "";
      }
    } else if (s_serial_line.length() < 2048) {
      s_serial_line += c;
    }
  }
}

void setupWatchdog() {
  esp_task_wdt_config_t cfg = {};
  cfg.timeout_ms = kWatchdogMs;
  cfg.idle_core_mask = 0;
  cfg.trigger_panic = true;
  if (esp_task_wdt_reconfigure(&cfg) != ESP_OK) esp_task_wdt_init(&cfg);
  esp_task_wdt_add(nullptr);
}

}  // namespace

void setup() {
  power::earlyCheck();  // kritik pil uykusundan uyanıldıysa gerekirse tekrar uyu
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);  // USB bağlı değilken yazma bloklamasın
  delay(200);
  LOGI("=== KlimaHub v%s (reset sebebi %d) ===", HUB_FW_VERSION, int(esp_reset_reason()));

  if (!LittleFS.begin(true)) LOGE("LittleFS baglanamadi!");
  config::load();
  timekeeper::begin();

  led::begin();
  touch::begin(onGesture);
  power::begin();
  history::begin();
  sensors::begin();
  ir::begin();
  api::begin();

  net::begin();
  espnow::begin();
  web::begin();
  ble::begin();
  telegram::begin();

  s_last_usb = g_rt.hub.usb;
  ble::setLowPower(!g_rt.hub.usb);
  setupWatchdog();
  LOGI("Hazir. Bos bellek: %u, PSRAM: %u", unsigned(ESP.getFreeHeap()), unsigned(ESP.getFreePsram()));
}

void loop() {
  esp_task_wdt_reset();
  readSerial();

  net::loop();
  timekeeper::loop();
  espnow::loop();
  sensors::loop();
  ir::loop();
  automation::loop();
  api::loop();
  ble::loop();
  web::loop();
  telegram::loop();
  touch::loop();
  led::loop();
  power::loop();
  history::loop();
  config::loop();

  if (g_rt.hub.usb != s_last_usb) {
    s_last_usb = g_rt.hub.usb;
    ble::setLowPower(!s_last_usb);
  }
  delay(2);
}
