#include "sensors.h"

#include <Wire.h>

#include "app_config.h"
#include "battery.h"
#include "espnow_hub.h"
#include "history.h"
#include "log.h"
#include "notify.h"
#include "pins.h"
#include "runtime.h"
#include "sht31.h"
#include "timekeeper.h"

namespace hub::sensors {
namespace {

constexpr uint32_t kHubReadPeriodMs = 30000;
constexpr uint32_t kHubFreshMs = 120000;
klima::Sht31 s_sht(0x44);
bool s_sht_present = false;
uint32_t s_last_read_ms = 0;
uint32_t s_last_probe_ms = 0;
uint8_t s_fail_count = 0;

float clampHum(float h) { return h < 0 ? 0 : (h > 100 ? 100 : h); }

uint16_t effectiveInterval(int slot) {
  uint16_t iv = g_cfg.sensors[slot].interval_s;
  if (g_rt.sensors[slot].interval_s > iv) iv = g_rt.sensors[slot].interval_s;
  return iv ? iv : klima::kDefaultIntervalS;
}

void readHub(uint32_t now) {
  HubRuntime& h = g_rt.hub;
  if (!g_cfg.hub_sensor.enabled) {
    if (h.sensor_ok) markStateDirty();
    h.sensor_ok = false;
    return;
  }
  if (!s_sht_present) {
    if (s_last_probe_ms && now - s_last_probe_ms < 60000) return;
    s_last_probe_ms = now;
    s_sht_present = s_sht.begin(Wire);
    if (!s_sht_present) {
      if (h.sensor_ok) markStateDirty();
      h.sensor_ok = false;
      return;
    }
    LOGI("Hub SHT31 bulundu");
  }
  float t, hu;
  if (s_sht.measure(t, hu)) {
    t += g_cfg.hub_sensor.t_off;
    hu = clampHum(hu + g_cfg.hub_sensor.h_off);
    bool changed = !h.sensor_ok || fabsf(t - h.temp) >= 0.1f || fabsf(hu - h.hum) >= 0.5f;
    h.temp = t;
    h.hum = hu;
    h.sensor_ok = true;
    h.last_read_ms = now;
    s_fail_count = 0;
    history::add(kHubSource, timekeeper::now(), t, hu);
    if (changed) markStateDirty();
  } else if (++s_fail_count >= 3) {
    LOGW("Hub SHT31 okunamiyor");
    h.sensor_ok = false;
    s_sht_present = false;
    markStateDirty();
  }
}

void checkOffline(uint32_t now) {
  for (int i = 0; i < kMaxSensors; i++) {
    const SensorCfg& c = g_cfg.sensors[i];
    SensorRuntime& s = g_rt.sensors[i];
    if (!c.used) continue;
    uint32_t timeout_ms = (uint32_t(effectiveInterval(i)) * 25 / 10 + 120) * 1000UL;
    uint32_t since = s.seen ? now - s.last_seen_ms : now;  // hiç görülmediyse açılıştan beri
    if (since > timeout_ms) {
      if (s.online) {
        s.online = false;
        markStateDirty();
      }
      if (!s.offline_notified) {
        s.offline_notified = true;
        char msg[128];
        snprintf(msg, sizeof(msg), "⚠️ %s sensörü %lu dakikadır veri göndermiyor.", c.name.c_str(),
                 (unsigned long)(since / 60000));
        notify::send(notify::Kind::kOffline, msg);
      }
    }
  }
}

}  // namespace

void begin() {
  Wire.begin(pins::kSda, pins::kScl, 100000);
  s_sht_present = s_sht.begin(Wire);
  LOGI("Hub SHT31 %s", s_sht_present ? "hazir" : "bulunamadi");
  readHub(millis());
  s_last_read_ms = millis();
}

void loop() {
  uint32_t now = millis();
  if (now - s_last_read_ms >= kHubReadPeriodMs) {
    s_last_read_ms = now;
    readHub(now);
    checkOffline(now);
  }
}

void onReport(int slot, const uint8_t* mac, int8_t rssi, const klima::Report& r, bool is_new) {
  if (slot < 0 || slot >= kMaxSensors) return;
  SensorCfg& c = g_cfg.sensors[slot];
  SensorRuntime& s = g_rt.sensors[slot];

  if (is_new || !c.used || memcmp(c.mac, mac, 6) != 0) {
    c = SensorCfg();
    c.used = true;
    memcpy(c.mac, mac, 6);
    c.name = String("Sensör ") + (slot + 1);
    c.interval_s = klima::kDefaultIntervalS;
    s = SensorRuntime();
    history::clearSource(slot + 1);
    config::markDirty();
    espnow::syncDirectory();
    LOGI("Yeni sensor kaydedildi: yuva %d, %s", slot + 1, config::macToString(mac).c_str());
    char msg[96];
    snprintf(msg, sizeof(msg), "🆕 Yeni sensör eklendi: %s (%s)", c.name.c_str(),
             config::macToString(mac).c_str());
    notify::send(notify::Kind::kInfo, msg);
  }

  bool was_online = s.online;
  s.seen = true;
  s.online = true;
  s.last_seen_ms = millis();
  s.batt_mv = r.f.batt_mv;
  s.batt_pct = r.f.batt_pct;
  s.flags = r.f.flags;
  s.interval_s = r.f.interval_s;
  s.boot_count = r.f.boot_count;
  s.fw_major = r.f.fw_major;
  s.fw_minor = r.f.fw_minor;
  s.rssi = rssi;
  s.reports++;

  uint32_t now_ep = timekeeper::now();
  int n = r.f.n_readings;
  int newest = -1;
  uint32_t newest_age = UINT32_MAX;
  // Eskiden yeniye işle (zaman damgası sırası korunsun)
  for (int i = n - 1; i >= 0; i--) {
    const klima::Reading& rd = r.r[i];
    float t = rd.temp_c100 / 100.0f + c.t_off;
    float h = clampHum(rd.hum_c100 / 100.0f + c.h_off);
    if (rd.age_s < newest_age) {
      newest_age = rd.age_s;
      newest = i;
    }
    if (!now_ep) continue;
    uint32_t ep = now_ep - rd.age_s;
    if (s.last_reading_epoch > ep + 3600) s.last_reading_epoch = 0;  // saat geri gitti
    if (s.last_reading_epoch && ep <= s.last_reading_epoch + 5) continue;  // tekrar gelen ölçüm
    history::add(slot + 1, ep, t, h);
    s.last_reading_epoch = ep;
  }
  uint32_t fresh_limit = uint32_t(effectiveInterval(slot)) * 2;
  if (newest >= 0 && newest_age <= fresh_limit) {
    s.temp = r.r[newest].temp_c100 / 100.0f + c.t_off;
    s.hum = clampHum(r.r[newest].hum_c100 / 100.0f + c.h_off);
    s.has_reading = true;
    s.last_reading_ms = millis() - newest_age * 1000UL;
  }

  // Bildirimler
  if ((r.f.flags & klima::kFlagLowBattery) && !(r.f.flags & klima::kFlagUsbPower)) {
    if (!s.low_batt_notified) {
      s.low_batt_notified = true;
      char msg[128];
      snprintf(msg, sizeof(msg), "🔋 %s pili zayıf: %%%u (%.2f V). USB-C ile şarj edin.",
               c.name.c_str(), r.f.batt_pct, r.f.batt_mv / 1000.0f);
      notify::send(notify::Kind::kBattery, msg);
    }
  } else if (r.f.batt_pct > 20 || (r.f.flags & klima::kFlagUsbPower)) {
    s.low_batt_notified = false;
  }
  if (!was_online && s.offline_notified) {
    char msg[96];
    snprintf(msg, sizeof(msg), "✅ %s sensörü tekrar veri gönderiyor.", c.name.c_str());
    notify::send(notify::Kind::kOffline, msg);
  }
  s.offline_notified = false;
  markStateDirty();
}

void snapshot(SourceReading out[kNumSources]) {
  uint32_t now = millis();
  out[kHubSource].valid = g_rt.hub.sensor_ok && now - g_rt.hub.last_read_ms < kHubFreshMs;
  out[kHubSource].temp = g_rt.hub.temp;
  out[kHubSource].hum = g_rt.hub.hum;
  for (int i = 0; i < kMaxSensors; i++) {
    const SensorRuntime& s = g_rt.sensors[i];
    SourceReading& o = out[i + 1];
    uint32_t fresh_ms = uint32_t(effectiveInterval(i)) * 3000UL + 60000UL;
    o.valid = g_cfg.sensors[i].used && s.online && s.has_reading && now - s.last_reading_ms < fresh_ms;
    o.temp = s.temp;
    o.hum = s.hum;
  }
}

bool exists(int src) {
  if (src == kHubSource) return true;
  return src >= 1 && src <= kMaxSensors && g_cfg.sensors[src - 1].used;
}

bool online(int src) {
  if (src == kHubSource) return g_rt.hub.sensor_ok;
  return exists(src) && g_rt.sensors[src - 1].online;
}

String name(int src) {
  if (src == kHubSource) return g_cfg.hub_sensor.name;
  if (src == kSourceAverage) return "Ortalama";
  if (exists(src)) return g_cfg.sensors[src - 1].name;
  return String("Sensör ") + src;
}

void toJson(JsonArray arr) {
  uint32_t now = millis();
  {
    JsonObject o = arr.add<JsonObject>();
    o["id"] = 0;
    o["kind"] = "hub";
    o["name"] = g_cfg.hub_sensor.name;
    o["enabled"] = g_cfg.hub_sensor.enabled;
    o["online"] = g_rt.hub.sensor_ok;
    if (g_rt.hub.sensor_ok) {
      o["temp"] = roundf(g_rt.hub.temp * 10) / 10;
      o["hum"] = roundf(g_rt.hub.hum * 10) / 10;
      o["age_s"] = (now - g_rt.hub.last_read_ms) / 1000;
    }
    o["t_off"] = g_cfg.hub_sensor.t_off;
    o["h_off"] = g_cfg.hub_sensor.h_off;
  }
  for (int i = 0; i < kMaxSensors; i++) {
    const SensorCfg& c = g_cfg.sensors[i];
    if (!c.used) continue;
    const SensorRuntime& s = g_rt.sensors[i];
    JsonObject o = arr.add<JsonObject>();
    o["id"] = i + 1;
    o["kind"] = "sensor";
    o["name"] = c.name;
    o["mac"] = config::macToString(c.mac);
    o["online"] = s.online;
    if (s.has_reading) {
      o["temp"] = roundf(s.temp * 10) / 10;
      o["hum"] = roundf(s.hum * 10) / 10;
      o["age_s"] = (now - s.last_reading_ms) / 1000;
    }
    if (s.seen) {
      o["seen_s"] = (now - s.last_seen_ms) / 1000;
      o["batt_mv"] = s.batt_mv;
      o["batt_pct"] = s.batt_pct;
      o["usb"] = (s.flags & klima::kFlagUsbPower) != 0;
      o["low_batt"] = (s.flags & klima::kFlagLowBattery) != 0;
      o["sensor_error"] = (s.flags & klima::kFlagSensorError) != 0;
      o["rssi"] = s.rssi;
      o["fw"] = String(s.fw_major) + "." + s.fw_minor;
      o["boots"] = s.boot_count;
      o["reports"] = s.reports;
      o["interval_now"] = s.interval_s;
    }
    o["interval_min"] = c.interval_s / 60;
    o["t_off"] = c.t_off;
    o["h_off"] = c.h_off;
  }
}

bool configure(int src, JsonObjectConst o, String& err) {
  if (src == kHubSource) {
    HubSensorCfg& h = g_cfg.hub_sensor;
    if (o["name"].is<const char*>()) h.name = o["name"].as<String>();
    if (o["enabled"].is<bool>()) h.enabled = o["enabled"];
    if (o["t_off"].is<float>()) {
      float nv = constrain(o["t_off"].as<float>(), -10.0f, 10.0f);
      g_rt.hub.temp += nv - h.t_off;
      h.t_off = nv;
    }
    if (o["h_off"].is<float>()) {
      float nv = constrain(o["h_off"].as<float>(), -30.0f, 30.0f);
      g_rt.hub.hum = clampHum(g_rt.hub.hum + nv - h.h_off);
      h.h_off = nv;
    }
    config::markDirty();
    markStateDirty();
    return true;
  }
  if (!exists(src)) {
    err = "sensor bulunamadi";
    return false;
  }
  SensorCfg& c = g_cfg.sensors[src - 1];
  SensorRuntime& s = g_rt.sensors[src - 1];
  if (o["name"].is<const char*>()) {
    String n = o["name"].as<String>();
    n.trim();
    if (n.length() >= 1 && n.length() <= 24) c.name = n;
  }
  if (o["interval_min"].is<int>()) {
    c.interval_s = klima::clampInterval(uint32_t(o["interval_min"].as<int>()) * 60);
  }
  if (o["t_off"].is<float>()) {
    float nv = constrain(o["t_off"].as<float>(), -10.0f, 10.0f);
    s.temp += nv - c.t_off;
    c.t_off = nv;
  }
  if (o["h_off"].is<float>()) {
    float nv = constrain(o["h_off"].as<float>(), -30.0f, 30.0f);
    s.hum = clampHum(s.hum + nv - c.h_off);
    c.h_off = nv;
  }
  config::markDirty();
  espnow::syncDirectory();
  markStateDirty();
  return true;
}

bool remove(int slot) {
  if (slot < 1 || slot > kMaxSensors || !g_cfg.sensors[slot - 1].used) return false;
  g_cfg.sensors[slot - 1] = SensorCfg();
  g_rt.sensors[slot - 1] = SensorRuntime();
  history::clearSource(slot);
  config::markDirty();
  espnow::syncDirectory();
  markStateDirty();
  return true;
}

}  // namespace hub::sensors
