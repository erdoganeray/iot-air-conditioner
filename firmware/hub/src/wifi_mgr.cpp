#include "wifi_mgr.h"

#include <DNSServer.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <esp_wifi.h>

#include "app_config.h"
#include "espnow_hub.h"
#include "log.h"
#include "notify.h"

namespace hub::net {
namespace {

constexpr uint32_t kConnectTimeoutMs = 20000;
constexpr uint32_t kBackoffMinMs = 5000;
constexpr uint32_t kBackoffMaxMs = 300000;           // 5 dk
constexpr uint32_t kStackResetAfterMs = 30 * 60000;  // 30 dk kesintide WiFi yığınını yenile
constexpr uint8_t kFastFailLimit = 3;                // kanal+BSSID ile en fazla 3 hızlı deneme
constexpr const char* kHostname = "klima";
constexpr const char* kApPass = "klima1234";

State s_state = State::kNoConfig;
uint32_t s_backoff_ms = kBackoffMinMs;
uint32_t s_next_try_ms = 0;
uint32_t s_attempt_start_ms = 0;
uint32_t s_outage_start_ms = 0;  // 0 = kesinti yok
bool s_ever_connected = false;
uint32_t s_reconnects = 0;
uint32_t s_last_stack_reset_ms = 0;
int s_net_idx = 0;
uint8_t s_fast_fails = 0;
uint8_t s_net_fails = 0;
bool s_internet_ok = false;

bool s_ap_on = false;
uint32_t s_ap_until_ms = 0;  // 0 = süresiz
DNSServer s_dns;
String s_ap_ssid;

volatile bool s_evt_got_ip = false;
volatile bool s_evt_disconnected = false;

bool s_scanning = false;
ScanCallback s_scan_cb = nullptr;
void* s_scan_ctx = nullptr;

bool s_mdns_started = false;

void onWifiEvent(arduino_event_id_t event, arduino_event_info_t) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      s_evt_got_ip = true;
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
    case ARDUINO_EVENT_WIFI_STA_LOST_IP:
      s_evt_disconnected = true;
      break;
    default:
      break;
  }
}

void applyRadioDefaults() {
  WiFi.setHostname(kHostname);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  // ESP-NOW paketlerini kaçırmamak için WiFi güç tasarrufunu kapat.
  if (!WiFi.setSleep(false)) LOGW("WiFi guc tasarrufu kapatilamadi (modem-sleep acik kalacak)");
}

void startMdns() {
  if (s_mdns_started) return;
  if (MDNS.begin(kHostname)) {
    MDNS.addService("http", "tcp", 80);
    s_mdns_started = true;
    LOGI("mDNS: http://%s.local", kHostname);
  }
}

void scheduleRetry(uint32_t now) {
  uint32_t jitter = uint32_t(random(0, long(s_backoff_ms / 4) + 1));
  s_next_try_ms = now + s_backoff_ms + jitter;
  s_backoff_ms = s_backoff_ms * 2 > kBackoffMaxMs ? kBackoffMaxMs : s_backoff_ms * 2;
  s_state = State::kWaitRetry;
  LOGI("WiFi yeniden deneme %lu sn sonra", (unsigned long)((s_next_try_ms - now) / 1000));
}

void tryConnect(uint32_t now) {
  if (g_cfg.n_wifi == 0) {
    s_state = State::kNoConfig;
    return;
  }
  if (s_net_idx >= g_cfg.n_wifi) s_net_idx = 0;
  WifiNet& n = g_cfg.wifi[s_net_idx];
  bool fast = n.channel >= 1 && n.channel <= 13 && n.has_bssid && s_fast_fails < kFastFailLimit;
  LOGI("WiFi baglaniyor: '%s' (%s)", n.ssid.c_str(), fast ? "hizli" : "taramali");
  WiFi.disconnect(false, false);
  if (fast) {
    WiFi.begin(n.ssid.c_str(), n.pass.c_str(), n.channel, n.bssid, true);
  } else {
    WiFi.begin(n.ssid.c_str(), n.pass.c_str());
  }
  s_attempt_start_ms = now;
  s_state = State::kConnecting;
}

void onConnectFailed(uint32_t now) {
  WifiNet& n = g_cfg.wifi[s_net_idx < g_cfg.n_wifi ? s_net_idx : 0];
  bool was_fast = n.has_bssid && n.channel && s_fast_fails < kFastFailLimit;
  if (was_fast) s_fast_fails++;
  s_net_fails++;
  // Bir ağ 2 kez üst üste başarısızsa (ve başka ağ kayıtlıysa) sıradakine geç
  if (s_net_fails >= 2 && g_cfg.n_wifi > 1) {
    s_net_idx = (s_net_idx + 1) % g_cfg.n_wifi;
    s_net_fails = 0;
    s_fast_fails = 0;
  }
  WiFi.disconnect(false, false);
  scheduleRetry(now);
}

void onConnected(uint32_t now) {
  s_state = State::kConnected;
  s_backoff_ms = kBackoffMinMs;
  s_fast_fails = 0;
  s_net_fails = 0;
  uint32_t outage_s = s_outage_start_ms ? (now - s_outage_start_ms) / 1000 : 0;
  s_outage_start_ms = 0;
  if (s_ever_connected) s_reconnects++;
  s_ever_connected = true;

  // Kanal ve BSSID'yi kaydet: sonraki bağlanmada tarama gerekmez
  if (s_net_idx < g_cfg.n_wifi) {
    WifiNet& n = g_cfg.wifi[s_net_idx];
    uint8_t ch = uint8_t(WiFi.channel());
    const uint8_t* b = WiFi.BSSID();
    bool changed = ch != n.channel || !n.has_bssid || (b && memcmp(b, n.bssid, 6) != 0);
    if (changed && b) {
      n.channel = ch;
      memcpy(n.bssid, b, 6);
      n.has_bssid = true;
      config::markDirty();
    }
  }
  LOGI("WiFi baglandi: %s, IP %s, kanal %d, RSSI %d", WiFi.SSID().c_str(),
       WiFi.localIP().toString().c_str(), int(WiFi.channel()), int(WiFi.RSSI()));
  startMdns();
  if (outage_s >= 60) {
    char msg[96];
    snprintf(msg, sizeof(msg), "📶 WiFi bağlantısı geri geldi. Kesinti süresi: %lu dk.",
             (unsigned long)(outage_s / 60));
    notify::send(notify::Kind::kWifi, msg);
  }
}

void onDisconnected(uint32_t now) {
  if (s_state == State::kConnected) {
    LOGW("WiFi baglantisi koptu");
    s_outage_start_ms = now ? now : 1;
    s_internet_ok = false;
    s_backoff_ms = kBackoffMinMs;
    // İlk denemeyi kısa bir bekleme ile yap (anlık kopmalar için)
    s_next_try_ms = now + 2000;
    s_state = State::kWaitRetry;
  }
}

void resetStack(uint32_t now) {
  LOGW("Uzun kesinti: WiFi yigini yeniden baslatiliyor");
  s_last_stack_reset_ms = now;
  espnow::end();
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_OFF);
  delay(200);
  WiFi.mode(s_ap_on ? WIFI_AP_STA : WIFI_STA);
  applyRadioDefaults();
  espnow::begin();
  s_fast_fails = kFastFailLimit;  // bir sonraki deneme taramalı olsun
  s_next_try_ms = now + 1000;
  s_state = State::kWaitRetry;
}

void finishScan() {
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) return;
  s_scanning = false;
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  if (n > 0) {
    for (int i = 0; i < n && i < 20; i++) {
      // Aynı SSID'yi bir kez göster
      String s = WiFi.SSID(i);
      if (s.isEmpty()) continue;
      bool dup = false;
      for (JsonObject o : arr) {
        if (s == o["ssid"].as<const char*>()) {
          dup = true;
          break;
        }
      }
      if (dup) continue;
      JsonObject o = arr.add<JsonObject>();
      o["ssid"] = s;
      o["rssi"] = WiFi.RSSI(i);
      o["ch"] = WiFi.channel(i);
      o["open"] = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
    }
  }
  WiFi.scanDelete();
  if (s_scan_cb) s_scan_cb(arr, s_scan_ctx);
  s_scan_cb = nullptr;
}

}  // namespace

void begin() {
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  WiFi.onEvent(onWifiEvent);
  WiFi.mode(WIFI_STA);
  applyRadioDefaults();
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char b[24];
  snprintf(b, sizeof(b), "KlimaHub-%02X%02X", mac[4], mac[5]);
  s_ap_ssid = b;
  startMdns();
  if (g_cfg.n_wifi == 0) {
    s_state = State::kNoConfig;
    startAp(0);
  } else {
    s_state = State::kWaitRetry;
    s_next_try_ms = millis();
    s_outage_start_ms = millis() ? millis() : 1;
  }
}

void loop() {
  uint32_t now = millis();

  if (s_evt_disconnected) {
    s_evt_disconnected = false;
    if (s_state == State::kConnected) onDisconnected(now);
  }
  if (s_evt_got_ip) {
    s_evt_got_ip = false;
    if (s_state == State::kConnecting || s_state == State::kWaitRetry) onConnected(now);
  }

  switch (s_state) {
    case State::kNoConfig:
      if (g_cfg.n_wifi > 0) {
        s_state = State::kWaitRetry;
        s_next_try_ms = now;
      }
      break;
    case State::kWaitRetry:
      if (!s_scanning && int32_t(now - s_next_try_ms) >= 0) tryConnect(now);
      break;
    case State::kConnecting:
      if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
        onConnected(now);
      } else if (now - s_attempt_start_ms > kConnectTimeoutMs) {
        LOGW("WiFi baglanti zaman asimi");
        onConnectFailed(now);
      }
      break;
    case State::kConnected:
      if (!WiFi.isConnected()) onDisconnected(now);
      break;
  }

  if (s_state != State::kConnected && s_state != State::kNoConfig && s_outage_start_ms &&
      now - s_outage_start_ms > kStackResetAfterMs &&
      (s_last_stack_reset_ms == 0 || now - s_last_stack_reset_ms > kStackResetAfterMs)) {
    resetStack(now);
  }

  if (s_ap_on) {
    s_dns.processNextRequest();
    if (s_ap_until_ms && int32_t(now - s_ap_until_ms) >= 0 && g_cfg.n_wifi > 0) stopAp();
  } else if (g_cfg.n_wifi == 0) {
    startAp(0);
  }

  if (s_scanning) finishScan();
}

bool connected() { return s_state == State::kConnected; }
State state() { return s_state; }

const char* stateName() {
  switch (s_state) {
    case State::kNoConfig: return "no_config";
    case State::kWaitRetry: return "retry_wait";
    case State::kConnecting: return "connecting";
    case State::kConnected: return "connected";
  }
  return "?";
}

String ssid() {
  if (connected()) return WiFi.SSID();
  if (g_cfg.n_wifi > 0 && s_net_idx < g_cfg.n_wifi) return g_cfg.wifi[s_net_idx].ssid;
  return String();
}

int rssi() { return connected() ? WiFi.RSSI() : 0; }
String ip() { return connected() ? WiFi.localIP().toString() : String(); }

uint8_t channel() {
  uint8_t primary = 1;
  wifi_second_chan_t second;
  if (esp_wifi_get_channel(&primary, &second) != ESP_OK) primary = 1;
  return primary;
}

uint32_t outageSeconds() {
  if (connected() || !s_outage_start_ms) return 0;
  return (millis() - s_outage_start_ms) / 1000;
}

uint32_t reconnectCount() { return s_reconnects; }

uint32_t nextRetryInS() {
  if (s_state != State::kWaitRetry) return 0;
  int32_t d = int32_t(s_next_try_ms - millis());
  return d > 0 ? uint32_t(d) / 1000 : 0;
}

void addNetwork(const String& ssid, const String& pass) {
  // Varsa çıkar, sonra en başa ekle
  removeNetwork(ssid);
  for (int i = (g_cfg.n_wifi < kMaxWifi ? g_cfg.n_wifi : kMaxWifi - 1); i > 0; i--) {
    g_cfg.wifi[i] = g_cfg.wifi[i - 1];
  }
  g_cfg.wifi[0] = WifiNet();
  g_cfg.wifi[0].ssid = ssid;
  g_cfg.wifi[0].pass = pass;
  if (g_cfg.n_wifi < kMaxWifi) g_cfg.n_wifi++;
  config::markDirty();
  s_net_idx = 0;
  // Süresiz açık kurulum ağı varsa 5 dk sonra kapansın (kullanıcı sonucu görebilsin)
  if (s_ap_on && s_ap_until_ms == 0) s_ap_until_ms = millis() + 5 * 60000UL;
  reconnectNow();
}

bool removeNetwork(const String& ssid) {
  for (int i = 0; i < g_cfg.n_wifi; i++) {
    if (g_cfg.wifi[i].ssid == ssid) {
      for (int j = i; j < g_cfg.n_wifi - 1; j++) g_cfg.wifi[j] = g_cfg.wifi[j + 1];
      g_cfg.n_wifi--;
      g_cfg.wifi[g_cfg.n_wifi] = WifiNet();
      config::markDirty();
      return true;
    }
  }
  return false;
}

void reconnectNow() {
  s_backoff_ms = kBackoffMinMs;
  s_fast_fails = 0;
  s_net_fails = 0;
  if (s_state == State::kConnected) {
    WiFi.disconnect(false, false);
    s_outage_start_ms = millis() ? millis() : 1;
  }
  s_state = g_cfg.n_wifi ? State::kWaitRetry : State::kNoConfig;
  s_next_try_ms = millis();
}

void startAp(uint32_t minutes) {
  s_ap_until_ms = minutes ? millis() + minutes * 60000UL : 0;
  if (s_ap_on) return;
  WiFi.mode(WIFI_AP_STA);
  applyRadioDefaults();
  // AP kanalı: STA bağlıysa zorunlu olarak onunla aynıdır
  uint8_t ch = connected() ? uint8_t(WiFi.channel()) : 1;
  WiFi.softAP(s_ap_ssid.c_str(), kApPass, ch);
  s_dns.setErrorReplyCode(DNSReplyCode::NoError);
  s_dns.start(53, "*", WiFi.softAPIP());
  s_ap_on = true;
  LOGI("Kurulum agi acildi: %s (sifre %s) http://%s", s_ap_ssid.c_str(), kApPass,
       WiFi.softAPIP().toString().c_str());
}

void stopAp() {
  if (!s_ap_on) return;
  s_dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  applyRadioDefaults();
  s_ap_on = false;
  LOGI("Kurulum agi kapatildi");
}

bool apActive() { return s_ap_on; }
String apSsid() { return s_ap_ssid; }

bool startScan(ScanCallback cb, void* ctx) {
  if (s_scanning || s_state == State::kConnecting) return false;
  if (WiFi.scanNetworks(true, false) == WIFI_SCAN_FAILED) return false;
  s_scanning = true;
  s_scan_cb = cb;
  s_scan_ctx = ctx;
  return true;
}

void setInternetOk(bool ok) { s_internet_ok = ok; }
bool internetOk() { return connected() && s_internet_ok; }

}  // namespace hub::net
