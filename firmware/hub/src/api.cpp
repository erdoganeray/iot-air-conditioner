#include "api.h"

#include <Esp.h>
#include <LittleFS.h>
#include <WiFi.h>

#include "app_config.h"
#include "automation.h"
#include "ble_srv.h"
#include "espnow_hub.h"
#include "history.h"
#include "ir_ctrl.h"
#include "log.h"
#include "runtime.h"
#include "sensors.h"
#include "telegram.h"
#include "timekeeper.h"
#include "version.h"
#include "web_srv.h"
#include "wifi_mgr.h"

namespace hub::api {
namespace {

constexpr size_t kMaxMsg = 4096;
constexpr int kMaxSessions = 8;
constexpr uint32_t kBroadcastMinMs = 1000;

struct Req {
  Transport t;
  uint32_t client;
  size_t len;
  char* data;  // nullptr = bağlantı koptu
};

struct Session {
  bool used = false;
  Transport t = Transport::kSerial;
  uint32_t client = 0;
  bool authed = false;
  uint8_t fails = 0;
  uint32_t lock_until = 0;
};

struct Ctx {
  Transport t;
  uint32_t client;
  Session* s;
  Source src;
};

using Handler = bool (*)(Ctx& c, JsonObjectConst req, JsonObject out, String& err);

QueueHandle_t s_q = nullptr;
Session s_sessions[kMaxSessions];
uint32_t s_last_bcast = 0;
uint32_t s_reboot_at = 0;
bool s_factory_pending = false;
bool s_scan_pending = false;
Transport s_scan_t;
uint32_t s_scan_client = 0;

// ------------------------------------------------------------- oturumlar
Session* findSession(Transport t, uint32_t client, bool create) {
  for (auto& s : s_sessions)
    if (s.used && s.t == t && s.client == client) return &s;
  if (!create) return nullptr;
  for (auto& s : s_sessions) {
    if (!s.used) {
      s = Session();
      s.used = true;
      s.t = t;
      s.client = client;
      s.authed = (t == Transport::kSerial);  // USB seri: fiziksel erişim = yetkili
      return &s;
    }
  }
  // Doluysa en eski yetkisiz oturumu yeniden kullan
  for (auto& s : s_sessions) {
    if (!s.authed) {
      s = Session();
      s.used = true;
      s.t = t;
      s.client = client;
      return &s;
    }
  }
  return nullptr;
}

void sendTo(Transport t, uint32_t client, const String& msg) {
  switch (t) {
    case Transport::kBle: ble::send(client, msg); break;
    case Transport::kWs: web::send(client, msg); break;
    case Transport::kSerial: Serial.println(msg); break;
  }
}

void sendDoc(Transport t, uint32_t client, JsonDocument& doc) {
  String s;
  serializeJson(doc, s);
  sendTo(t, client, s);
}

// --------------------------------------------------------- yardımcılar
void acStateJson(const AcState& s, JsonObject o) {
  o["power"] = s.power;
  o["mode"] = modeName(s.mode);
  o["temp"] = s.temp;
  o["fan"] = fanName(s.fan);
  o["swing"] = s.swing;
}

void onScanDone(JsonArray results, void*) {
  if (!s_scan_pending) return;
  s_scan_pending = false;
  JsonDocument doc;
  doc["evt"] = "wifi.scan";
  doc["data"]["networks"] = results;
  sendDoc(s_scan_t, s_scan_client, doc);
}

// ----------------------------------------------------------- komutlar
bool hHello(Ctx& c, JsonObjectConst, JsonObject out, String&) {
  out["name"] = g_cfg.name;
  out["fw"] = HUB_FW_VERSION;
  out["api"] = HUB_API_VERSION;
  out["authed"] = c.s && c.s->authed;
  out["time_valid"] = timekeeper::valid();
  out["default_pin"] = g_cfg.pin == "1234";
  return true;
}

bool hAuth(Ctx& c, JsonObjectConst r, JsonObject out, String& err) {
  if (!c.s) {
    err = "oturum yok";
    return false;
  }
  if (c.s->lock_until && int32_t(millis() - c.s->lock_until) < 0) {
    err = "cok fazla hatali deneme, 1 dk bekleyin";
    return false;
  }
  String pin = r["pin"] | "";
  if (pin == g_cfg.pin) {
    c.s->authed = true;
    c.s->fails = 0;
    out["name"] = g_cfg.name;
    return true;
  }
  if (++c.s->fails >= 5) {
    c.s->lock_until = millis() + 60000;
    c.s->fails = 0;
  }
  err = "PIN hatali";
  return false;
}

bool hStatus(Ctx&, JsonObjectConst, JsonObject out, String&) {
  buildStatus(out);
  return true;
}

bool hAcSet(Ctx& c, JsonObjectConst r, JsonObject out, String& err) {
  if (!setAc(r, uint8_t(c.src), err)) return false;
  acStateJson(g_rt.ac.state, out["state"].to<JsonObject>());
  return true;
}

bool hAcToggle(Ctx& c, JsonObjectConst, JsonObject out, String& err) {
  JsonDocument d;
  d["power"] = !g_rt.ac.state.power;
  if (!setAc(d.as<JsonObjectConst>(), uint8_t(c.src), err)) return false;
  acStateJson(g_rt.ac.state, out["state"].to<JsonObject>());
  return true;
}

bool hIrLearn(Ctx&, JsonObjectConst r, JsonObject out, String& err) {
  if (!ir::startLearning(r["name"] | "", r["role"] | "", r["raw"] | false, err)) return false;
  out["timeout_s"] = 30;
  return true;
}

bool hIrLearnCancel(Ctx&, JsonObjectConst, JsonObject, String&) {
  ir::cancelLearning();
  return true;
}

bool hIrCmds(Ctx&, JsonObjectConst, JsonObject out, String&) {
  ir::commandsJson(out["cmds"].to<JsonArray>());
  ir::infoJson(out["ir"].to<JsonObject>());
  return true;
}

bool hIrCmdSend(Ctx& c, JsonObjectConst r, JsonObject, String& err) {
  return ir::sendCommand(r["id"] | -1, c.src, err);
}

bool hIrCmdUpdate(Ctx&, JsonObjectConst r, JsonObject, String& err) {
  return ir::updateCommand(r["id"] | -1, r, err);
}

bool hIrCmdDelete(Ctx&, JsonObjectConst r, JsonObject, String& err) {
  if (ir::deleteCommand(r["id"] | -1)) return true;
  err = "komut bulunamadi";
  return false;
}

bool hIrProtocols(Ctx&, JsonObjectConst, JsonObject out, String&) {
  ir::protocolsJson(out["protocols"].to<JsonArray>());
  return true;
}

bool hIrModels(Ctx&, JsonObjectConst r, JsonObject out, String&) {
  ir::modelsJson(r["protocol"] | "", out["models"].to<JsonArray>());
  return true;
}

bool hIrProtocolSet(Ctx&, JsonObjectConst r, JsonObject out, String& err) {
  if (!ir::setProtocol(r["protocol"] | "", r["model"] | -1, err)) return false;
  ir::infoJson(out["ir"].to<JsonObject>());
  return true;
}

bool hIrModeSet(Ctx&, JsonObjectConst r, JsonObject out, String& err) {
  if (!ir::setRawMode(r["raw"] | false, err)) return false;
  ir::infoJson(out["ir"].to<JsonObject>());
  return true;
}

bool hSensors(Ctx&, JsonObjectConst, JsonObject out, String&) {
  sensors::toJson(out["sensors"].to<JsonArray>());
  out["auto_accept"] = g_cfg.auto_accept_sensors;
  return true;
}

bool hSensorSet(Ctx&, JsonObjectConst r, JsonObject out, String& err) {
  if (!sensors::configure(r["id"] | -1, r, err)) return false;
  sensors::toJson(out["sensors"].to<JsonArray>());
  return true;
}

bool hSensorRemove(Ctx&, JsonObjectConst r, JsonObject, String& err) {
  if (sensors::remove(r["id"] | -1)) return true;
  err = "sensor bulunamadi";
  return false;
}

bool hHistory(Ctx&, JsonObjectConst r, JsonObject out, String& err) {
  if (!history::queryJson(r["src"] | 0, r["hours"] | 24, r["step_min"] | 15, out)) {
    err = timekeeper::valid() ? "gecmis yok" : "saat ayarli degil";
    return false;
  }
  return true;
}

bool hEvents(Ctx&, JsonObjectConst r, JsonObject out, String&) {
  history::eventsJson(out["events"].to<JsonArray>(), r["max"] | 50);
  return true;
}

bool hAutoGet(Ctx&, JsonObjectConst, JsonObject out, String&) {
  config::autoToJson(out);
  automation::statusJson(out["status"].to<JsonObject>());
  return true;
}

bool hAutoSet(Ctx& c, JsonObjectConst r, JsonObject out, String& err) {
  bool ok = config::autoFromJson(r, err);
  config::markDirty();
  automation::kick();
  markStateDirty();
  hAutoGet(c, r, out, err);
  return ok;
}

bool hWifiList(Ctx&, JsonObjectConst, JsonObject out, String&) {
  config::wifiToJson(out["networks"].to<JsonArray>());
  out["state"] = net::stateName();
  out["ssid"] = net::ssid();
  out["ip"] = net::ip();
  out["ap"] = net::apActive();
  out["ap_ssid"] = net::apSsid();
  return true;
}

bool hWifiAdd(Ctx& c, JsonObjectConst r, JsonObject out, String& err) {
  String ssid = r["ssid"] | "";
  String pass = r["pass"] | "";
  if (ssid.isEmpty() || ssid.length() > 32) {
    err = "gecersiz SSID";
    return false;
  }
  if (pass.length() > 0 && pass.length() < 8) {
    err = "WiFi sifresi en az 8 karakter olmali";
    return false;
  }
  net::addNetwork(ssid, pass);
  return hWifiList(c, r, out, err);
}

bool hWifiRemove(Ctx& c, JsonObjectConst r, JsonObject out, String& err) {
  if (!net::removeNetwork(r["ssid"] | "")) {
    err = "ag bulunamadi";
    return false;
  }
  return hWifiList(c, r, out, err);
}

bool hWifiScan(Ctx& c, JsonObjectConst, JsonObject, String& err) {
  if (!net::startScan(onScanDone, nullptr)) {
    err = "tarama su an yapilamiyor, birazdan tekrar deneyin";
    return false;
  }
  s_scan_pending = true;
  s_scan_t = c.t;
  s_scan_client = c.client;
  return true;
}

bool hWifiReconnect(Ctx&, JsonObjectConst, JsonObject, String&) {
  net::reconnectNow();
  return true;
}

bool hTelegramGet(Ctx&, JsonObjectConst, JsonObject out, String&) {
  config::telegramToJson(out);
  telegram::statusJson(out["status"].to<JsonObject>());
  return true;
}

bool hTelegramSet(Ctx& c, JsonObjectConst r, JsonObject out, String& err) {
  bool ok = config::telegramFromJson(r, err);
  config::markDirty();
  telegram::syncConfig();
  hTelegramGet(c, r, out, err);
  return ok;
}

bool hTelegramTest(Ctx&, JsonObjectConst, JsonObject, String& err) {
  if (!g_cfg.tg.enabled || g_cfg.tg.token.isEmpty()) {
    err = "Telegram kapali veya token yok";
    return false;
  }
  if (g_cfg.tg.n_chats == 0) {
    err = "Henuz yetkili sohbet yok: bota '/start PIN' yazin";
    return false;
  }
  telegram::queueTest();
  return true;
}

bool hSettingsGet(Ctx&, JsonObjectConst, JsonObject out, String&) {
  config::settingsToJson(out);
  return true;
}

bool hSettingsSet(Ctx& c, JsonObjectConst r, JsonObject out, String& err) {
  String old_tz = g_cfg.tz;
  bool ok = config::settingsFromJson(r, err);
  if (old_tz != g_cfg.tz) timekeeper::applyTimezone();
  config::markDirty();
  espnow::syncDirectory();
  markStateDirty();
  config::settingsToJson(out);
  return ok;
}

bool hTimeSet(Ctx&, JsonObjectConst r, JsonObject out, String& err) {
  uint32_t ep = r["epoch"] | 0u;
  if (ep < 1735689600u) {
    err = "gecersiz zaman";
    return false;
  }
  timekeeper::setEpoch(ep, "app");
  out["time"] = timekeeper::now();
  return true;
}

bool hSystemInfo(Ctx&, JsonObjectConst, JsonObject out, String&) {
  out["fw"] = HUB_FW_VERSION;
  out["idf"] = esp_get_idf_version();
  out["uptime"] = uptimeS();
  out["heap"] = ESP.getFreeHeap();
  out["heap_min"] = ESP.getMinFreeHeap();
  out["psram"] = ESP.getFreePsram();
  out["reset_reason"] = int(esp_reset_reason());
  out["mac"] = WiFi.macAddress();
  out["fs_used"] = LittleFS.usedBytes();
  out["fs_total"] = LittleFS.totalBytes();
  espnow::Stats st = espnow::stats();
  JsonObject e = out["espnow"].to<JsonObject>();
  e["rx"] = st.rx_frames;
  e["invalid"] = st.rx_invalid;
  e["unknown"] = st.rx_unknown;
  e["acks"] = st.acks_sent;
  e["ack_fail"] = st.ack_fail;
  e["queue_full"] = st.queue_full;
  out["ble_clients"] = ble::clients();
  out["ws_clients"] = web::clients();
  return true;
}

bool hReboot(Ctx&, JsonObjectConst, JsonObject, String&) {
  s_reboot_at = millis() + 800;
  return true;
}

bool hFactoryReset(Ctx&, JsonObjectConst r, JsonObject, String& err) {
  if (!(r["confirm"] | false)) {
    err = "onay gerekli (confirm:true)";
    return false;
  }
  s_factory_pending = true;
  s_reboot_at = millis() + 800;
  return true;
}

bool hAp(Ctx&, JsonObjectConst r, JsonObject out, String&) {
  uint32_t minutes = r["minutes"] | 10u;
  if (minutes == 0) {
    net::stopAp();
  } else {
    net::startAp(minutes);
  }
  out["ap"] = net::apActive();
  out["ssid"] = net::apSsid();
  return true;
}

bool hBleOn(Ctx&, JsonObjectConst r, JsonObject, String&) {
  ble::requestOn(r["minutes"] | 15u);
  return true;
}

struct Cmd {
  const char* name;
  Handler fn;
  bool auth;
};

const Cmd kCmds[] = {
    {"hello", hHello, false},
    {"auth", hAuth, false},
    {"status", hStatus, true},
    {"ac.set", hAcSet, true},
    {"ac.toggle", hAcToggle, true},
    {"ir.learn", hIrLearn, true},
    {"ir.learn.cancel", hIrLearnCancel, true},
    {"ir.cmds", hIrCmds, true},
    {"ir.cmd.send", hIrCmdSend, true},
    {"ir.cmd.update", hIrCmdUpdate, true},
    {"ir.cmd.delete", hIrCmdDelete, true},
    {"ir.protocols", hIrProtocols, true},
    {"ir.models", hIrModels, true},
    {"ir.protocol.set", hIrProtocolSet, true},
    {"ir.mode.set", hIrModeSet, true},
    {"sensors", hSensors, true},
    {"sensor.set", hSensorSet, true},
    {"sensor.remove", hSensorRemove, true},
    {"history", hHistory, true},
    {"events", hEvents, true},
    {"auto.get", hAutoGet, true},
    {"auto.set", hAutoSet, true},
    {"wifi.list", hWifiList, true},
    {"wifi.add", hWifiAdd, true},
    {"wifi.remove", hWifiRemove, true},
    {"wifi.scan", hWifiScan, true},
    {"wifi.reconnect", hWifiReconnect, true},
    {"telegram.get", hTelegramGet, true},
    {"telegram.set", hTelegramSet, true},
    {"telegram.test", hTelegramTest, true},
    {"settings.get", hSettingsGet, true},
    {"settings.set", hSettingsSet, true},
    {"time.set", hTimeSet, true},
    {"system.info", hSystemInfo, true},
    {"system.reboot", hReboot, true},
    {"system.factory_reset", hFactoryReset, true},
    {"system.ap", hAp, true},
    {"ble.on", hBleOn, true},
};

void handle(Req& r) {
  if (!r.data) {  // bağlantı koptu
    Session* s = findSession(r.t, r.client, false);
    if (s) *s = Session();
    return;
  }
  JsonDocument req;
  JsonDocument resp;
  DeserializationError e = deserializeJson(req, r.data, r.len);
  if (e) {
    resp["ok"] = false;
    resp["error"] = String("JSON hatasi: ") + e.c_str();
    sendDoc(r.t, r.client, resp);
    return;
  }
  resp["id"] = req["id"];
  const char* name = req["cmd"] | "";
  Session* s = findSession(r.t, r.client, true);
  Ctx c{r.t, r.client, s, r.t == Transport::kSerial ? Source::kSerial : Source::kApp};

  const Cmd* cmd = nullptr;
  for (const Cmd& x : kCmds) {
    if (strcmp(x.name, name) == 0) {
      cmd = &x;
      break;
    }
  }
  if (!cmd) {
    resp["ok"] = false;
    resp["error"] = String("bilinmeyen komut: ") + name;
  } else if (cmd->auth && !(s && s->authed)) {
    resp["ok"] = false;
    resp["error"] = "auth";
  } else {
    String err;
    JsonObject out = resp["data"].to<JsonObject>();
    bool ok = cmd->fn(c, req.as<JsonObjectConst>(), out, err);
    resp["ok"] = ok;
    if (!ok) resp["error"] = err.length() ? err : String("basarisiz");
  }
  sendDoc(r.t, r.client, resp);
}

}  // namespace

void begin() { s_q = xQueueCreate(16, sizeof(Req)); }

bool enqueue(Transport t, uint32_t client, const char* data, size_t len) {
  if (!s_q) return false;
  Req r{t, client, 0, nullptr};
  if (data) {
    if (len == 0 || len > kMaxMsg) return false;
    r.data = static_cast<char*>(malloc(len + 1));
    if (!r.data) return false;
    memcpy(r.data, data, len);
    r.data[len] = 0;
    r.len = len;
  }
  if (xQueueSend(s_q, &r, 0) != pdTRUE) {
    free(r.data);
    return false;
  }
  return true;
}

void loop() {
  Req r;
  for (int i = 0; i < 4 && xQueueReceive(s_q, &r, 0) == pdTRUE; i++) {
    handle(r);
    free(r.data);
  }

  uint32_t now = millis();
  if (g_rt.state_dirty && now - s_last_bcast >= kBroadcastMinMs) {
    g_rt.state_dirty = false;
    s_last_bcast = now;
    if (sessionCount() > 0) {
      JsonDocument doc;
      buildStatus(doc.to<JsonObject>());
      broadcastEvent("status", doc.as<JsonVariantConst>());
    }
  }

  if (s_reboot_at && int32_t(now - s_reboot_at) >= 0) {
    if (s_factory_pending) {
      LOGW("Fabrika ayarlarina donuluyor");
      LittleFS.format();
    } else {
      config::saveNow();
      history::saveNow();
    }
    ESP.restart();
  }
}

int sessionCount() {
  int n = 0;
  for (auto& s : s_sessions)
    if (s.used && s.authed) n++;
  return n;
}

void broadcastEvent(const char* evt, JsonVariantConst data) {
  JsonDocument doc;
  doc["evt"] = evt;
  doc["data"] = data;
  String msg;
  serializeJson(doc, msg);
  for (auto& s : s_sessions) {
    if (s.used && s.authed) sendTo(s.t, s.client, msg);
  }
}

namespace {
// Açılışta kullanılacak "son ayarlar"ı güncelle
void rememberDefaults(const AcState& s) {
  AcState& d = g_cfg.ac_default;
  if (d.mode != s.mode || d.temp != s.temp || d.fan != s.fan || d.swing != s.swing) {
    d.mode = s.mode;
    d.temp = s.temp;
    d.fan = s.fan;
    d.swing = s.swing;
    d.power = false;
    config::markDirty();
  }
}
}  // namespace

bool setAc(JsonObjectConst r, uint8_t source, String& err) {
  Source src = Source(source);
  const AcState cur = g_rt.ac.state;
  AcState s = cur;
  const bool has_power = r["power"].is<bool>();
  const bool want_power = has_power ? r["power"].as<bool>() : cur.power;
  if (want_power && !cur.power) {
    // Kapalıyken açılıyorsa son kullanılan ayarlarla başla
    s = g_cfg.ac_default;
  }
  s.power = want_power;
  AcMode m;
  if (r["mode"].is<const char*>()) {
    if (!parseMode(r["mode"], m)) {
      err = "gecersiz mod";
      return false;
    }
    s.mode = m;
  }
  if (r["temp"].is<float>()) s.temp = clampAcTemp(r["temp"].as<float>());
  if (r["temp_delta"].is<float>()) s.temp = clampAcTemp(s.temp + r["temp_delta"].as<float>());
  AcFan f;
  if (r["fan"].is<const char*>()) {
    if (!parseFan(r["fan"], f)) {
      err = "gecersiz fan";
      return false;
    }
    s.fan = f;
  }
  if (r["swing"].is<bool>()) s.swing = r["swing"];

  if (!s.power && !cur.power && !has_power) {
    // Klima kapalıyken yalnızca ayar değişti: IR gönderme, açılış ayarı olarak sakla.
    // (Açıkça "kapat" istenirse kumanda ile senkron olmayabileceği için yine gönderilir.)
    rememberDefaults(s);
    g_rt.ac.state = s;
    markStateDirty();
    return true;
  }
  if (!ir::apply(s, src, err)) return false;
  if (s.power) rememberDefaults(s);
  return true;
}

void buildStatus(JsonObject out) {
  JsonObject hubo = out["hub"].to<JsonObject>();
  hubo["name"] = g_cfg.name;
  hubo["fw"] = HUB_FW_VERSION;
  hubo["uptime"] = uptimeS();
  hubo["time"] = timekeeper::now();
  hubo["time_valid"] = timekeeper::valid();
  hubo["time_src"] = timekeeper::lastSource();

  JsonObject w = hubo["wifi"].to<JsonObject>();
  w["state"] = net::stateName();
  w["ssid"] = net::ssid();
  w["configured"] = g_cfg.n_wifi;
  if (net::connected()) {
    w["rssi"] = net::rssi();
    w["ip"] = net::ip();
  }
  w["ch"] = net::channel();
  w["outage_s"] = net::outageSeconds();
  w["reconnects"] = net::reconnectCount();
  w["retry_in"] = net::nextRetryInS();
  w["internet"] = net::internetOk();

  JsonObject ap = hubo["ap"].to<JsonObject>();
  ap["on"] = net::apActive();
  ap["ssid"] = net::apSsid();

  JsonObject b = hubo["ble"].to<JsonObject>();
  b["on"] = ble::active();
  b["clients"] = ble::clients();
  b["always"] = g_cfg.ble_always;

  JsonObject p = hubo["power"].to<JsonObject>();
  p["usb"] = g_rt.hub.usb;
  p["battery"] = g_rt.hub.battery_present;
  p["batt_mv"] = g_rt.hub.batt_mv;
  p["batt_pct"] = g_rt.hub.batt_pct;
  p["low"] = g_rt.hub.battery_critical;

  telegram::statusJson(hubo["telegram"].to<JsonObject>());

  sensors::toJson(out["sensors"].to<JsonArray>());

  JsonObject ac = out["ac"].to<JsonObject>();
  acStateJson(g_rt.ac.state, ac["state"].to<JsonObject>());
  ac["src"] = sourceName(g_rt.ac.last_src);
  ac["changed"] = g_rt.ac.last_change_epoch;
  ir::infoJson(ac["ir"].to<JsonObject>());

  automation::statusJson(out["auto"].to<JsonObject>());
}

}  // namespace hub::api
