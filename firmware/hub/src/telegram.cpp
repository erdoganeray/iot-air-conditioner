#include "telegram.h"

#include <HTTPClient.h>
#include <NetworkClientSecure.h>

#include "api.h"
#include "app_config.h"
#include "automation.h"
#include "log.h"
#include "runtime.h"
#include "sensors.h"
#include "telegram_ca.h"
#include "timekeeper.h"
#include "wifi_mgr.h"

namespace hub::telegram {
namespace {

constexpr const char* kHost = "api.telegram.org";
constexpr int kPollTimeoutS = 20;
constexpr uint32_t kReplyWaitMs = 2500;
constexpr int kOutQueue = 24;

struct OutMsg {
  int64_t chat;
  char* text;
  char* markup;   // inline klavye JSON'u (isteğe bağlı)
  char* cbq_id;   // yanıtlanacak callback_query (isteğe bağlı)
  uint32_t created_ms;
  uint32_t created_epoch;
};

struct InMsg {
  int64_t chat;
  char* text;     // mesaj metni veya callback verisi
  char* cbq_id;   // callback ise dolu
  char* from;
};

QueueHandle_t s_out_q = nullptr;
QueueHandle_t s_in_q = nullptr;
SemaphoreHandle_t s_cfg_lock = nullptr;
char s_token[96] = {0};
bool s_enabled = false;
bool s_insecure = false;
volatile bool s_ok = false;
volatile uint32_t s_last_ok_ms = 0;
char s_last_error[64] = "";
int64_t s_offset = 0;

struct FailInfo {
  int64_t chat;
  uint8_t fails;
  uint32_t until;
};
FailInfo s_fail[4] = {};

char* dupStr(const char* s) {
  if (!s) return nullptr;
  size_t n = strlen(s);
  char* d = static_cast<char*>(malloc(n + 1));
  if (d) memcpy(d, s, n + 1);
  return d;
}

void setError(const char* e) {
  strncpy(s_last_error, e, sizeof(s_last_error) - 1);
  s_last_error[sizeof(s_last_error) - 1] = 0;
}

void freeOut(OutMsg& m) {
  free(m.text);
  free(m.markup);
  free(m.cbq_id);
}

void enqueueOut(int64_t chat, const String& text, const char* markup = nullptr,
                const char* cbq = nullptr) {
  if (!s_out_q) return;
  OutMsg m{chat, dupStr(text.c_str()), dupStr(markup), dupStr(cbq), millis(), timekeeper::now()};
  if (xQueueSend(s_out_q, &m, 0) != pdTRUE) {
    // Kuyruk dolu (uzun internet kesintisi): en eski mesajı at
    OutMsg old;
    if (xQueueReceive(s_out_q, &old, 0) == pdTRUE) freeOut(old);
    if (xQueueSend(s_out_q, &m, 0) != pdTRUE) freeOut(m);
  }
}

// ----------------------------------------------------------- HTTP (görev)
bool request(NetworkClientSecure& client, const String& path, const String* body, String& resp) {
  HTTPClient http;
  http.setReuse(true);
  http.setTimeout((kPollTimeoutS + 10) * 1000);
  http.setConnectTimeout(10000);
  if (!http.begin(client, kHost, 443, path, true)) {
    setError("baglanti kurulamadi");
    return false;
  }
  int code;
  if (body) {
    http.addHeader("Content-Type", "application/json");
    code = http.POST(*body);
  } else {
    code = http.GET();
  }
  if (code <= 0) {
    setError(http.errorToString(code).c_str());
    http.end();
    return false;
  }
  resp = http.getString();
  http.end();
  if (code != 200) {
    char e[64];
    snprintf(e, sizeof(e), "HTTP %d", code);
    setError(code == 401 ? "token gecersiz (HTTP 401)" : e);
    return false;
  }
  return true;
}

bool sendOut(NetworkClientSecure& client, const char* token, OutMsg& m) {
  String resp;
  if (m.cbq_id) {
    JsonDocument d;
    d["callback_query_id"] = m.cbq_id;
    String b;
    serializeJson(d, b);
    request(client, String("/bot") + token + "/answerCallbackQuery", &b, resp);
  }
  if (!m.text || !m.text[0]) return true;
  JsonDocument d;
  d["chat_id"] = m.chat;
  String text = m.text;
  if (millis() - m.created_ms > 120000 && m.created_epoch) {
    time_t t = m.created_epoch;
    struct tm lt;
    localtime_r(&t, &lt);
    char pre[32];
    snprintf(pre, sizeof(pre), "[gecikmeli %02d:%02d] ", lt.tm_hour, lt.tm_min);
    text = String(pre) + text;
  }
  d["text"] = text;
  d["disable_web_page_preview"] = true;
  if (m.markup) d["reply_markup"] = serialized(m.markup);
  String b;
  serializeJson(d, b);
  return request(client, String("/bot") + token + "/sendMessage", &b, resp);
}

// Kuyruktaki mesajları gönderir. false: ağ hatası (mesaj kuyruğa geri kondu)
bool drainOut(NetworkClientSecure& client, const char* token) {
  OutMsg m;
  while (xQueuePeek(s_out_q, &m, 0) == pdTRUE) {
    if (!sendOut(client, token, m)) {
      // 4xx (ör. sohbet silinmiş) ise mesajı at, ağ hatasıysa sonra tekrar dene
      bool net_err = strncmp(s_last_error, "HTTP 4", 6) != 0;
      if (net_err) return false;
    }
    if (xQueueReceive(s_out_q, &m, 0) == pdTRUE) freeOut(m);
  }
  return true;
}

void parseUpdates(const String& body) {
  JsonDocument filter;
  JsonObject f = filter["result"][0].to<JsonObject>();
  f["update_id"] = true;
  f["message"]["chat"]["id"] = true;
  f["message"]["text"] = true;
  f["message"]["from"]["first_name"] = true;
  f["callback_query"]["id"] = true;
  f["callback_query"]["data"] = true;
  f["callback_query"]["from"]["first_name"] = true;
  f["callback_query"]["message"]["chat"]["id"] = true;
  JsonDocument doc;
  if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) return;
  for (JsonObjectConst u : doc["result"].as<JsonArrayConst>()) {
    int64_t uid = u["update_id"].as<int64_t>();
    if (uid >= s_offset) s_offset = uid + 1;
    InMsg in{};
    if (!u["message"].isNull()) {
      in.chat = u["message"]["chat"]["id"].as<int64_t>();
      in.text = dupStr(u["message"]["text"] | "");
      in.from = dupStr(u["message"]["from"]["first_name"] | "");
    } else if (!u["callback_query"].isNull()) {
      in.chat = u["callback_query"]["message"]["chat"]["id"].as<int64_t>();
      in.text = dupStr(u["callback_query"]["data"] | "");
      in.cbq_id = dupStr(u["callback_query"]["id"] | "");
      in.from = dupStr(u["callback_query"]["from"]["first_name"] | "");
    } else {
      continue;
    }
    if (xQueueSend(s_in_q, &in, 0) != pdTRUE) {
      free(in.text);
      free(in.cbq_id);
      free(in.from);
    }
  }
}

void task(void*) {
  NetworkClientSecure* client = nullptr;
  uint32_t backoff_ms = 5000;
  char token[96];
  bool insecure = false;
  for (;;) {
    xSemaphoreTake(s_cfg_lock, portMAX_DELAY);
    bool enabled = s_enabled && s_token[0];
    memcpy(token, s_token, sizeof(token));
    bool want_insecure = s_insecure;
    xSemaphoreGive(s_cfg_lock);

    if (!enabled || !net::connected()) {
      s_ok = false;
      if (client) {
        delete client;
        client = nullptr;
      }
      vTaskDelay(pdMS_TO_TICKS(2000));
      continue;
    }
    if (client && insecure != want_insecure) {
      delete client;
      client = nullptr;
    }
    if (!client) {
      client = new NetworkClientSecure();
      insecure = want_insecure;
      if (insecure) {
        client->setInsecure();
      } else {
        client->setCACert(kTelegramRootCa);
      }
      client->setHandshakeTimeout(30);
    }

    bool ok = drainOut(*client, token);
    String body;
    if (ok) {
      char q[160];
      snprintf(q, sizeof(q),
               "/getUpdates?timeout=%d&limit=5&offset=%lld"
               "&allowed_updates=%%5B%%22message%%22%%2C%%22callback_query%%22%%5D",
               kPollTimeoutS, (long long)s_offset);
      String path = String("/bot") + token + q;
      ok = request(*client, path, nullptr, body);
    }
    if (!ok) {
      s_ok = false;
      LOGW("Telegram hatasi: %s (%lu sn sonra)", s_last_error, (unsigned long)(backoff_ms / 1000));
      delete client;
      client = nullptr;
      vTaskDelay(pdMS_TO_TICKS(backoff_ms));
      backoff_ms = backoff_ms * 2 > 300000 ? 300000 : backoff_ms * 2;
      continue;
    }
    backoff_ms = 5000;
    s_ok = true;
    s_last_ok_ms = millis();
    s_last_error[0] = 0;
    net::setInternetOk(true);
    UBaseType_t before = uxQueueMessagesWaiting(s_in_q);
    parseUpdates(body);
    if (uxQueueMessagesWaiting(s_in_q) > before) {
      // Ana döngünün yanıt üretmesi için kısa süre bekle, sonra hemen gönder
      uint32_t t0 = millis();
      while (millis() - t0 < kReplyWaitMs && uxQueueMessagesWaiting(s_out_q) == 0) {
        vTaskDelay(pdMS_TO_TICKS(50));
      }
      vTaskDelay(pdMS_TO_TICKS(100));
      drainOut(*client, token);
    }
  }
}

// --------------------------------------------------- komutlar (ana döngü)
const char* modeTr(AcMode m) {
  switch (m) {
    case AcMode::kAuto: return "Otomatik";
    case AcMode::kCool: return "Soğutma";
    case AcMode::kHeat: return "Isıtma";
    case AcMode::kDry: return "Kurutma";
    case AcMode::kFan: return "Fan";
  }
  return "?";
}

const char* fanTr(AcFan f) {
  switch (f) {
    case AcFan::kAuto: return "otomatik";
    case AcFan::kMin: return "en düşük";
    case AcFan::kLow: return "düşük";
    case AcFan::kMedium: return "orta";
    case AcFan::kHigh: return "yüksek";
    case AcFan::kMax: return "en yüksek";
  }
  return "?";
}

const char* kMenu =
    "{\"inline_keyboard\":["
    "[{\"text\":\"❄️ Aç\",\"callback_data\":\"on\"},{\"text\":\"⏻ Kapat\",\"callback_data\":\"off\"}],"
    "[{\"text\":\"➖ 1°C\",\"callback_data\":\"tdown\"},{\"text\":\"➕ 1°C\",\"callback_data\":\"tup\"}],"
    "[{\"text\":\"❄️ Soğutma\",\"callback_data\":\"m:cool\"},{\"text\":\"💧 Kurutma\",\"callback_data\":\"m:dry\"},"
    "{\"text\":\"🔥 Isıtma\",\"callback_data\":\"m:heat\"}],"
    "[{\"text\":\"🤖 Termostat aç/kapa\",\"callback_data\":\"th\"},{\"text\":\"💧 Nem oto. aç/kapa\",\"callback_data\":\"dry\"}],"
    "[{\"text\":\"📊 Durum\",\"callback_data\":\"status\"}]]}";

String statusText() {
  String s;
  s.reserve(600);
  s += "🏠 " + g_cfg.name + "\n";
  const AcState& a = g_rt.ac.state;
  char b[160];
  if (a.power) {
    snprintf(b, sizeof(b), "❄️ Klima: AÇIK — %s %.1f°C, fan %s%s\n", modeTr(a.mode), a.temp,
             fanTr(a.fan), a.swing ? ", salınım" : "");
  } else {
    snprintf(b, sizeof(b), "⏻ Klima: KAPALI (son ayar: %s %.1f°C)\n", modeTr(g_cfg.ac_default.mode),
             g_cfg.ac_default.temp);
  }
  s += b;
  if (g_rt.ac.last_change_epoch) {
    time_t t = g_rt.ac.last_change_epoch;
    struct tm lt;
    localtime_r(&t, &lt);
    snprintf(b, sizeof(b), "   son değişiklik: %s, %02d:%02d\n", sourceName(g_rt.ac.last_src), lt.tm_hour,
             lt.tm_min);
    s += b;
  }
  if (g_rt.hub.sensor_ok) {
    snprintf(b, sizeof(b), "🌡️ %s: %.1f°C  💧%%%.0f\n", g_cfg.hub_sensor.name.c_str(), g_rt.hub.temp,
             g_rt.hub.hum);
    s += b;
  }
  for (int i = 0; i < kMaxSensors; i++) {
    const SensorCfg& c = g_cfg.sensors[i];
    if (!c.used) continue;
    const SensorRuntime& r = g_rt.sensors[i];
    if (r.online && r.has_reading) {
      snprintf(b, sizeof(b), "🌡️ %s: %.1f°C  💧%%%.0f  🔋%%%u%s\n", c.name.c_str(), r.temp, r.hum,
               r.batt_pct, (r.flags & 1) ? " (şarjda)" : "");
    } else {
      snprintf(b, sizeof(b), "🌡️ %s: — çevrimdışı%s\n", c.name.c_str(),
               r.seen ? "" : " (henüz veri yok)");
    }
    s += b;
  }
  if (g_cfg.thermo.enabled) {
    snprintf(b, sizeof(b), "🤖 Termostat: AÇIK, %s hedef %.1f°C (%s)\n", modeTr(g_cfg.thermo.mode),
             g_cfg.thermo.target, sensors::name(g_cfg.thermo.source).c_str());
  } else {
    snprintf(b, sizeof(b), "🤖 Termostat: kapalı\n");
  }
  s += b;
  if (g_cfg.dry.enabled) {
    snprintf(b, sizeof(b), "💧 Nem otomasyonu: AÇIK (%%%.0f üstünde kurutma)\n", g_cfg.dry.on_pct);
  } else {
    snprintf(b, sizeof(b), "💧 Nem otomasyonu: kapalı\n");
  }
  s += b;
  snprintf(b, sizeof(b), "📶 WiFi: %s (%d dBm) | %s | pil %%%u", net::ssid().c_str(), net::rssi(),
           g_rt.hub.usb ? "🔌 adaptör" : "🔋 pilde", g_rt.hub.batt_pct);
  s += b;
  return s;
}

String helpText() {
  return F(
      "Komutlar:\n"
      "/durum — genel durum\n"
      "/ac [derece] [mod] — klimayı aç (ör. /ac 24 sogutma)\n"
      "/kapat — klimayı kapat\n"
      "/sicaklik 24 — dereceyi ayarla\n"
      "/mod sogutma | isitma | kuru | fan | otomatik\n"
      "/fan otomatik | dusuk | orta | yuksek\n"
      "/termostat ac | kapat | 25 (hedef)\n"
      "/nem ac | kapat | 65 (eşik)\n"
      "/sensorler — sensör ve pil bilgisi\n"
      "/menu — butonlu menü");
}

String sensorsText() {
  String s = "📡 Sensörler:\n";
  char b[160];
  for (int i = 0; i < kMaxSensors; i++) {
    const SensorCfg& c = g_cfg.sensors[i];
    if (!c.used) continue;
    const SensorRuntime& r = g_rt.sensors[i];
    uint32_t ago = r.seen ? (millis() - r.last_seen_ms) / 60000 : 0;
    snprintf(b, sizeof(b), "• %s: %s, pil %%%u (%.2f V), aralık %u dk, son veri %s%lu dk önce, sinyal %d dBm\n",
             c.name.c_str(), r.online ? "çevrimiçi" : "ÇEVRİMDIŞI", r.batt_pct, r.batt_mv / 1000.0f,
             c.interval_s / 60, r.seen ? "" : "— (", (unsigned long)ago, r.rssi);
    s += b;
  }
  if (s.length() < 20) s += "Kayıtlı sensör yok.";
  return s;
}

bool chatAuthorized(int64_t chat) {
  for (int i = 0; i < g_cfg.tg.n_chats; i++)
    if (g_cfg.tg.chats[i] == chat) return true;
  return false;
}

FailInfo* failSlot(int64_t chat) {
  FailInfo* oldest = &s_fail[0];
  for (auto& f : s_fail) {
    if (f.chat == chat) return &f;
    if (f.until < oldest->until) oldest = &f;
  }
  *oldest = FailInfo{chat, 0, 0};
  return oldest;
}

String applyAc(JsonDocument& d, const char* ok_prefix) {
  String err;
  if (!api::setAc(d.as<JsonObjectConst>(), uint8_t(Source::kTelegram), err)) return "❗ " + err;
  const AcState& a = g_rt.ac.state;
  char b[120];
  if (a.power) {
    snprintf(b, sizeof(b), "%s Klima açık: %s %.1f°C, fan %s", ok_prefix, modeTr(a.mode), a.temp, fanTr(a.fan));
  } else {
    snprintf(b, sizeof(b), "%s Klima kapalı.", ok_prefix);
  }
  return b;
}

void handle(InMsg& in) {
  String text = in.text ? in.text : "";
  text.trim();
  const bool is_cb = in.cbq_id != nullptr;
  const char* cbq = in.cbq_id;

  // Yetki
  if (!chatAuthorized(in.chat)) {
    FailInfo* f = failSlot(in.chat);
    if (f->until && int32_t(millis() - f->until) < 0) return;  // geçici olarak yok say
    if (text.startsWith("/start")) {
      String pin = text.substring(6);
      pin.trim();
      if (pin.length() && pin == g_cfg.pin) {
        if (g_cfg.tg.n_chats < kMaxTelegramChats) {
          g_cfg.tg.chats[g_cfg.tg.n_chats++] = in.chat;
          config::markDirty();
          enqueueOut(in.chat, String("✅ Merhaba ") + (in.from ? in.from : "") +
                                  "! Bu sohbet yetkilendirildi.\n\n" + helpText(),
                     kMenu, cbq);
        } else {
          enqueueOut(in.chat, "❗ En fazla 5 sohbet yetkilendirilebilir.", nullptr, cbq);
        }
        return;
      }
      if (++f->fails >= 5) {
        f->until = millis() + 10 * 60000UL;
        f->fails = 0;
      }
    }
    enqueueOut(in.chat, "🔒 Yetkisiz. Hub PIN'iniz ile şunu yazın:  /start PIN", nullptr, cbq);
    return;
  }

  // Komut ve argümanlar
  String cmd = text, arg;
  int sp = text.indexOf(' ');
  if (sp > 0) {
    cmd = text.substring(0, sp);
    arg = text.substring(sp + 1);
    arg.trim();
  }
  int at = cmd.indexOf('@');  // /durum@BotAdi
  if (at > 0) cmd = cmd.substring(0, at);
  cmd.toLowerCase();
  arg.toLowerCase();

  JsonDocument d;
  String reply;
  const char* markup = nullptr;

  if (cmd == "/start" || cmd == "/menu" || cmd == "menu") {
    reply = statusText();
    markup = kMenu;
  } else if (cmd == "/yardim" || cmd == "/help") {
    reply = helpText();
  } else if (cmd == "/durum" || cmd == "status") {
    reply = statusText();
    if (is_cb) markup = kMenu;
  } else if (cmd == "/sensorler") {
    reply = sensorsText();
  } else if (cmd == "/ac" || cmd == "on") {
    d["power"] = true;
    // "/ac 24 sogutma" veya "/ac sogutma 24"
    int s2 = arg.indexOf(' ');
    String a1 = s2 > 0 ? arg.substring(0, s2) : arg;
    String a2 = s2 > 0 ? arg.substring(s2 + 1) : "";
    for (const String& a : {a1, a2}) {
      if (a.isEmpty()) continue;
      if (isDigit(a[0])) {
        d["temp"] = a.toFloat();
      } else {
        d["mode"] = a;
      }
    }
    reply = applyAc(d, "✅");
  } else if (cmd == "/kapat" || cmd == "off") {
    d["power"] = false;
    reply = applyAc(d, "✅");
  } else if (cmd == "/sicaklik" || cmd == "/derece") {
    if (arg.isEmpty()) {
      reply = "Kullanım: /sicaklik 24";
    } else {
      d["temp"] = arg.toFloat();
      reply = applyAc(d, "✅");
    }
  } else if (cmd == "tup" || cmd == "tdown") {
    d["temp_delta"] = cmd == "tup" ? 1 : -1;
    reply = applyAc(d, "✅");
  } else if (cmd == "/mod" || cmd.startsWith("m:")) {
    String m = cmd.startsWith("m:") ? cmd.substring(2) : arg;
    AcMode mm;
    if (!parseMode(m.c_str(), mm)) {
      reply = "Kullanım: /mod sogutma | isitma | kuru | fan | otomatik";
    } else {
      d["mode"] = modeName(mm);
      d["power"] = true;
      reply = applyAc(d, "✅");
    }
  } else if (cmd == "/fan") {
    AcFan ff;
    if (!parseFan(arg.c_str(), ff)) {
      reply = "Kullanım: /fan otomatik | dusuk | orta | yuksek";
    } else {
      d["fan"] = fanName(ff);
      reply = applyAc(d, "✅");
    }
  } else if (cmd == "/termostat" || cmd == "th") {
    if (cmd == "th") arg = g_cfg.thermo.enabled ? "kapat" : "ac";
    if (arg == "ac" || arg == "aç") {
      g_cfg.thermo.enabled = true;
    } else if (arg == "kapat") {
      g_cfg.thermo.enabled = false;
    } else if (arg.length() && isDigit(arg[0])) {
      g_cfg.thermo.target = constrain(arg.toFloat(), 10.0f, 35.0f);
      g_cfg.thermo.enabled = true;
    }
    config::markDirty();
    automation::kick();
    markStateDirty();
    char b[120];
    snprintf(b, sizeof(b), "🤖 Termostat %s. Hedef %.1f°C (%s), mod %s.",
             g_cfg.thermo.enabled ? "AÇIK" : "kapalı", g_cfg.thermo.target,
             sensors::name(g_cfg.thermo.source).c_str(), modeTr(g_cfg.thermo.mode));
    reply = b;
  } else if (cmd == "/nem" || cmd == "dry") {
    if (cmd == "dry") arg = g_cfg.dry.enabled ? "kapat" : "ac";
    if (arg == "ac" || arg == "aç") {
      g_cfg.dry.enabled = true;
    } else if (arg == "kapat") {
      g_cfg.dry.enabled = false;
    } else if (arg.length() && isDigit(arg[0])) {
      float on = constrain(arg.toFloat(), 30.0f, 95.0f);
      g_cfg.dry.on_pct = on;
      if (g_cfg.dry.off_pct > on - 3) g_cfg.dry.off_pct = on - 10;
      g_cfg.dry.enabled = true;
    }
    config::markDirty();
    automation::kick();
    markStateDirty();
    char b[140];
    snprintf(b, sizeof(b), "💧 Nem otomasyonu %s. Nem %%%.0f üstünde kurutma, %%%.0f altında kapanır (%s).",
             g_cfg.dry.enabled ? "AÇIK" : "kapalı", g_cfg.dry.on_pct, g_cfg.dry.off_pct,
             sensors::name(g_cfg.dry.source).c_str());
    reply = b;
  } else {
    reply = "Anlaşılamadı. /yardim yazın.";
  }
  enqueueOut(in.chat, reply, markup, cbq);
}

}  // namespace

void begin() {
  s_out_q = xQueueCreate(kOutQueue, sizeof(OutMsg));
  s_in_q = xQueueCreate(8, sizeof(InMsg));
  s_cfg_lock = xSemaphoreCreateMutex();
  syncConfig();
  xTaskCreatePinnedToCore(task, "telegram", 12288, nullptr, 1, nullptr, 0);
}

void syncConfig() {
  xSemaphoreTake(s_cfg_lock, portMAX_DELAY);
  strncpy(s_token, g_cfg.tg.token.c_str(), sizeof(s_token) - 1);
  s_token[sizeof(s_token) - 1] = 0;
  s_enabled = g_cfg.tg.enabled;
  s_insecure = g_cfg.tg.insecure;
  xSemaphoreGive(s_cfg_lock);
}

void loop() {
  InMsg in;
  if (s_in_q && xQueueReceive(s_in_q, &in, 0) == pdTRUE) {
    handle(in);
    free(in.text);
    free(in.cbq_id);
    free(in.from);
  }
}

void queueBroadcast(const String& text) {
  for (int i = 0; i < g_cfg.tg.n_chats; i++) enqueueOut(g_cfg.tg.chats[i], text);
}

void queueTest() { queueBroadcast("✅ Test mesajı: KlimaHub Telegram bağlantısı çalışıyor.\n\n" + statusText()); }

void statusJson(JsonObject o) {
  o["enabled"] = g_cfg.tg.enabled;
  o["ok"] = s_ok && millis() - s_last_ok_ms < 120000;
  o["error"] = s_last_error;
  o["chats"] = g_cfg.tg.n_chats;
  o["queued"] = s_out_q ? uxQueueMessagesWaiting(s_out_q) : 0;
}

}  // namespace hub::telegram
