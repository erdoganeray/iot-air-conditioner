// Klima sistemi — Sıcaklık/nem sensörü (Seeed XIAO ESP32-C3)
//
// Çalışma döngüsü (her uyanışta):
//   1. SHT31'i GPIO'dan besle, ölç, beslemeyi kes
//   2. Pil voltajını ve USB durumunu oku
//   3. Ölçümü RTC belleğindeki tampona ekle
//   4. ESP-NOW ile hub'a gönder, ACK bekle (gerekirse kanal tara)
//   5. ACK ile gelen ayarları uygula (aralık, saat, kanal)
//   6. Derin uyku
//
// Ayrıntılar: docs/MIMARI.md

#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <Wire.h>
#include <driver/gpio.h>
#include <esp_idf_version.h>
#include <esp_now.h>
#include <esp_sleep.h>
#include <esp_wifi.h>
#include <sys/time.h>

#include "battery.h"
#include "klima_protocol.h"
#include "sht31.h"

// ---------------------------------------------------------------------------
// Sürüm ve pinler (docs/MIMARI.md §4.1)
// ---------------------------------------------------------------------------
constexpr uint8_t kFwMajor = 1;
constexpr uint8_t kFwMinor = 0;

constexpr int kPinBattAdc = 3;    // D1 — pil bölücü (470k/470k)
constexpr int kPinSensorPwr = 4;  // D2 — SHT31 beslemesi
constexpr int kPinUsbSense = 5;   // D3 — USB var mı (100k/150k)
constexpr int kPinSda = 6;        // D4
constexpr int kPinScl = 7;        // D5

// Pil bölücü oranı ve kalibrasyon katsayısı. Multimetreyle ölçüp karşılaştırarak
// gerekirse platformio.ini içinden -DBATT_CAL=1.02 gibi düzeltin.
#ifndef BATT_CAL
#define BATT_CAL 1.0f
#endif
constexpr float kBattDividerRatio = 2.0f;

// Zamanlamalar
constexpr uint32_t kBurstWindowMs = 400;      // bilinen kanalda tekrar gönderim penceresi
constexpr uint32_t kBurstShortMs = 120;       // hub uzun süredir yoksa kısaltılmış pencere
constexpr uint32_t kBroadcastWindowMs = 120;  // yayın adresiyle ek deneme
constexpr uint32_t kScanAckTimeoutMs = 40;    // tarama sırasında kanal başına
constexpr uint8_t kScanEveryNthFailure = 6;   // ardışık hatada her 6 uyanışta bir tara
constexpr uint32_t kCriticalSleepS = 6 * 3600;
constexpr int kMaxPacketsPerWake = 3;

// ---------------------------------------------------------------------------
// RTC belleği (derin uykuda korunur, pil çıkınca silinir)
// ---------------------------------------------------------------------------
constexpr uint32_t kRtcMagic = 0x53454E31;  // "SEN1"
constexpr int kBufCap = 64;

struct StoredReading {
  uint32_t t;        // sensörün iç saatine göre saniye (time())
  int16_t temp_c100;
  uint16_t hum_c100;
};

struct RtcState {
  uint32_t magic;
  uint32_t boot_count;
  uint16_t seq;
  uint16_t interval_s;
  uint8_t channel;
  uint8_t hub_mac[6];
  uint8_t hub_known;
  uint8_t fail_streak;
  uint8_t head;   // bir sonraki yazılacak indeks
  uint8_t count;  // tampondaki ölçüm sayısı
  StoredReading buf[kBufCap];
};

RTC_DATA_ATTR RtcState rtc;

// ---------------------------------------------------------------------------
// Yardımcılar
// ---------------------------------------------------------------------------
static bool g_usb = false;
#define LOG(...)                       \
  do {                                 \
    if (g_usb) Serial.printf(__VA_ARGS__); \
  } while (0)

static const uint8_t kBroadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static volatile bool g_ack_ok = false;
static volatile bool g_send_done = false;
static volatile bool g_send_ok = false;
static volatile uint16_t g_wait_seq = 0;
static klima::Ack g_ack;
static uint8_t g_ack_mac[6];

static uint32_t nowS() { return static_cast<uint32_t>(time(nullptr)); }

static void bufPush(const StoredReading& r) {
  rtc.buf[rtc.head] = r;
  rtc.head = (rtc.head + 1) % kBufCap;
  if (rtc.count < kBufCap) rtc.count++;
}

// i = 0 en yeni ölçüm
static StoredReading& bufNewest(int i) {
  int idx = (int(rtc.head) - 1 - i + kBufCap * 2) % kBufCap;
  return rtc.buf[idx];
}

// En yeni n ölçümü tampondan siler (gönderildikleri onaylandı)
static void bufDropNewest(int n) {
  if (n > rtc.count) n = rtc.count;
  rtc.head = uint8_t((int(rtc.head) - n + kBufCap) % kBufCap);
  rtc.count -= n;
}

static void loadPersistent(bool cold) {
  if (!cold && rtc.magic == kRtcMagic) return;
  // Soğuk açılış: RTC belleği geçersiz, kalıcı ayarları NVS'ten yükle
  memset(&rtc, 0, sizeof(rtc));
  rtc.magic = kRtcMagic;
  Preferences p;
  p.begin("klima", true);
  rtc.interval_s = klima::clampInterval(p.getUShort("interval", klima::kDefaultIntervalS));
  rtc.channel = p.getUChar("channel", 1);
  rtc.hub_known = p.getBytes("hub", rtc.hub_mac, 6) == 6 ? 1 : 0;
  rtc.boot_count = p.getULong("boots", 0);
  p.end();
  if (rtc.channel < 1 || rtc.channel > 13) rtc.channel = 1;
}

static void savePersistent() {
  Preferences p;
  p.begin("klima", false);
  p.putUShort("interval", rtc.interval_s);
  p.putUChar("channel", rtc.channel);
  if (rtc.hub_known) p.putBytes("hub", rtc.hub_mac, 6);
  p.putULong("boots", rtc.boot_count);
  p.end();
}

// ---------------------------------------------------------------------------
// Ölçümler
// ---------------------------------------------------------------------------
static bool measureClimate(float& t, float& h) {
  gpio_hold_dis(static_cast<gpio_num_t>(kPinSensorPwr));
  pinMode(kPinSensorPwr, OUTPUT);
  digitalWrite(kPinSensorPwr, HIGH);
  delay(3);  // SHT31 açılış süresi en fazla 1 ms

  Wire.begin(kPinSda, kPinScl, 100000);
  klima::Sht31 sht;
  bool ok = sht.begin(Wire) && sht.measure(t, h);
  if (!ok) {
    sht.softReset();
    delay(2);
    ok = sht.measure(t, h);
  }
  Wire.end();
  // I2C hatlarını yüksek empedansa al: modül kapalıyken pull-up'lar üzerinden
  // ters besleme olmasın.
  pinMode(kPinSda, INPUT);
  pinMode(kPinScl, INPUT);
  digitalWrite(kPinSensorPwr, LOW);
  return ok;
}

static uint16_t readBatteryMv() {
  analogReadResolution(12);
  analogSetPinAttenuation(kPinBattAdc, ADC_11db);
  uint32_t sum = 0;
  constexpr int kSamples = 16;
  for (int i = 0; i < kSamples; i++) sum += analogReadMilliVolts(kPinBattAdc);
  float mv = float(sum) / kSamples * kBattDividerRatio * BATT_CAL;
  return static_cast<uint16_t>(mv + 0.5f);
}

// ---------------------------------------------------------------------------
// ESP-NOW
// ---------------------------------------------------------------------------
static void onRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  klima::Ack a;
  if (!klima::parseAck(data, size_t(len), a)) return;
  if (a.ack_seq != g_wait_seq) return;
  memcpy(g_ack_mac, info->src_addr, 6);
  g_ack = a;
  g_ack_ok = true;
}

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)
static void onSent(const esp_now_send_info_t*, esp_now_send_status_t st) {
#else
static void onSent(const uint8_t*, esp_now_send_status_t st) {
#endif
  g_send_ok = (st == ESP_NOW_SEND_SUCCESS);
  g_send_done = true;
}

static bool ensurePeer(const uint8_t* mac) {
  if (esp_now_is_peer_exist(mac)) return true;
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, mac, 6);
  peer.channel = 0;  // 0 = o anki kanal
  peer.ifidx = WIFI_IF_STA;
  peer.encrypt = false;
  return esp_now_add_peer(&peer) == ESP_OK;
}

static bool setChannel(uint8_t ch) {
  return esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE) == ESP_OK;
}

static bool radioStart() {
  WiFi.mode(WIFI_STA);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  esp_wifi_set_ps(WIFI_PS_NONE);
  if (esp_now_init() != ESP_OK) return false;
  esp_now_register_recv_cb(onRecv);
  esp_now_register_send_cb(onSent);
  ensurePeer(kBroadcast);
  if (rtc.hub_known) ensurePeer(rtc.hub_mac);
  return setChannel(rtc.channel);
}

static void radioStop() {
  esp_now_deinit();
  WiFi.mode(WIFI_OFF);
}

// Paketi window_ms boyunca kısa aralıklarla tekrar gönderir ve hub ACK'ini bekler.
// Neden: hub'da Bluetooth ile WiFi aynı radyoyu paylaşır; hub birkaç milisaniyelik
// aralıklarla "sağır" olabilir. Tekrarlar bu boşlukları kapatır.
static bool sendBurst(const uint8_t* dest, const uint8_t* data, size_t len, uint16_t seq,
                      uint32_t window_ms) {
  g_ack_ok = false;
  g_wait_seq = seq;
  const bool unicast = memcmp(dest, kBroadcast, 6) != 0;
  uint32_t t0 = millis();
  while (millis() - t0 < window_ms) {
    g_send_done = false;
    g_send_ok = false;
    if (esp_now_send(dest, data, len) != ESP_OK) {
      delay(5);
      continue;
    }
    // Gönderim sonucunu bekle (unicast'te hub'ın radyo katmanı onayı)
    uint32_t ts = millis();
    while (!g_send_done && millis() - ts < 20) {
      if (g_ack_ok) return true;
      delay(1);
    }
    // Hub paketi radyo katmanında aldıysa uygulama ACK'ini biraz daha uzun bekle
    uint32_t wait = (unicast && g_send_ok) ? 60 : 8;
    ts = millis();
    while (millis() - ts < wait) {
      if (g_ack_ok) return true;
      delay(1);
    }
  }
  return g_ack_ok;
}

// Hub'ın verdiği ayarları uygula
static void applyAck(const klima::Ack& a) {
  bool changed = false;
  uint16_t iv = klima::clampInterval(a.interval_s);
  if (iv != rtc.interval_s) {
    rtc.interval_s = iv;
    changed = true;
  }
  if (a.channel >= 1 && a.channel <= 13 && a.channel != rtc.channel) {
    rtc.channel = a.channel;
    changed = true;
  }
  if (!rtc.hub_known || memcmp(rtc.hub_mac, g_ack_mac, 6) != 0) {
    memcpy(rtc.hub_mac, g_ack_mac, 6);
    rtc.hub_known = 1;
    changed = true;
  }
  if (a.flags & klima::kAckTimeValid) {
    // Saati ayarla, tampondaki ölçümlerin zaman damgalarını aynı miktarda kaydır
    int64_t delta = int64_t(a.epoch) - int64_t(nowS());
    if (delta > 2 || delta < -2) {
      for (int i = 0; i < rtc.count; i++) {
        StoredReading& r = bufNewest(i);
        r.t = uint32_t(int64_t(r.t) + delta);
      }
      timeval tv = {static_cast<time_t>(a.epoch), 0};
      settimeofday(&tv, nullptr);
    }
  }
  if (changed) savePersistent();
}

// Tampondaki ölçümleri (en yeniler önce) paketler halinde gönderir.
// Dönüş: en az bir paket onaylandıysa true
static bool transmit(uint16_t batt_mv, uint8_t batt_pct, uint8_t flags) {
  bool any_ok = false;
  for (int pkt = 0; pkt < kMaxPacketsPerWake; pkt++) {
    klima::Report rep = {};
    uint16_t seq = ++rtc.seq;
    klima::fillHeader(rep.f.h, klima::MsgType::kReport, seq);
    rep.f.batt_mv = batt_mv;
    rep.f.batt_pct = batt_pct;
    rep.f.flags = flags;
    rep.f.interval_s = rtc.interval_s;
    rep.f.boot_count = rtc.boot_count;
    rep.f.fw_major = kFwMajor;
    rep.f.fw_minor = kFwMinor;
    int n = rtc.count < int(klima::kMaxReadingsPerReport) ? rtc.count : int(klima::kMaxReadingsPerReport);
    uint32_t now = nowS();
    for (int i = 0; i < n; i++) {
      const StoredReading& s = bufNewest(i);
      rep.r[i].age_s = now >= s.t ? now - s.t : 0;
      rep.r[i].temp_c100 = s.temp_c100;
      rep.r[i].hum_c100 = s.hum_c100;
    }
    rep.f.n_readings = uint8_t(n);
    size_t len = klima::reportSize(rep.f.n_readings);
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&rep);

    // Hub uzun süredir yanıt vermiyorsa (kapalı olabilir) pil için deneme penceresini kısalt
    const uint32_t window = rtc.fail_streak >= 3 ? kBurstShortMs : kBurstWindowMs;
    bool ok = false;
    // 1) Bilinen kanalda tekrarlı gönderim (hub biliniyorsa doğrudan ona)
    const uint8_t* dest = rtc.hub_known ? rtc.hub_mac : kBroadcast;
    ok = sendBurst(dest, bytes, len, seq, window);
    // 2) Hub'ın yayın adresine yanıt verip vermediğini de dene (MAC değişmiş olabilir)
    if (!ok && rtc.hub_known) ok = sendBurst(kBroadcast, bytes, len, seq, kBroadcastWindowMs);

    // 3) Kanal taraması (pil harcamasın diye seyrek)
    if (!ok && pkt == 0) {
      rtc.fail_streak++;
      bool do_scan = rtc.fail_streak == 1 || (rtc.fail_streak % kScanEveryNthFailure) == 0;
      if (do_scan) {
        LOG("Hub yanit vermedi, kanal taraniyor...\n");
        uint8_t orig = rtc.channel;
        for (uint8_t ch = 1; ch <= 13 && !ok; ch++) {
          if (ch == orig) continue;
          setChannel(ch);
          ok = sendBurst(kBroadcast, bytes, len, seq, kScanAckTimeoutMs);
          if (ok) {
            LOG("Hub kanal %u'de bulundu\n", ch);
            rtc.channel = ch;
          }
        }
        if (!ok) setChannel(orig);
      }
    }

    if (!ok) break;
    rtc.fail_streak = 0;
    any_ok = true;
    ensurePeer(g_ack_mac);
    applyAck(g_ack);
    bufDropNewest(n);
    if (rtc.count == 0) break;
  }
  return any_ok;
}

// ---------------------------------------------------------------------------
// Uyku
// ---------------------------------------------------------------------------
static void goToSleep(uint32_t sleep_s, bool wake_on_usb) {
  pinMode(kPinSensorPwr, OUTPUT);
  digitalWrite(kPinSensorPwr, LOW);
  gpio_hold_en(static_cast<gpio_num_t>(kPinSensorPwr));
  gpio_deep_sleep_hold_en();

  uint32_t awake_ms = millis();
  uint64_t us = uint64_t(sleep_s) * 1000000ULL;
  uint64_t awake_us = uint64_t(awake_ms) * 1000ULL;
  us = us > awake_us + 1000000ULL ? us - awake_us : 1000000ULL;
  esp_sleep_enable_timer_wakeup(us);
  if (wake_on_usb) {
    esp_deep_sleep_enable_gpio_wakeup(1ULL << kPinUsbSense, ESP_GPIO_WAKEUP_GPIO_HIGH);
  }
  LOG("Uyuyor: %lu sn (uyanik kalma %lu ms)\n", (unsigned long)(us / 1000000ULL), (unsigned long)awake_ms);
  if (g_usb) Serial.flush();
  esp_deep_sleep_start();
}

// ---------------------------------------------------------------------------
void setup() {
  pinMode(kPinUsbSense, INPUT);
  g_usb = digitalRead(kPinUsbSense) == HIGH;
  if (g_usb) {
    Serial.begin(115200);
    Serial.setTxTimeoutMs(0);
    delay(50);
  }

  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  bool cold = (cause == ESP_SLEEP_WAKEUP_UNDEFINED);
  loadPersistent(cold);
  rtc.boot_count++;

  uint8_t flags = 0;
  if (cold) flags |= klima::kFlagFirstBoot;
  if (g_usb) flags |= klima::kFlagUsbPower;

  LOG("\n[Sensor] v%u.%u acilis #%lu, sebep=%d, kanal=%u, aralik=%u sn\n", kFwMajor, kFwMinor,
      (unsigned long)rtc.boot_count, int(cause), rtc.channel, rtc.interval_s);

  // 1) Ölçüm
  float t = 0, h = 0;
  if (measureClimate(t, h)) {
    StoredReading r;
    r.t = nowS();
    r.temp_c100 = static_cast<int16_t>(lroundf(t * 100.0f));
    r.hum_c100 = static_cast<uint16_t>(lroundf(h * 100.0f));
    bufPush(r);
    LOG("Olcum: %.2f C, %%%.1f\n", t, h);
  } else {
    flags |= klima::kFlagSensorError;
    LOG("SHT31 okunamadi!\n");
  }

  // 2) Pil
  uint16_t mv = readBatteryMv();
  uint8_t pct = klima::lipoPercent(mv);
  bool battery_ok = mv >= klima::kBattPresentMinMv;
  bool critical = battery_ok && !g_usb && mv < klima::kBattCriticalMv;
  if (battery_ok && mv < klima::kBattLowMv) flags |= klima::kFlagLowBattery;
  if (critical) flags |= klima::kFlagCritical;
  LOG("Pil: %u mV (%%%u)%s\n", mv, pct, g_usb ? " [USB]" : "");

  // 3) Gönderim
  if (radioStart()) {
    bool ok = transmit(mv, pct, flags);
    LOG(ok ? "Hub onayladi.\n" : "Hub'a ulasilamadi, %u olcum tamponda.\n", rtc.count);
  }
  radioStop();

  if (cold) savePersistent();

  // 4) Uyku
  if (critical) {
    goToSleep(kCriticalSleepS, true);
  }
  goToSleep(rtc.interval_s, !g_usb);
}

void loop() {
  // Buraya hiç gelinmez; setup() derin uykuyla biter.
}
