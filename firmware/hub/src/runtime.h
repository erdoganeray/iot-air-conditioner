// Çalışma zamanı durumu (RAM). Yalnızca ana döngüden değiştirilir.
#pragma once

#include <Arduino.h>

#include "automation_logic.h"
#include "model.h"

namespace hub {

struct SensorRuntime {
  bool seen = false;          // açılıştan beri en az bir rapor geldi
  bool online = false;
  bool has_reading = false;
  float temp = 0;             // kalibrasyon uygulanmış
  float hum = 0;
  uint32_t last_seen_ms = 0;
  uint32_t last_reading_ms = 0;
  uint32_t last_reading_epoch = 0;  // tekrar gönderilen ölçümleri ayıklamak için
  uint16_t batt_mv = 0;
  uint8_t batt_pct = 0;
  uint8_t flags = 0;
  uint16_t interval_s = 0;    // sensörün bildirdiği aralık
  uint32_t boot_count = 0;
  uint8_t fw_major = 0, fw_minor = 0;
  int8_t rssi = 0;
  uint32_t reports = 0;
  bool low_batt_notified = false;
  bool offline_notified = false;
};

struct HubRuntime {
  bool sensor_ok = false;
  float temp = 0;  // kalibrasyon uygulanmış
  float hum = 0;
  uint32_t last_read_ms = 0;
  uint16_t batt_mv = 0;
  uint8_t batt_pct = 0;
  bool usb = true;
  bool battery_present = false;
  bool battery_critical = false;
};

struct AcRuntime {
  AcState state;
  Source last_src = Source::kNone;
  uint32_t last_change_epoch = 0;
  bool has_power_change = false;
  uint32_t last_power_change_s = 0;
  bool has_manual = false;
  uint32_t last_manual_s = 0;
};

struct Runtime {
  SensorRuntime sensors[kMaxSensors];
  HubRuntime hub;
  AcRuntime ac;
  AutoMemory auto_mem;
  AutoDecision last_decision;
  bool state_dirty = true;  // uygulamalara durum yayını gerekiyor
};

extern Runtime g_rt;

inline uint32_t uptimeS() { return uint32_t(millis() / 1000); }

// Durum değişti: bağlı uygulamalara (kısıtlanmış hızda) yayınlanır
inline void markStateDirty() { g_rt.state_dirty = true; }

}  // namespace hub
