// Masaüstü birim testleri: pio test -e native
#include <unity.h>

#include "automation_logic.h"
#include "battery.h"
#include "history_core.h"
#include "klima_protocol.h"
#include "sht31.h"

using namespace hub;

void setUp() {}
void tearDown() {}

// ---------------------------------------------------------------- termostat
static AutoInputs baseInputs(float t, bool power, AcMode mode = AcMode::kCool) {
  AutoInputs in;
  in.now_s = 100000;
  in.temp_valid = true;
  in.temp = t;
  in.hum_valid = true;
  in.hum = 50;
  in.ac.power = power;
  in.ac.mode = mode;
  in.ac.temp = 24;
  return in;
}

void test_thermo_cool_turns_on_above_band() {
  ThermoConfig th;
  th.enabled = true;
  th.target = 25;
  th.hyst = 0.5;
  DryConfig dr;
  AutoTiming tm;
  AutoMemory mem;
  auto d = evaluateAutomation(th, dr, tm, baseInputs(25.6f, false), mem);
  TEST_ASSERT_TRUE(d.act);
  TEST_ASSERT_TRUE(d.target.power);
  TEST_ASSERT_EQUAL(int(AcMode::kCool), int(d.target.mode));
  TEST_ASSERT_EQUAL(int(Owner::kThermostat), int(mem.owner));
  TEST_ASSERT_EQUAL(int(Source::kThermostat), int(d.reason));
}

void test_thermo_keeps_inside_band() {
  ThermoConfig th;
  th.enabled = true;
  DryConfig dr;
  AutoTiming tm;
  AutoMemory mem;
  mem.owner = Owner::kThermostat;
  auto d = evaluateAutomation(th, dr, tm, baseInputs(25.2f, true), mem);
  TEST_ASSERT_FALSE(d.act);
  d = evaluateAutomation(th, dr, tm, baseInputs(24.8f, false), mem);
  TEST_ASSERT_FALSE(d.act);
}

void test_thermo_turns_off_below_band() {
  ThermoConfig th;
  th.enabled = true;
  DryConfig dr;
  AutoTiming tm;
  AutoMemory mem;
  mem.owner = Owner::kThermostat;
  auto in = baseInputs(24.4f, true);
  auto d = evaluateAutomation(th, dr, tm, in, mem);
  TEST_ASSERT_TRUE(d.act);
  TEST_ASSERT_FALSE(d.target.power);
}

void test_thermo_heat_mode() {
  ThermoConfig th;
  th.enabled = true;
  th.mode = AcMode::kHeat;
  th.target = 21;
  DryConfig dr;
  AutoTiming tm;
  AutoMemory mem;
  auto d = evaluateAutomation(th, dr, tm, baseInputs(20.4f, false), mem);
  TEST_ASSERT_TRUE(d.act);
  TEST_ASSERT_EQUAL(int(AcMode::kHeat), int(d.target.mode));
  d = evaluateAutomation(th, dr, tm, baseInputs(21.6f, true, AcMode::kHeat), mem);
  TEST_ASSERT_TRUE(d.act);
  TEST_ASSERT_FALSE(d.target.power);
}

void test_short_cycle_protection_defers() {
  ThermoConfig th;
  th.enabled = true;
  DryConfig dr;
  AutoTiming tm;
  AutoMemory mem;
  mem.owner = Owner::kThermostat;
  auto in = baseInputs(24.0f, true);
  in.has_power_change = true;
  in.last_power_change_s = in.now_s - 60;  // 1 dk önce açıldı
  auto d = evaluateAutomation(th, dr, tm, in, mem);
  TEST_ASSERT_FALSE(d.act);
  TEST_ASSERT_TRUE(d.deferred);
  in.last_power_change_s = in.now_s - 400;
  d = evaluateAutomation(th, dr, tm, in, mem);
  TEST_ASSERT_TRUE(d.act);
}

void test_manual_override_blocks() {
  ThermoConfig th;
  th.enabled = true;
  DryConfig dr;
  AutoTiming tm;
  AutoMemory mem;
  auto in = baseInputs(28.0f, false);
  in.has_manual = true;
  in.last_manual_s = in.now_s - 120;
  auto d = evaluateAutomation(th, dr, tm, in, mem);
  TEST_ASSERT_FALSE(d.act);
  TEST_ASSERT_TRUE(d.overridden);
  in.last_manual_s = in.now_s - 4000;
  d = evaluateAutomation(th, dr, tm, in, mem);
  TEST_ASSERT_TRUE(d.act);
}

// ---------------------------------------------------------------- kurutma
void test_dry_turns_on_and_off() {
  ThermoConfig th;
  DryConfig dr;
  dr.enabled = true;
  AutoTiming tm;
  AutoMemory mem;
  auto in = baseInputs(24, false);
  in.hum = 70;
  auto d = evaluateAutomation(th, dr, tm, in, mem);
  TEST_ASSERT_TRUE(d.act);
  TEST_ASSERT_EQUAL(int(AcMode::kDry), int(d.target.mode));
  TEST_ASSERT_EQUAL(int(Owner::kDry), int(mem.owner));

  in.ac = d.target;
  in.hum = 60;  // band içi: devam
  d = evaluateAutomation(th, dr, tm, in, mem);
  TEST_ASSERT_FALSE(d.act);

  in.hum = 54;
  d = evaluateAutomation(th, dr, tm, in, mem);
  TEST_ASSERT_TRUE(d.act);
  TEST_ASSERT_FALSE(d.target.power);
  TEST_ASSERT_EQUAL(int(Owner::kNone), int(mem.owner));
}

void test_dry_does_not_turn_off_manual_ac() {
  ThermoConfig th;
  DryConfig dr;
  dr.enabled = true;
  AutoTiming tm;
  AutoMemory mem;  // sahibi yok: kullanıcı açmış
  auto in = baseInputs(24, true, AcMode::kCool);
  in.hum = 40;
  auto d = evaluateAutomation(th, dr, tm, in, mem);
  TEST_ASSERT_FALSE(d.act);
}

void test_thermo_off_but_humid_switches_to_dry() {
  ThermoConfig th;
  th.enabled = true;
  DryConfig dr;
  dr.enabled = true;
  AutoTiming tm;
  AutoMemory mem;
  mem.owner = Owner::kThermostat;
  auto in = baseInputs(24.0f, true, AcMode::kCool);
  in.hum = 70;
  auto d = evaluateAutomation(th, dr, tm, in, mem);
  TEST_ASSERT_TRUE(d.act);
  TEST_ASSERT_TRUE(d.target.power);
  TEST_ASSERT_EQUAL(int(AcMode::kDry), int(d.target.mode));
  TEST_ASSERT_EQUAL(int(Owner::kDry), int(mem.owner));
}

void test_disabled_automation_releases_owner() {
  ThermoConfig th;  // kapalı
  DryConfig dr;
  AutoTiming tm;
  AutoMemory mem;
  mem.owner = Owner::kThermostat;
  auto d = evaluateAutomation(th, dr, tm, baseInputs(30, true), mem);
  TEST_ASSERT_FALSE(d.act);
  TEST_ASSERT_EQUAL(int(Owner::kNone), int(mem.owner));
}

// ---------------------------------------------------------------- zamanlayıcı
void test_schedule_due_once_per_day() {
  ScheduleEntry e;
  e.days = 1 << 0;  // yalnızca Pazartesi
  e.minute = 23 * 60;
  TEST_ASSERT_TRUE(scheduleDue(e, 0, 23 * 60, 100));
  TEST_ASSERT_FALSE(scheduleDue(e, 1, 23 * 60, 100));
  TEST_ASSERT_FALSE(scheduleDue(e, 0, 23 * 60 + 1, 100));
  e.last_fired_day = 100;
  TEST_ASSERT_FALSE(scheduleDue(e, 0, 23 * 60, 100));
  TEST_ASSERT_TRUE(scheduleDue(e, 0, 23 * 60, 107));
  e.enabled = false;
  TEST_ASSERT_FALSE(scheduleDue(e, 0, 23 * 60, 114));
}

void test_sched_action_parse() {
  SchedAction a;
  TEST_ASSERT_TRUE(parseSchedAction("thermo_on", a));
  TEST_ASSERT_EQUAL(int(SchedAction::kThermoOn), int(a));
  TEST_ASSERT_FALSE(parseSchedAction("thermo", a));
  TEST_ASSERT_FALSE(parseSchedAction("thermo_onx", a));
}

void test_pick_source_average_and_fallback() {
  SourceReading all[kNumSources];
  all[0] = {true, 24, 50};
  all[1] = {true, 26, 60};
  all[2] = {false, 0, 0};
  auto r = pickSource(all, kNumSources, kSourceAverage);
  TEST_ASSERT_TRUE(r.valid);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 25.0, r.temp);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 55.0, r.hum);
  r = pickSource(all, kNumSources, 2);  // geçersiz -> hub
  TEST_ASSERT_TRUE(r.valid);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 24.0, r.temp);
}

// ---------------------------------------------------------------- geçmiş
static HistBin g_bins[HistoryCore::kBins];

void test_history_average_and_query() {
  HistoryCore h;
  h.attach(1, g_bins);
  uint32_t base = 1760000000u - (1760000000u % 300);
  h.add(1, base + 10, 20.0f, 50.0f);
  h.add(1, base + 100, 22.0f, 60.0f);
  h.add(1, base + 600, 30.0f, 40.0f);
  int16_t t[4];
  uint16_t hm[4];
  int n = h.query(1, base, base + 1200, 300, t, hm, 4);
  TEST_ASSERT_EQUAL(4, n);
  TEST_ASSERT_EQUAL(2100, t[0]);
  TEST_ASSERT_EQUAL(5500, hm[0]);
  TEST_ASSERT_EQUAL(kHistNoTemp, t[1]);
  TEST_ASSERT_EQUAL(3000, t[2]);
  // 600 sn adım: iki kutuyu birleştirir
  n = h.query(1, base, base + 1200, 600, t, hm, 4);
  TEST_ASSERT_EQUAL(2, n);
  TEST_ASSERT_EQUAL(2100, t[0]);
  TEST_ASSERT_EQUAL(3000, t[1]);
}

void test_history_wraps_after_week() {
  HistoryCore h;
  h.attach(1, g_bins);
  uint32_t base = 1760000000u - (1760000000u % 300);
  h.add(1, base, 20.0f, 50.0f);
  uint32_t week = HistoryCore::kBins * HistoryCore::kBinS;
  h.add(1, base + week, 25.0f, 55.0f);  // aynı kutuya düşer, eskisinin üzerine yazar
  int16_t t[1];
  uint16_t hm[1];
  h.query(1, base, base + 300, 300, t, hm, 1);
  TEST_ASSERT_EQUAL(kHistNoTemp, t[0]);
  h.query(1, base + week, base + week + 300, 300, t, hm, 1);
  TEST_ASSERT_EQUAL(2500, t[0]);
}

void test_history_dirty_tracking() {
  HistoryCore h;
  h.attach(2, g_bins);
  uint32_t base = 1760000000u;
  h.add(2, base, 20.0f, 50.0f);
  h.add(2, base + 300, 20.0f, 50.0f);
  int seen = h.forEachDirty(2, [](int) {});
  TEST_ASSERT_EQUAL(2, seen);
  TEST_ASSERT_FALSE(h.anyDirty(2));
}

// ---------------------------------------------------------------- ortak kütüphane
void test_lipo_percent() {
  TEST_ASSERT_EQUAL(100, klima::lipoPercent(4250));
  TEST_ASSERT_EQUAL(0, klima::lipoPercent(3000));
  TEST_ASSERT_EQUAL(50, klima::lipoPercent(3840));
  uint8_t p = klima::lipoPercent(3900);
  TEST_ASSERT_TRUE(p > 60 && p < 70);
}

void test_sht31_crc_and_decode() {
  const uint8_t v[2] = {0xBE, 0xEF};
  TEST_ASSERT_EQUAL_HEX8(0x92, klima::sht31Crc(v, 2));  // Sensirion veri sayfası örneği
  // 0x6666 -> -45 + 175*0.4 = 25.0 °C ; 0x8000 -> %50
  uint8_t buf[6] = {0x66, 0x66, 0, 0x80, 0x00, 0};
  buf[2] = klima::sht31Crc(buf, 2);
  buf[5] = klima::sht31Crc(buf + 3, 2);
  float t, h;
  TEST_ASSERT_TRUE(klima::sht31Decode(buf, t, h));
  TEST_ASSERT_FLOAT_WITHIN(0.01, 25.0, t);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 50.0, h);
  buf[5] ^= 1;
  TEST_ASSERT_FALSE(klima::sht31Decode(buf, t, h));
}

void test_protocol_roundtrip() {
  klima::Report rep = {};
  klima::fillHeader(rep.f.h, klima::MsgType::kReport, 42);
  rep.f.n_readings = 3;
  rep.r[0].temp_c100 = 2345;
  size_t len = klima::reportSize(3);
  klima::Report out;
  TEST_ASSERT_TRUE(klima::parseReport(reinterpret_cast<uint8_t*>(&rep), len, out));
  TEST_ASSERT_EQUAL(42, out.f.h.seq);
  TEST_ASSERT_EQUAL(2345, out.r[0].temp_c100);
  // yanlış uzunluk
  TEST_ASSERT_FALSE(klima::parseReport(reinterpret_cast<uint8_t*>(&rep), len - 1, out));
  // yanlış ağ kimliği
  rep.f.h.net_id ^= 1;
  TEST_ASSERT_FALSE(klima::parseReport(reinterpret_cast<uint8_t*>(&rep), len, out));

  klima::Ack ack = {};
  klima::fillHeader(ack.h, klima::MsgType::kAck, 7);
  ack.ack_seq = 42;
  klima::Ack a2;
  TEST_ASSERT_TRUE(klima::parseAck(reinterpret_cast<uint8_t*>(&ack), sizeof(ack), a2));
  TEST_ASSERT_EQUAL(42, a2.ack_seq);
  TEST_ASSERT_EQUAL(60, klima::clampInterval(5));
  TEST_ASSERT_EQUAL(3600, klima::clampInterval(99999));
}

void test_model_helpers() {
  AcMode m;
  TEST_ASSERT_TRUE(parseMode("kuru", m));
  TEST_ASSERT_EQUAL(int(AcMode::kDry), int(m));
  TEST_ASSERT_FALSE(parseMode("xyz", m));
  TEST_ASSERT_FLOAT_WITHIN(0.001, 16.0, clampAcTemp(10));
  TEST_ASSERT_FLOAT_WITHIN(0.001, 24.5, clampAcTemp(24.4f));
  AcState a, b;
  a.power = b.power = false;
  a.temp = 20;
  TEST_ASSERT_TRUE(a.sameAs(b));  // ikisi de kapalı
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_thermo_cool_turns_on_above_band);
  RUN_TEST(test_thermo_keeps_inside_band);
  RUN_TEST(test_thermo_turns_off_below_band);
  RUN_TEST(test_thermo_heat_mode);
  RUN_TEST(test_short_cycle_protection_defers);
  RUN_TEST(test_manual_override_blocks);
  RUN_TEST(test_dry_turns_on_and_off);
  RUN_TEST(test_dry_does_not_turn_off_manual_ac);
  RUN_TEST(test_thermo_off_but_humid_switches_to_dry);
  RUN_TEST(test_disabled_automation_releases_owner);
  RUN_TEST(test_schedule_due_once_per_day);
  RUN_TEST(test_sched_action_parse);
  RUN_TEST(test_pick_source_average_and_fallback);
  RUN_TEST(test_history_average_and_query);
  RUN_TEST(test_history_wraps_after_week);
  RUN_TEST(test_history_dirty_tracking);
  RUN_TEST(test_lipo_percent);
  RUN_TEST(test_sht31_crc_and_decode);
  RUN_TEST(test_protocol_roundtrip);
  RUN_TEST(test_model_helpers);
  return UNITY_END();
}
