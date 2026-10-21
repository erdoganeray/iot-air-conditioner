#include "app_config.h"

#include <LittleFS.h>

#include "log.h"

namespace hub {

AppConfig g_cfg;

namespace config {
namespace {

constexpr const char* kPath = "/config.json";
constexpr const char* kTmpPath = "/config.tmp";
bool s_dirty = false;
uint32_t s_dirty_since = 0;

String hhmm(uint16_t minute) {
  char b[6];
  snprintf(b, sizeof(b), "%02u:%02u", unsigned(minute / 60) % 24, unsigned(minute % 60));
  return String(b);
}

bool parseHhmm(JsonVariantConst v, uint16_t& out) {
  if (v.is<int>()) {
    int m = v.as<int>();
    if (m < 0 || m > 1439) return false;
    out = uint16_t(m);
    return true;
  }
  const char* s = v.as<const char*>();
  if (!s) return false;
  int h = 0, m = 0;
  if (sscanf(s, "%d:%d", &h, &m) != 2) return false;
  if (h < 0 || h > 23 || m < 0 || m > 59) return false;
  out = uint16_t(h * 60 + m);
  return true;
}

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

void acStateToJson(const AcState& s, JsonObject o) {
  o["mode"] = modeName(s.mode);
  o["temp"] = s.temp;
  o["fan"] = fanName(s.fan);
  o["swing"] = s.swing;
}

void acStateFromJson(JsonObjectConst o, AcState& s) {
  AcMode m;
  if (parseMode(o["mode"] | "", m)) s.mode = m;
  if (o["temp"].is<float>()) s.temp = clampAcTemp(o["temp"].as<float>());
  AcFan f;
  if (parseFan(o["fan"] | "", f)) s.fan = f;
  if (o["swing"].is<bool>()) s.swing = o["swing"].as<bool>();
}

void sensorsToFile(JsonArray arr) {
  for (int i = 0; i < kMaxSensors; i++) {
    const SensorCfg& s = g_cfg.sensors[i];
    if (!s.used) continue;
    JsonObject o = arr.add<JsonObject>();
    o["slot"] = i + 1;
    o["mac"] = macToString(s.mac);
    o["name"] = s.name;
    o["interval"] = s.interval_s;
    o["t_off"] = s.t_off;
    o["h_off"] = s.h_off;
  }
}

void sensorsFromFile(JsonArrayConst arr) {
  for (JsonObjectConst o : arr) {
    int slot = o["slot"] | 0;
    if (slot < 1 || slot > kMaxSensors) continue;
    SensorCfg& s = g_cfg.sensors[slot - 1];
    if (!parseMac(o["mac"] | "", s.mac)) continue;
    s.used = true;
    String def_name = String("Sensör ") + String(slot);
    s.name = o["name"] | def_name;
    s.interval_s = o["interval"] | 600;
    s.t_off = o["t_off"] | 0.0f;
    s.h_off = o["h_off"] | 0.0f;
  }
}

void toFile(JsonDocument& doc) {
  doc["v"] = 1;
  doc["pin"] = g_cfg.pin;
  JsonArray w = doc["wifi"].to<JsonArray>();
  for (int i = 0; i < g_cfg.n_wifi; i++) {
    JsonObject o = w.add<JsonObject>();
    o["ssid"] = g_cfg.wifi[i].ssid;
    o["pass"] = g_cfg.wifi[i].pass;
    o["ch"] = g_cfg.wifi[i].channel;
    if (g_cfg.wifi[i].has_bssid) o["bssid"] = macToString(g_cfg.wifi[i].bssid);
  }
  JsonObject ir = doc["ir"].to<JsonObject>();
  ir["protocol"] = g_cfg.ir.protocol;
  ir["model"] = g_cfg.ir.model;
  ir["raw_mode"] = g_cfg.ir.raw_mode;
  settingsToJson(doc["settings"].to<JsonObject>());
  autoToJson(doc["auto"].to<JsonObject>());
  sensorsToFile(doc["sensors"].to<JsonArray>());
  JsonObject tg = doc["telegram"].to<JsonObject>();
  telegramToJson(tg);
  tg["token"] = g_cfg.tg.token;  // dosyada gerçek token saklanır
}

void fromFile(JsonDocument& doc) {
  g_cfg.pin = doc["pin"] | "1234";
  g_cfg.n_wifi = 0;
  for (JsonObjectConst o : doc["wifi"].as<JsonArrayConst>()) {
    if (g_cfg.n_wifi >= kMaxWifi) break;
    WifiNet& n = g_cfg.wifi[g_cfg.n_wifi];
    n.ssid = o["ssid"] | "";
    n.pass = o["pass"] | "";
    n.channel = o["ch"] | 0;
    n.has_bssid = parseMac(o["bssid"] | "", n.bssid);
    if (n.ssid.length()) g_cfg.n_wifi++;
  }
  JsonObjectConst ir = doc["ir"];
  g_cfg.ir.protocol = ir["protocol"] | "";
  g_cfg.ir.model = ir["model"] | -1;
  g_cfg.ir.raw_mode = ir["raw_mode"] | false;
  String err;
  settingsFromJson(doc["settings"], err);
  autoFromJson(doc["auto"], err);
  sensorsFromFile(doc["sensors"]);
  telegramFromJson(doc["telegram"], err);
  g_cfg.tg.token = doc["telegram"]["token"] | "";
}

}  // namespace

String macToString(const uint8_t* mac) {
  char b[18];
  snprintf(b, sizeof(b), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4],
           mac[5]);
  return String(b);
}

bool parseMac(const char* s, uint8_t* out) {
  if (!s) return false;
  unsigned v[6];
  if (sscanf(s, "%x:%x:%x:%x:%x:%x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6) return false;
  for (int i = 0; i < 6; i++) {
    if (v[i] > 255) return false;
    out[i] = uint8_t(v[i]);
  }
  return true;
}

bool load() {
  if (!LittleFS.exists(kPath)) {
    LOGI("Ayar dosyasi yok, varsayilanlar kullaniliyor");
    return false;
  }
  File f = LittleFS.open(kPath, "r");
  if (!f) return false;
  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, f);
  f.close();
  if (e) {
    LOGE("config.json bozuk: %s (varsayilanlar)", e.c_str());
    return false;
  }
  fromFile(doc);
  LOGI("Ayarlar yuklendi (%d WiFi, %d zamanlayici)", g_cfg.n_wifi, g_cfg.n_sched);
  return true;
}

void markDirty() {
  if (!s_dirty) s_dirty_since = millis();
  s_dirty = true;
}

bool saveNow() {
  JsonDocument doc;
  toFile(doc);
  // Önce geçici dosyaya yaz, sonra yer değiştir: yazarken elektrik kesilirse
  // eski dosya bozulmaz.
  File f = LittleFS.open(kTmpPath, "w");
  if (!f) {
    LOGE("config.tmp acilamadi");
    return false;
  }
  size_t n = serializeJson(doc, f);
  f.close();
  if (n == 0) return false;
  LittleFS.remove(kPath);
  bool ok = LittleFS.rename(kTmpPath, kPath);
  s_dirty = false;
  LOGI("Ayarlar kaydedildi (%u bayt)", unsigned(n));
  return ok;
}

void loop() {
  if (s_dirty && millis() - s_dirty_since > 3000) saveNow();
}

void factoryReset() {
  LittleFS.remove(kPath);
  LittleFS.remove(kTmpPath);
  g_cfg = AppConfig();
}

void wifiToJson(JsonArray arr) {
  for (int i = 0; i < g_cfg.n_wifi; i++) {
    JsonObject o = arr.add<JsonObject>();
    o["ssid"] = g_cfg.wifi[i].ssid;
    o["channel"] = g_cfg.wifi[i].channel;
  }
}

// ----------------------------------------------------------------- otomasyon
void autoToJson(JsonObject obj) {
  JsonObject th = obj["thermo"].to<JsonObject>();
  th["enabled"] = g_cfg.thermo.enabled;
  th["source"] = g_cfg.thermo.source;
  th["mode"] = modeName(g_cfg.thermo.mode);
  th["target"] = g_cfg.thermo.target;
  th["hyst"] = g_cfg.thermo.hyst;
  th["ac_temp"] = g_cfg.thermo.ac_temp;
  th["fan"] = fanName(g_cfg.thermo.fan);

  JsonObject dr = obj["dry"].to<JsonObject>();
  dr["enabled"] = g_cfg.dry.enabled;
  dr["source"] = g_cfg.dry.source;
  dr["on"] = g_cfg.dry.on_pct;
  dr["off"] = g_cfg.dry.off_pct;
  dr["ac_temp"] = g_cfg.dry.ac_temp;

  JsonObject tm = obj["timing"].to<JsonObject>();
  tm["min_on_min"] = g_cfg.timing.min_on_s / 60;
  tm["min_off_min"] = g_cfg.timing.min_off_s / 60;
  tm["override_min"] = g_cfg.timing.override_s / 60;

  JsonArray sc = obj["schedules"].to<JsonArray>();
  for (int i = 0; i < g_cfg.n_sched; i++) {
    const ScheduleEntry& e = g_cfg.sched[i];
    JsonObject o = sc.add<JsonObject>();
    o["enabled"] = e.enabled;
    o["days"] = e.days;
    o["time"] = hhmm(e.minute);
    o["action"] = schedActionName(e.action);
    o["mode"] = modeName(e.mode);
    o["temp"] = e.temp;
  }
}

bool autoFromJson(JsonObjectConst obj, String& err) {
  if (obj.isNull()) return true;
  JsonObjectConst th = obj["thermo"];
  if (!th.isNull()) {
    ThermoConfig& c = g_cfg.thermo;
    if (th["enabled"].is<bool>()) c.enabled = th["enabled"];
    if (th["source"].is<int>()) {
      int s = th["source"];
      if ((s >= 0 && s <= kMaxSensors) || s == kSourceAverage) c.source = uint8_t(s);
    }
    AcMode m;
    if (parseMode(th["mode"] | "", m) && (m == AcMode::kCool || m == AcMode::kHeat)) c.mode = m;
    if (th["target"].is<float>()) c.target = clampf(th["target"], 10, 35);
    if (th["hyst"].is<float>()) c.hyst = clampf(th["hyst"], 0.2f, 3.0f);
    if (th["ac_temp"].is<float>()) c.ac_temp = clampAcTemp(th["ac_temp"]);
    AcFan f;
    if (parseFan(th["fan"] | "", f)) c.fan = f;
  }
  JsonObjectConst dr = obj["dry"];
  if (!dr.isNull()) {
    DryConfig& c = g_cfg.dry;
    if (dr["enabled"].is<bool>()) c.enabled = dr["enabled"];
    if (dr["source"].is<int>()) {
      int s = dr["source"];
      if ((s >= 0 && s <= kMaxSensors) || s == kSourceAverage) c.source = uint8_t(s);
    }
    if (dr["on"].is<float>()) c.on_pct = clampf(dr["on"], 30, 95);
    if (dr["off"].is<float>()) c.off_pct = clampf(dr["off"], 20, 90);
    if (c.off_pct > c.on_pct - 3) c.off_pct = c.on_pct - 3;
    if (dr["ac_temp"].is<float>()) c.ac_temp = clampAcTemp(dr["ac_temp"]);
  }
  JsonObjectConst tm = obj["timing"];
  if (!tm.isNull()) {
    if (tm["min_on_min"].is<int>()) g_cfg.timing.min_on_s = uint32_t(constrain(tm["min_on_min"].as<int>(), 0, 60)) * 60;
    if (tm["min_off_min"].is<int>()) g_cfg.timing.min_off_s = uint32_t(constrain(tm["min_off_min"].as<int>(), 0, 60)) * 60;
    if (tm["override_min"].is<int>()) g_cfg.timing.override_s = uint32_t(constrain(tm["override_min"].as<int>(), 0, 720)) * 60;
  }
  JsonArrayConst sc = obj["schedules"];
  if (!sc.isNull()) {
    int n = 0;
    for (JsonObjectConst o : sc) {
      if (n >= kMaxSchedules) {
        err = "en fazla 16 zamanlayici";
        break;
      }
      ScheduleEntry e;
      e.enabled = o["enabled"] | true;
      e.days = uint8_t(o["days"] | 0x7F) & 0x7F;
      if (!parseHhmm(o["time"], e.minute)) {
        err = "gecersiz saat";
        continue;
      }
      if (!parseSchedAction(o["action"] | "", e.action)) {
        err = "gecersiz islem";
        continue;
      }
      AcMode m;
      if (parseMode(o["mode"] | "", m)) e.mode = m;
      e.temp = clampAcTemp(o["temp"] | 24.0f);
      g_cfg.sched[n++] = e;
    }
    g_cfg.n_sched = n;
  }
  return err.length() == 0;
}

// ------------------------------------------------------------------- ayarlar
void settingsToJson(JsonObject obj) {
  obj["name"] = g_cfg.name;
  obj["tz"] = g_cfg.tz;
  obj["ble_always"] = g_cfg.ble_always;
  obj["auto_accept"] = g_cfg.auto_accept_sensors;
  JsonObject led = obj["led"].to<JsonObject>();
  led["brightness"] = g_cfg.led.brightness;
  led["night"] = g_cfg.led.night;
  led["night_start"] = hhmm(g_cfg.led.night_start);
  led["night_end"] = hhmm(g_cfg.led.night_end);
  acStateToJson(g_cfg.ac_default, obj["ac_default"].to<JsonObject>());
  JsonObject hs = obj["hub_sensor"].to<JsonObject>();
  hs["enabled"] = g_cfg.hub_sensor.enabled;
  hs["name"] = g_cfg.hub_sensor.name;
  hs["t_off"] = g_cfg.hub_sensor.t_off;
  hs["h_off"] = g_cfg.hub_sensor.h_off;
}

bool settingsFromJson(JsonObjectConst obj, String& err) {
  if (obj.isNull()) return true;
  if (obj["name"].is<const char*>()) {
    String n = obj["name"].as<String>();
    n.trim();
    if (n.length() >= 1 && n.length() <= 24) g_cfg.name = n;
  }
  if (obj["pin"].is<const char*>()) {
    String p = obj["pin"].as<String>();
    if (p.length() >= 4 && p.length() <= 16) {
      g_cfg.pin = p;
    } else {
      err = "PIN 4-16 karakter olmali";
    }
  }
  if (obj["tz"].is<const char*>()) g_cfg.tz = obj["tz"].as<String>();
  if (obj["ble_always"].is<bool>()) g_cfg.ble_always = obj["ble_always"];
  if (obj["auto_accept"].is<bool>()) g_cfg.auto_accept_sensors = obj["auto_accept"];
  JsonObjectConst led = obj["led"];
  if (!led.isNull()) {
    if (led["brightness"].is<int>()) g_cfg.led.brightness = uint8_t(constrain(led["brightness"].as<int>(), 0, 100));
    if (led["night"].is<bool>()) g_cfg.led.night = led["night"];
    parseHhmm(led["night_start"], g_cfg.led.night_start);
    parseHhmm(led["night_end"], g_cfg.led.night_end);
  }
  if (!obj["ac_default"].isNull()) acStateFromJson(obj["ac_default"], g_cfg.ac_default);
  JsonObjectConst hs = obj["hub_sensor"];
  if (!hs.isNull()) {
    if (hs["enabled"].is<bool>()) g_cfg.hub_sensor.enabled = hs["enabled"];
    if (hs["name"].is<const char*>()) g_cfg.hub_sensor.name = hs["name"].as<String>();
    if (hs["t_off"].is<float>()) g_cfg.hub_sensor.t_off = clampf(hs["t_off"], -10, 10);
    if (hs["h_off"].is<float>()) g_cfg.hub_sensor.h_off = clampf(hs["h_off"], -30, 30);
  }
  return err.length() == 0;
}

// ------------------------------------------------------------------ telegram
void telegramToJson(JsonObject obj) {
  obj["enabled"] = g_cfg.tg.enabled;
  obj["has_token"] = g_cfg.tg.token.length() > 0;
  JsonArray c = obj["chats"].to<JsonArray>();
  for (int i = 0; i < g_cfg.tg.n_chats; i++) c.add(g_cfg.tg.chats[i]);
  JsonObject n = obj["notify"].to<JsonObject>();
  n["battery"] = g_cfg.tg.notify_battery;
  n["offline"] = g_cfg.tg.notify_offline;
  n["power"] = g_cfg.tg.notify_power;
  n["wifi"] = g_cfg.tg.notify_wifi;
  n["auto"] = g_cfg.tg.notify_auto;
  obj["insecure"] = g_cfg.tg.insecure;
}

bool telegramFromJson(JsonObjectConst obj, String& err) {
  if (obj.isNull()) return true;
  TelegramCfg& t = g_cfg.tg;
  if (obj["enabled"].is<bool>()) t.enabled = obj["enabled"];
  if (obj["token"].is<const char*>()) {
    String tok = obj["token"].as<String>();
    tok.trim();
    if (tok.length() == 0 || tok.indexOf(':') > 0) {
      t.token = tok;
    } else {
      err = "token bicimi hatali (123456:ABC... olmali)";
    }
  }
  JsonArrayConst c = obj["chats"];
  if (!c.isNull()) {
    t.n_chats = 0;
    for (JsonVariantConst v : c) {
      if (t.n_chats >= kMaxTelegramChats) break;
      int64_t id = v.as<int64_t>();
      if (id != 0) t.chats[t.n_chats++] = id;
    }
  }
  JsonObjectConst n = obj["notify"];
  if (!n.isNull()) {
    if (n["battery"].is<bool>()) t.notify_battery = n["battery"];
    if (n["offline"].is<bool>()) t.notify_offline = n["offline"];
    if (n["power"].is<bool>()) t.notify_power = n["power"];
    if (n["wifi"].is<bool>()) t.notify_wifi = n["wifi"];
    if (n["auto"].is<bool>()) t.notify_auto = n["auto"];
  }
  if (obj["insecure"].is<bool>()) t.insecure = obj["insecure"];
  return err.length() == 0;
}

}  // namespace config
}  // namespace hub
