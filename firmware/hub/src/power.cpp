#include "power.h"

#include <esp_sleep.h>

#include "app_config.h"
#include "battery.h"
#include "history.h"
#include "log.h"
#include "notify.h"
#include "pins.h"
#include "runtime.h"

namespace hub::power {
namespace {

constexpr uint32_t kPeriodMs = 2000;
constexpr uint32_t kUsbThresholdMv = 4000;     // bölücü sonrası 5V ≈ 3000 mV -> x1.667
constexpr uint64_t kCriticalWakeUs = 30ULL * 60ULL * 1000000ULL;
constexpr int kDebounceCount = 3;

RTC_DATA_ATTR bool rtc_critical_sleep = false;

uint32_t s_last_ms = 0;
int s_usb_votes = 0;
int s_crit_votes = 0;
bool s_usb_initialized = false;
uint32_t s_avg_mv = 0;

uint32_t readMv(int pin, int samples = 16) {
  uint32_t sum = 0;
  for (int i = 0; i < samples; i++) sum += analogReadMilliVolts(pin);
  return sum / uint32_t(samples);
}

uint16_t readBatteryMv() { return uint16_t(float(readMv(pins::kBattAdc)) * kBattDividerRatio + 0.5f); }
bool readUsb() { return float(readMv(pins::kUsbSense, 8)) * kUsbDividerRatio > kUsbThresholdMv; }

void setupAdc() {
  analogReadResolution(12);
  analogSetPinAttenuation(pins::kBattAdc, ADC_11db);
  analogSetPinAttenuation(pins::kUsbSense, ADC_11db);
}

[[noreturn]] void sleepNow() {
  rtc_critical_sleep = true;
  esp_sleep_enable_ext0_wakeup(static_cast<gpio_num_t>(pins::kUsbSense), 1);
  esp_sleep_enable_timer_wakeup(kCriticalWakeUs);
  Serial.flush();
  esp_deep_sleep_start();
  for (;;) {
  }
}

}  // namespace

void earlyCheck() {
  if (!rtc_critical_sleep) return;
  setupAdc();
  bool usb = readUsb();
  uint16_t mv = readBatteryMv();
  if (!usb && mv >= klima::kBattPresentMinMv && mv < klima::kBattCriticalMv + 100) {
    sleepNow();  // hâlâ kritik: tekrar uyu
  }
  rtc_critical_sleep = false;
}

void begin() {
  setupAdc();
  HubRuntime& h = g_rt.hub;
  h.usb = readUsb();
  h.batt_mv = readBatteryMv();
  h.battery_present = h.batt_mv >= klima::kBattPresentMinMv;
  h.batt_pct = h.battery_present ? klima::lipoPercent(h.batt_mv) : 0;
  s_avg_mv = h.batt_mv;
  s_usb_initialized = true;
  LOGI("Guc: %s, pil %u mV (%%%u)", h.usb ? "adaptor" : "PIL", h.batt_mv, h.batt_pct);
}

void loop() {
  uint32_t now = millis();
  if (now - s_last_ms < kPeriodMs) return;
  s_last_ms = now;
  HubRuntime& h = g_rt.hub;

  uint16_t mv = readBatteryMv();
  // Basit alçak geçiren süzgeç
  s_avg_mv = (s_avg_mv * 3 + mv) / 4;
  bool present = s_avg_mv >= klima::kBattPresentMinMv;
  uint8_t pct = present ? klima::lipoPercent(uint16_t(s_avg_mv)) : 0;
  if (present != h.battery_present || pct != h.batt_pct) markStateDirty();
  h.batt_mv = uint16_t(s_avg_mv);
  h.battery_present = present;
  h.batt_pct = pct;

  bool usb = readUsb();
  if (usb != h.usb) {
    if (++s_usb_votes >= kDebounceCount) {
      s_usb_votes = 0;
      h.usb = usb;
      markStateDirty();
      char msg[120];
      if (usb) {
        snprintf(msg, sizeof(msg), "🔌 Hub adaptöre bağlandı (pil %%%u).", pct);
      } else {
        snprintf(msg, sizeof(msg),
                 "⚡ Hub'ın adaptörü çekildi veya elektrik kesildi. Pille çalışıyor (%%%u).", pct);
      }
      notify::send(notify::Kind::kPower, msg);
    }
  } else {
    s_usb_votes = 0;
  }

  bool critical = present && !h.usb && h.batt_mv < klima::kBattCriticalMv;
  // %10'da uyar, %15'e çıkınca (veya adaptör takılınca) uyarıyı kaldır
  bool low = present && !h.usb && (h.batt_pct <= 10 || (h.battery_critical && h.batt_pct < 15));
  if (low != h.battery_critical && !critical) {
    h.battery_critical = low;
    markStateDirty();
    if (low) notify::send(notify::Kind::kBattery, "🔋 Hub pili %10'un altında. Adaptörü takın.");
  }
  if (critical) {
    h.battery_critical = true;
    if (++s_crit_votes >= kDebounceCount) {
      LOGW("Pil kritik (%u mV): veriler kaydediliyor, derin uyku", h.batt_mv);
      notify::send(notify::Kind::kBattery,
                   "🪫 Hub pili bitti, uyku moduna geçiyor. Adaptör takılınca kendiliğinden açılır.");
      config::saveNow();
      history::saveNow();
      delay(3000);  // bildirimin gönderilmesi için kısa süre
      sleepNow();
    }
  } else {
    s_crit_votes = 0;
  }
}

}  // namespace hub::power
