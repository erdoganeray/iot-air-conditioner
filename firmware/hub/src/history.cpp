#include "history.h"

#include <LittleFS.h>
#include <esp_heap_caps.h>

#include "history_core.h"
#include "log.h"
#include "timekeeper.h"

namespace hub::history {
namespace {

constexpr uint32_t kSavePeriodMs = 30 * 60000UL;
constexpr int kMaxEvents = 200;
constexpr size_t kFileSize = sizeof(HistBin) * HistoryCore::kBins;

#pragma pack(push, 1)
struct AcEvent {
  uint32_t epoch;
  uint8_t bits;   // bit0 açık, bit1 salınım, bit2-4 fan
  uint8_t mode;
  uint8_t temp2;  // derece x2
  uint8_t src;
};
struct EventFileHeader {
  uint32_t magic;
  uint16_t head;
  uint16_t count;
};
#pragma pack(pop)
constexpr uint32_t kEventMagic = 0x41435632;  // "ACV2"

HistoryCore s_core;
AcEvent s_events[kMaxEvents];
uint16_t s_ev_head = 0, s_ev_count = 0;
uint32_t s_last_save_ms = 0;

String pathFor(int src) { return String("/hist_") + src + ".bin"; }

void loadSource(int src) {
  HistBin* bins = s_core.raw(src);
  if (!bins) return;
  String p = pathFor(src);
  File f = LittleFS.open(p, "r");
  if (!f || f.size() != kFileSize) {
    if (f) f.close();
    s_core.markAllDirty(src);  // ilk kayıtta dosya baştan oluşturulsun
    return;
  }
  size_t n = f.read(reinterpret_cast<uint8_t*>(bins), kFileSize);
  f.close();
  if (n != kFileSize) {
    memset(bins, 0, kFileSize);
    s_core.markAllDirty(src);
  }
}

void saveSource(int src) {
  HistBin* bins = s_core.raw(src);
  if (!bins || !s_core.anyDirty(src)) return;
  String p = pathFor(src);
  bool exists = LittleFS.exists(p);
  File f;
  if (exists) {
    f = LittleFS.open(p, "r+");
    if (f && f.size() != kFileSize) {
      f.close();
      exists = false;
    }
  }
  if (!exists) {
    // Dosyayı baştan, tamamıyla yaz
    f = LittleFS.open(p, "w");
    if (!f) return;
    f.write(reinterpret_cast<const uint8_t*>(bins), kFileSize);
    f.close();
    s_core.forEachDirty(src, [](int) {});
    return;
  }
  if (!f) return;
  s_core.forEachDirty(src, [&](int idx) {
    f.seek(size_t(idx) * sizeof(HistBin));
    f.write(reinterpret_cast<const uint8_t*>(&bins[idx]), sizeof(HistBin));
  });
  f.close();
}

void loadEvents() {
  File f = LittleFS.open("/acevents.bin", "r");
  if (!f) return;
  EventFileHeader h;
  if (f.read(reinterpret_cast<uint8_t*>(&h), sizeof(h)) == sizeof(h) && h.magic == kEventMagic &&
      h.head < kMaxEvents && h.count <= kMaxEvents) {
    if (f.read(reinterpret_cast<uint8_t*>(s_events), sizeof(s_events)) == sizeof(s_events)) {
      s_ev_head = h.head;
      s_ev_count = h.count;
    }
  }
  f.close();
}

void saveEvents() {
  File f = LittleFS.open("/acevents.bin", "w");
  if (!f) return;
  EventFileHeader h = {kEventMagic, s_ev_head, s_ev_count};
  f.write(reinterpret_cast<const uint8_t*>(&h), sizeof(h));
  f.write(reinterpret_cast<const uint8_t*>(s_events), sizeof(s_events));
  f.close();
}

}  // namespace

void begin() {
  for (int src = 0; src < kNumSources; src++) {
    void* mem = heap_caps_malloc(kFileSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!mem) mem = heap_caps_malloc(kFileSize, MALLOC_CAP_8BIT);
    if (!mem) {
      LOGE("Gecmis icin bellek yok (kaynak %d)", src);
      continue;
    }
    s_core.attach(src, static_cast<HistBin*>(mem));
    loadSource(src);
  }
  loadEvents();
  s_last_save_ms = millis();
  LOGI("Gecmis yuklendi (%u olay)", s_ev_count);
}

void loop() {
  if (millis() - s_last_save_ms >= kSavePeriodMs) saveNow();
}

void saveNow() {
  s_last_save_ms = millis();
  uint32_t t0 = millis();
  for (int src = 0; src < kNumSources; src++) saveSource(src);
  LOGI("Gecmis kaydedildi (%lu ms)", (unsigned long)(millis() - t0));
}

void add(int src, uint32_t epoch, float temp, float hum) {
  if (!epoch) return;  // saat bilinmiyorsa geçmişe yazılamaz
  s_core.add(src, epoch, temp, hum);
}

void clearSource(int src) { s_core.clear(src); }

bool queryJson(int src, uint32_t hours, uint32_t step_min, JsonObject out) {
  uint32_t now = timekeeper::now();
  if (!now || !s_core.available(src)) return false;
  if (hours < 1) hours = 1;
  if (hours > 168) hours = 168;
  uint32_t step = step_min * 60;
  if (step < HistoryCore::kBinS) step = HistoryCore::kBinS;
  uint32_t from = now - hours * 3600;
  constexpr int kMaxPoints = 400;
  // Nokta sayısı sınırı: adımı gerekirse büyüt
  while ((hours * 3600) / step > uint32_t(kMaxPoints)) step += HistoryCore::kBinS;
  static int16_t t[kMaxPoints];
  static uint16_t h[kMaxPoints];
  int n = s_core.query(src, from, now + 1, step, t, h, kMaxPoints);
  out["src"] = src;
  out["t0"] = (from / step) * step;
  out["step"] = step;
  JsonArray at = out["temp"].to<JsonArray>();
  JsonArray ah = out["hum"].to<JsonArray>();
  for (int i = 0; i < n; i++) {
    if (t[i] == kHistNoTemp) {
      at.add(nullptr);
      ah.add(nullptr);
    } else {
      at.add(int((t[i] + (t[i] >= 0 ? 5 : -5)) / 10));  // x10
      ah.add(int((h[i] + 5) / 10));
    }
  }
  return true;
}

void logAc(const AcState& s, Source src) {
  AcEvent& e = s_events[s_ev_head];
  e.epoch = timekeeper::now();
  e.bits = uint8_t((s.power ? 1 : 0) | (s.swing ? 2 : 0) | ((uint8_t(s.fan) & 7) << 2));
  e.mode = uint8_t(s.mode);
  e.temp2 = uint8_t(s.temp * 2);
  e.src = uint8_t(src);
  s_ev_head = (s_ev_head + 1) % kMaxEvents;
  if (s_ev_count < kMaxEvents) s_ev_count++;
  saveEvents();
}

bool lastAc(AcState& s, Source& src, uint32_t& epoch) {
  if (!s_ev_count) return false;
  const AcEvent& e = s_events[(s_ev_head - 1 + kMaxEvents) % kMaxEvents];
  s.power = e.bits & 1;
  s.swing = e.bits & 2;
  uint8_t fan = (e.bits >> 2) & 7;
  s.fan = fan <= uint8_t(AcFan::kMax) ? AcFan(fan) : AcFan::kAuto;
  s.mode = e.mode <= uint8_t(AcMode::kFan) ? AcMode(e.mode) : AcMode::kCool;
  s.temp = clampAcTemp(e.temp2 / 2.0f);
  src = Source(e.src);
  epoch = e.epoch;
  return true;
}

void eventsJson(JsonArray out, int max_items) {
  int n = s_ev_count < max_items ? s_ev_count : max_items;
  for (int i = 0; i < n; i++) {
    const AcEvent& e = s_events[(s_ev_head - 1 - i + kMaxEvents * 2) % kMaxEvents];
    JsonObject o = out.add<JsonObject>();
    o["t"] = e.epoch;
    o["power"] = bool(e.bits & 1);
    o["mode"] = modeName(AcMode(e.mode));
    o["temp"] = e.temp2 / 2.0f;
    o["src"] = sourceName(Source(e.src));
  }
}

}  // namespace hub::history
