// Hub genelinde kullanılan veri tipleri. Arduino'ya bağımlı değildir;
// masaüstü birim testlerinde de derlenir.
#pragma once

#include <stdint.h>
#include <string.h>

namespace hub {

constexpr int kMaxSensors = 4;   // uzak sensör yuvası (1..4)
constexpr int kNumSources = kMaxSensors + 1;  // 0 = hub'ın kendi sensörü
constexpr int kHubSource = 0;

enum class AcMode : uint8_t { kAuto = 0, kCool = 1, kHeat = 2, kDry = 3, kFan = 4 };
enum class AcFan : uint8_t { kAuto = 0, kMin = 1, kLow = 2, kMedium = 3, kHigh = 4, kMax = 5 };

struct AcState {
  bool power = false;
  AcMode mode = AcMode::kCool;
  float temp = 24.0f;
  AcFan fan = AcFan::kAuto;
  bool swing = false;

  bool sameAs(const AcState& o) const {
    if (power != o.power) return false;
    if (!power) return true;  // ikisi de kapalıysa diğer alanlar önemsiz
    return mode == o.mode && temp == o.temp && fan == o.fan && swing == o.swing;
  }
};

// Klima durumunu kim değiştirdi
enum class Source : uint8_t {
  kNone = 0,
  kApp,        // telefon uygulaması (BLE / WebSocket)
  kTelegram,
  kRemote,     // orijinal kumanda (IR alıcıdan duyuldu)
  kTouch,      // hub üzerindeki dokunmatik buton
  kThermostat,
  kDry,
  kSchedule,
  kSerial,
};

inline bool isManualSource(Source s) {
  return s == Source::kApp || s == Source::kTelegram || s == Source::kRemote ||
         s == Source::kTouch || s == Source::kSerial;
}

inline const char* sourceName(Source s) {
  switch (s) {
    case Source::kApp: return "app";
    case Source::kTelegram: return "telegram";
    case Source::kRemote: return "remote";
    case Source::kTouch: return "touch";
    case Source::kThermostat: return "thermostat";
    case Source::kDry: return "dry";
    case Source::kSchedule: return "schedule";
    case Source::kSerial: return "serial";
    default: return "none";
  }
}

inline const char* modeName(AcMode m) {
  switch (m) {
    case AcMode::kAuto: return "auto";
    case AcMode::kCool: return "cool";
    case AcMode::kHeat: return "heat";
    case AcMode::kDry: return "dry";
    case AcMode::kFan: return "fan";
  }
  return "cool";
}

inline bool parseMode(const char* s, AcMode& out) {
  if (!s) return false;
  struct { const char* n; AcMode m; } tbl[] = {
      {"auto", AcMode::kAuto},  {"otomatik", AcMode::kAuto}, {"cool", AcMode::kCool},
      {"sogutma", AcMode::kCool}, {"soğutma", AcMode::kCool}, {"heat", AcMode::kHeat},
      {"isitma", AcMode::kHeat}, {"ısıtma", AcMode::kHeat}, {"dry", AcMode::kDry},
      {"kuru", AcMode::kDry},   {"kurutma", AcMode::kDry},  {"fan", AcMode::kFan},
  };
  for (auto& e : tbl) {
    if (strcmp(s, e.n) == 0) {
      out = e.m;
      return true;
    }
  }
  return false;
}

inline const char* fanName(AcFan f) {
  switch (f) {
    case AcFan::kAuto: return "auto";
    case AcFan::kMin: return "min";
    case AcFan::kLow: return "low";
    case AcFan::kMedium: return "medium";
    case AcFan::kHigh: return "high";
    case AcFan::kMax: return "max";
  }
  return "auto";
}

inline bool parseFan(const char* s, AcFan& out) {
  if (!s) return false;
  struct { const char* n; AcFan f; } tbl[] = {
      {"auto", AcFan::kAuto}, {"otomatik", AcFan::kAuto}, {"min", AcFan::kMin},
      {"low", AcFan::kLow},   {"dusuk", AcFan::kLow},     {"medium", AcFan::kMedium},
      {"orta", AcFan::kMedium}, {"high", AcFan::kHigh},   {"yuksek", AcFan::kHigh},
      {"max", AcFan::kMax},   {"1", AcFan::kLow},         {"2", AcFan::kMedium},
      {"3", AcFan::kHigh},
  };
  for (auto& e : tbl) {
    if (strcmp(s, e.n) == 0) {
      out = e.f;
      return true;
    }
  }
  return false;
}

constexpr float kAcMinTemp = 16.0f;
constexpr float kAcMaxTemp = 30.0f;

inline float clampAcTemp(float t) {
  if (t < kAcMinTemp) return kAcMinTemp;
  if (t > kAcMaxTemp) return kAcMaxTemp;
  // 0.5 derece adımlarına yuvarla
  return float(int(t * 2.0f + 0.5f)) / 2.0f;
}

}  // namespace hub
