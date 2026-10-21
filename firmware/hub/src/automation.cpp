#include "automation.h"

#include "app_config.h"
#include "automation_logic.h"
#include "ir_ctrl.h"
#include "log.h"
#include "notify.h"
#include "runtime.h"
#include "sensors.h"
#include "timekeeper.h"

namespace hub::automation {
namespace {

constexpr uint32_t kEvalPeriodMs = 10000;
constexpr uint32_t kSchedPeriodMs = 5000;

uint32_t s_last_eval = 0;
uint32_t s_last_sched = 0;
bool s_kick = true;
bool s_thermo_stale_notified = false;
uint32_t s_last_fail_ms = 0;

SourceReading s_thermo_src, s_dry_src;

const char* wantName(Want w) {
  switch (w) {
    case Want::kOn: return "on";
    case Want::kOff: return "off";
    default: return "keep";
  }
}

const char* ownerName(Owner o) {
  switch (o) {
    case Owner::kThermostat: return "thermostat";
    case Owner::kDry: return "dry";
    default: return "none";
  }
}

void evaluate() {
  SourceReading all[kNumSources];
  sensors::snapshot(all);
  s_thermo_src = pickSource(all, kNumSources, g_cfg.thermo.source);
  s_dry_src = pickSource(all, kNumSources, g_cfg.dry.source);

  AutoInputs in;
  in.now_s = uptimeS();
  in.temp_valid = s_thermo_src.valid;
  in.temp = s_thermo_src.temp;
  in.hum_valid = s_dry_src.valid;
  in.hum = s_dry_src.hum;
  in.ac = g_rt.ac.state;
  in.has_power_change = g_rt.ac.has_power_change;
  in.last_power_change_s = g_rt.ac.last_power_change_s;
  in.has_manual = g_rt.ac.has_manual;
  in.last_manual_s = g_rt.ac.last_manual_s;

  Owner before = g_rt.auto_mem.owner;
  AutoDecision d = evaluateAutomation(g_cfg.thermo, g_cfg.dry, g_cfg.timing, in, g_rt.auto_mem);
  bool changed = d.thermo_want != g_rt.last_decision.thermo_want ||
                 d.dry_want != g_rt.last_decision.dry_want || d.deferred != g_rt.last_decision.deferred ||
                 before != g_rt.auto_mem.owner;
  g_rt.last_decision = d;
  if (changed) markStateDirty();

  // Seçili sensörden veri gelmiyorsa bir kez uyar
  bool stale = g_cfg.thermo.enabled && !s_thermo_src.valid;
  if (stale && !s_thermo_stale_notified) {
    s_thermo_stale_notified = true;
    notify::send(notify::Kind::kInfo, "⚠️ Termostat beklemede: seçili sensörden güncel veri yok.");
  } else if (!stale) {
    s_thermo_stale_notified = false;
  }

  if (!d.act) return;
  if (!ir::ready()) return;
  // IR gönderimi başarısızsa 60 sn bekle (sürekli denemesin)
  if (s_last_fail_ms && millis() - s_last_fail_ms < 60000) return;
  String err;
  if (ir::apply(d.target, d.reason, err)) {
    LOGI("Otomasyon: klima %s (%s)", d.target.power ? modeName(d.target.mode) : "kapali",
         sourceName(d.reason));
    s_last_fail_ms = 0;
  } else {
    LOGW("Otomasyon komutu gonderilemedi: %s", err.c_str());
    s_last_fail_ms = millis() ? millis() : 1;
  }
}

void runSchedule(ScheduleEntry& e) {
  String err;
  char msg[96];
  AcState s = g_rt.ac.state;
  switch (e.action) {
    case SchedAction::kAcOn:
      s.power = true;
      s.mode = e.mode;
      s.temp = e.temp;
      ir::apply(s, Source::kSchedule, err);
      snprintf(msg, sizeof(msg), "⏰ Zamanlayıcı: klima açıldı (%s %.1f°C)", modeName(e.mode), e.temp);
      break;
    case SchedAction::kAcOff:
      s.power = false;
      ir::apply(s, Source::kSchedule, err);
      snprintf(msg, sizeof(msg), "⏰ Zamanlayıcı: klima kapatıldı");
      break;
    case SchedAction::kThermoOn:
      g_cfg.thermo.enabled = true;
      snprintf(msg, sizeof(msg), "⏰ Zamanlayıcı: termostat açıldı");
      break;
    case SchedAction::kThermoOff:
      g_cfg.thermo.enabled = false;
      snprintf(msg, sizeof(msg), "⏰ Zamanlayıcı: termostat kapatıldı");
      break;
    case SchedAction::kDryOn:
      g_cfg.dry.enabled = true;
      snprintf(msg, sizeof(msg), "⏰ Zamanlayıcı: nem otomasyonu açıldı");
      break;
    case SchedAction::kDryOff:
      g_cfg.dry.enabled = false;
      snprintf(msg, sizeof(msg), "⏰ Zamanlayıcı: nem otomasyonu kapatıldı");
      break;
  }
  if (e.action == SchedAction::kAcOn || e.action == SchedAction::kAcOff) {
    // Zamanlayıcı açık bir kullanıcı isteğidir: otomasyon hemen geri almasın
    g_rt.ac.has_manual = true;
    g_rt.ac.last_manual_s = uptimeS();
    g_rt.auto_mem.owner = Owner::kNone;
  } else {
    config::markDirty();
  }
  if (err.length()) LOGW("Zamanlayici: %s", err.c_str());
  notify::send(notify::Kind::kAuto, msg);
  markStateDirty();
  s_kick = true;
}

void checkSchedules() {
  struct tm lt;
  if (!timekeeper::local(lt)) return;
  int minute = lt.tm_hour * 60 + lt.tm_min;
  int wd = (lt.tm_wday + 6) % 7;  // 0 = Pazartesi
  uint32_t day_key = uint32_t(lt.tm_year) * 400u + uint32_t(lt.tm_yday);
  for (int i = 0; i < g_cfg.n_sched; i++) {
    ScheduleEntry& e = g_cfg.sched[i];
    if (scheduleDue(e, wd, minute, day_key)) {
      e.last_fired_day = day_key;
      LOGI("Zamanlayici #%d calisti: %s", i, schedActionName(e.action));
      runSchedule(e);
    }
  }
}

}  // namespace

void loop() {
  uint32_t now = millis();
  if (now - s_last_sched >= kSchedPeriodMs) {
    s_last_sched = now;
    checkSchedules();
  }
  if (s_kick || now - s_last_eval >= kEvalPeriodMs) {
    s_kick = false;
    s_last_eval = now;
    evaluate();
  }
}

void kick() { s_kick = true; }

void statusJson(JsonObject o) {
  const AutoDecision& d = g_rt.last_decision;
  JsonObject th = o["thermo"].to<JsonObject>();
  th["enabled"] = g_cfg.thermo.enabled;
  th["mode"] = modeName(g_cfg.thermo.mode);
  th["target"] = g_cfg.thermo.target;
  th["hyst"] = g_cfg.thermo.hyst;
  th["source"] = g_cfg.thermo.source;
  th["source_name"] = sensors::name(g_cfg.thermo.source);
  th["want"] = wantName(d.thermo_want);
  if (s_thermo_src.valid) th["value"] = roundf(s_thermo_src.temp * 10) / 10;

  JsonObject dr = o["dry"].to<JsonObject>();
  dr["enabled"] = g_cfg.dry.enabled;
  dr["on"] = g_cfg.dry.on_pct;
  dr["off"] = g_cfg.dry.off_pct;
  dr["source"] = g_cfg.dry.source;
  dr["source_name"] = sensors::name(g_cfg.dry.source);
  dr["want"] = wantName(d.dry_want);
  if (s_dry_src.valid) dr["value"] = roundf(s_dry_src.hum * 10) / 10;

  o["owner"] = ownerName(g_rt.auto_mem.owner);
  o["deferred"] = d.deferred;
  uint32_t hold = 0;
  if (g_rt.ac.has_manual) {
    uint32_t el = uptimeS() - g_rt.ac.last_manual_s;
    if (el < g_cfg.timing.override_s) hold = g_cfg.timing.override_s - el;
  }
  o["override_s"] = hold;
  o["schedules"] = g_cfg.n_sched;
}

}  // namespace hub::automation
