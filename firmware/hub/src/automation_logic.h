// Otomasyon karar mantığı: termostat, nem (kurutma) ve zamanlayıcı.
// Donanımdan bağımsızdır (saf C++), masaüstü birim testleriyle doğrulanır:
//   pio test -e native
#pragma once

#include <stdint.h>

#include "model.h"

namespace hub {

// Termostat/nem için sensör kaynağı: 0 = hub, 1..4 = sensör yuvası, 255 = ortalama
constexpr uint8_t kSourceAverage = 255;

struct ThermoConfig {
  bool enabled = false;
  uint8_t source = kHubSource;
  AcMode mode = AcMode::kCool;  // yalnızca kCool veya kHeat
  float target = 25.0f;         // oda hedef sıcaklığı
  float hyst = 0.5f;            // eşik payı (±)
  float ac_temp = 24.0f;        // klimaya gönderilecek derece
  AcFan fan = AcFan::kAuto;
};

struct DryConfig {
  bool enabled = false;
  uint8_t source = kHubSource;
  float on_pct = 65.0f;   // bu nemin üstünde kurutma başlar
  float off_pct = 55.0f;  // bu nemin altında kurutma biter
  float ac_temp = 24.0f;
};

struct AutoTiming {
  uint32_t min_on_s = 300;      // klima açıldıktan sonra en az bu kadar açık kalır
  uint32_t min_off_s = 300;     // kapandıktan sonra en az bu kadar kapalı kalır
  uint32_t override_s = 3600;   // elle müdahaleden sonra otomasyon bekleme süresi
};

enum class Owner : uint8_t { kNone = 0, kThermostat, kDry };

struct AutoInputs {
  uint32_t now_s = 0;           // monoton saniye (açılıştan beri)
  bool temp_valid = false;
  float temp = 0;
  bool hum_valid = false;
  float hum = 0;
  AcState ac;                   // klimanın bilinen son durumu
  bool has_power_change = false;
  uint32_t last_power_change_s = 0;
  bool has_manual = false;
  uint32_t last_manual_s = 0;
};

// Otomasyonun hafızası (RAM'de tutulur)
struct AutoMemory {
  Owner owner = Owner::kNone;  // klimayı şu an hangi otomasyon "yönetiyor"
};

enum class Want : uint8_t { kKeep = 0, kOn, kOff };

struct AutoDecision {
  bool act = false;        // klimaya komut gönderilmeli mi
  bool deferred = false;   // gönderilmeliydi ama kısa döngü koruması bekletiyor
  bool overridden = false; // elle müdahale nedeniyle beklemede
  AcState target;
  Source reason = Source::kNone;
  Want thermo_want = Want::kKeep;
  Want dry_want = Want::kKeep;
};

inline Want thermoWant(const ThermoConfig& c, bool valid, float t) {
  if (!c.enabled || !valid) return Want::kKeep;
  if (c.mode == AcMode::kHeat) {
    if (t <= c.target - c.hyst) return Want::kOn;
    if (t >= c.target + c.hyst) return Want::kOff;
  } else {
    if (t >= c.target + c.hyst) return Want::kOn;
    if (t <= c.target - c.hyst) return Want::kOff;
  }
  return Want::kKeep;
}

inline Want dryWant(const DryConfig& c, bool valid, float h) {
  if (!c.enabled || !valid) return Want::kKeep;
  if (h >= c.on_pct) return Want::kOn;
  if (h <= c.off_pct) return Want::kOff;
  return Want::kKeep;
}

// Ana karar fonksiyonu. mem güncellenebilir.
inline AutoDecision evaluateAutomation(const ThermoConfig& th, const DryConfig& dr,
                                       const AutoTiming& tm, const AutoInputs& in,
                                       AutoMemory& mem) {
  AutoDecision d;
  // Devre dışı bırakılan otomasyonun sahipliğini bırak
  if (!th.enabled && mem.owner == Owner::kThermostat) mem.owner = Owner::kNone;
  if (!dr.enabled && mem.owner == Owner::kDry) mem.owner = Owner::kNone;

  d.thermo_want = thermoWant(th, in.temp_valid, in.temp);
  d.dry_want = dryWant(dr, in.hum_valid, in.hum);

  if (in.has_manual && in.now_s - in.last_manual_s < tm.override_s) {
    d.overridden = true;
    return d;
  }

  AcState thermo_state = in.ac;
  thermo_state.power = true;
  thermo_state.mode = (th.mode == AcMode::kHeat) ? AcMode::kHeat : AcMode::kCool;
  thermo_state.temp = clampAcTemp(th.ac_temp);
  thermo_state.fan = th.fan;

  AcState dry_state = in.ac;
  dry_state.power = true;
  dry_state.mode = AcMode::kDry;
  dry_state.temp = clampAcTemp(dr.ac_temp);
  dry_state.fan = AcFan::kAuto;

  AcState off_state = in.ac;
  off_state.power = false;

  bool have_target = false;
  AcState target;
  Owner new_owner = mem.owner;
  Source reason = Source::kNone;

  const bool dry_running = in.ac.power && in.ac.mode == AcMode::kDry;

  if (th.enabled) {
    if (d.thermo_want == Want::kOn) {
      target = thermo_state;
      new_owner = Owner::kThermostat;
      reason = Source::kThermostat;
      have_target = true;
    } else if (d.thermo_want == Want::kOff) {
      bool dry_needed = d.dry_want == Want::kOn ||
                        (d.dry_want == Want::kKeep && mem.owner == Owner::kDry && dry_running);
      if (dry_needed) {
        target = dry_state;
        new_owner = Owner::kDry;
        reason = Source::kDry;
      } else {
        target = off_state;
        new_owner = Owner::kNone;
        reason = Source::kThermostat;
      }
      have_target = true;
    } else {  // termostat bandın içinde
      if (mem.owner == Owner::kThermostat) {
        // olduğu gibi devam
      } else if (!in.ac.power && d.dry_want == Want::kOn) {
        target = dry_state;
        new_owner = Owner::kDry;
        reason = Source::kDry;
        have_target = true;
      } else if (mem.owner == Owner::kDry && d.dry_want == Want::kOff) {
        target = off_state;
        new_owner = Owner::kNone;
        reason = Source::kDry;
        have_target = true;
      }
    }
  } else {
    if (d.dry_want == Want::kOn && (!in.ac.power || mem.owner == Owner::kDry)) {
      target = dry_state;
      new_owner = Owner::kDry;
      reason = Source::kDry;
      have_target = true;
    } else if (d.dry_want == Want::kOff && mem.owner == Owner::kDry && in.ac.power) {
      target = off_state;
      new_owner = Owner::kNone;
      reason = Source::kDry;
      have_target = true;
    }
  }

  if (!have_target) return d;

  if (target.sameAs(in.ac)) {
    mem.owner = new_owner;  // zaten istenen durumda; sahipliği üstlen
    return d;
  }

  // Kısa döngü koruması (yalnızca aç/kapa geçişlerinde)
  if (target.power != in.ac.power && in.has_power_change) {
    uint32_t elapsed = in.now_s - in.last_power_change_s;
    uint32_t need = in.ac.power ? tm.min_on_s : tm.min_off_s;
    if (elapsed < need) {
      d.deferred = true;
      d.target = target;
      d.reason = reason;
      return d;
    }
  }

  mem.owner = new_owner;
  d.act = true;
  d.target = target;
  d.reason = reason;
  return d;
}

// ---------------------------------------------------------------------------
// Zamanlayıcı
// ---------------------------------------------------------------------------
enum class SchedAction : uint8_t {
  kAcOn = 0,
  kAcOff,
  kThermoOn,
  kThermoOff,
  kDryOn,
  kDryOff,
};

inline const char* schedActionName(SchedAction a) {
  switch (a) {
    case SchedAction::kAcOn: return "on";
    case SchedAction::kAcOff: return "off";
    case SchedAction::kThermoOn: return "thermo_on";
    case SchedAction::kThermoOff: return "thermo_off";
    case SchedAction::kDryOn: return "dry_on";
    case SchedAction::kDryOff: return "dry_off";
  }
  return "off";
}

inline bool parseSchedAction(const char* s, SchedAction& out) {
  if (!s) return false;
  for (int i = 0; i <= int(SchedAction::kDryOff); i++) {
    const char* n = schedActionName(SchedAction(i));
    int k = 0;
    while (n[k] && s[k] && n[k] == s[k]) k++;
    if (n[k] == 0 && s[k] == 0) {
      out = SchedAction(i);
      return true;
    }
  }
  return false;
}

struct ScheduleEntry {
  bool enabled = true;
  uint8_t days = 0x7F;      // bit0 = Pazartesi ... bit6 = Pazar
  uint16_t minute = 0;      // gün içindeki dakika (0..1439)
  SchedAction action = SchedAction::kAcOff;
  AcMode mode = AcMode::kCool;
  float temp = 24.0f;
  uint32_t last_fired_day = 0xFFFFFFFF;  // yerel gün numarası (RAM)
};

// weekday_mon0: 0 = Pazartesi. day_key: yerel takvimde gün numarası.
inline bool scheduleDue(const ScheduleEntry& e, int weekday_mon0, int minute_of_day,
                        uint32_t day_key) {
  if (!e.enabled) return false;
  if (weekday_mon0 < 0 || weekday_mon0 > 6) return false;
  if (!(e.days & (1u << weekday_mon0))) return false;
  if (minute_of_day != e.minute) return false;
  return e.last_fired_day != day_key;
}

// Ortalama sıcaklık/nem gibi kaynak seçimi için yardımcı
struct SourceReading {
  bool valid = false;
  float temp = 0;
  float hum = 0;
};

inline SourceReading pickSource(const SourceReading* all, int n, uint8_t source) {
  SourceReading r;
  if (source == kSourceAverage) {
    float st = 0, sh = 0;
    int k = 0;
    for (int i = 0; i < n; i++) {
      if (!all[i].valid) continue;
      st += all[i].temp;
      sh += all[i].hum;
      k++;
    }
    if (k > 0) {
      r.valid = true;
      r.temp = st / k;
      r.hum = sh / k;
    }
    return r;
  }
  if (source < n && all[source].valid) return all[source];
  // Seçili kaynak yoksa hub sensörüne düş
  if (n > 0 && all[kHubSource].valid) return all[kHubSource];
  return r;
}

}  // namespace hub
