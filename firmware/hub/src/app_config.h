// Kalıcı ayarlar: LittleFS üzerinde /config.json
// Yalnızca ana döngüden (loop) değiştirilir.
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "automation_logic.h"
#include "model.h"

namespace hub {

constexpr int kMaxWifi = 3;
constexpr int kMaxSchedules = 16;
constexpr int kMaxTelegramChats = 5;

struct WifiNet {
  String ssid;
  String pass;
  uint8_t channel = 0;  // son başarılı bağlantının kanalı (0 = bilinmiyor)
  uint8_t bssid[6] = {0};
  bool has_bssid = false;
};

struct SensorCfg {
  bool used = false;
  uint8_t mac[6] = {0};
  String name;
  uint16_t interval_s = 600;
  float t_off = 0;  // kalibrasyon: okunan değere eklenir
  float h_off = 0;
};

struct HubSensorCfg {
  bool enabled = true;
  String name = "Hub";
  float t_off = 0;
  float h_off = 0;
};

struct LedCfg {
  uint8_t brightness = 40;   // %
  bool night = true;
  uint16_t night_start = 23 * 60;
  uint16_t night_end = 7 * 60;
};

struct IrCfg {
  String protocol;   // ör. "FUJITSU_AC"; boş = protokol seçilmedi
  int16_t model = -1;
  bool raw_mode = false;  // true: öğrenilmiş ham komutlar kullanılır
};

struct TelegramCfg {
  bool enabled = false;
  String token;
  int64_t chats[kMaxTelegramChats] = {0};
  int n_chats = 0;
  bool notify_battery = true;
  bool notify_offline = true;
  bool notify_power = true;
  bool notify_wifi = true;
  bool notify_auto = false;
  bool insecure = false;  // sertifika doğrulamasını kapat (yalnızca sorun olursa)
};

struct AppConfig {
  String name = "KlimaHub";
  String pin = "1234";
  String tz = "<+03>-3";  // Türkiye (UTC+3, yaz saati yok)
  bool ble_always = true; // false: Bluetooth yalnızca gerektiğinde açılır
  bool auto_accept_sensors = true;

  WifiNet wifi[kMaxWifi];
  int n_wifi = 0;

  LedCfg led;
  IrCfg ir;
  AcState ac_default;  // dokunmatik butonla açılınca kullanılacak ayar

  ThermoConfig thermo;
  DryConfig dry;
  AutoTiming timing;
  ScheduleEntry sched[kMaxSchedules];
  int n_sched = 0;

  SensorCfg sensors[kMaxSensors];  // yuva 1..4 -> dizi 0..3
  HubSensorCfg hub_sensor;
  TelegramCfg tg;
};

extern AppConfig g_cfg;

namespace config {

bool load();
// Değişikliği işaretler; birkaç saniye içinde toplu olarak dosyaya yazılır.
void markDirty();
// Ana döngüden çağrılır.
void loop();
bool saveNow();
void factoryReset();

// JSON dönüşümleri (API için). secrets=false iken şifre/token gizlenir.
void wifiToJson(JsonArray arr);
void autoToJson(JsonObject obj);
bool autoFromJson(JsonObjectConst obj, String& err);
void settingsToJson(JsonObject obj);
bool settingsFromJson(JsonObjectConst obj, String& err);
void telegramToJson(JsonObject obj);
bool telegramFromJson(JsonObjectConst obj, String& err);

String macToString(const uint8_t* mac);
bool parseMac(const char* s, uint8_t* out);

}  // namespace config
}  // namespace hub
