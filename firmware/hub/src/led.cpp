#include "led.h"

#include "app_config.h"
#include "pins.h"
#include "runtime.h"
#include "timekeeper.h"
#include "wifi_mgr.h"

namespace hub::led {
namespace {

constexpr uint32_t kPwmFreq = 5000;
constexpr uint8_t kPwmBits = 8;

uint32_t s_flash_green_until = 0;
uint32_t s_flash_red_until = 0;
uint32_t s_ack_until = 0;
uint32_t s_wake_until = 0;
bool s_learning = false;
uint8_t s_factory = 0;
uint32_t s_last_ms = 0;

// Algısal parlaklık için kare eğrisi
uint8_t scale(uint8_t pct) {
  uint32_t v = uint32_t(pct) * 255 / 100;
  return uint8_t(v * v / 255);
}

bool nightNow() {
  if (!g_cfg.led.night) return false;
  struct tm lt;
  if (!timekeeper::local(lt)) return false;
  int m = lt.tm_hour * 60 + lt.tm_min;
  int s = g_cfg.led.night_start, e = g_cfg.led.night_end;
  if (s == e) return false;
  return s < e ? (m >= s && m < e) : (m >= s || m < e);
}

bool anySensorProblem() {
  if (g_cfg.hub_sensor.enabled && !g_rt.hub.sensor_ok) return true;
  for (int i = 0; i < kMaxSensors; i++) {
    if (g_cfg.sensors[i].used && g_rt.sensors[i].seen && !g_rt.sensors[i].online) return true;
  }
  return false;
}

void write(int pin, uint8_t duty) { ledcWrite(pin, duty); }

}  // namespace

void begin() {
  ledcAttach(pins::kLedGreen, kPwmFreq, kPwmBits);
  ledcAttach(pins::kLedRed, kPwmFreq, kPwmBits);
  write(pins::kLedGreen, 0);
  write(pins::kLedRed, 0);
  pinMode(pins::kBuiltinLed, OUTPUT);
  digitalWrite(pins::kBuiltinLed, HIGH);  // kart LED'i kapalı (aktif LOW)
}

void loop() {
  uint32_t now = millis();
  if (now - s_last_ms < 20) return;
  s_last_ms = now;

  uint8_t full = scale(g_cfg.led.brightness ? g_cfg.led.brightness : 1);
  uint8_t dim = full / 4 ? full / 4 : 1;
  bool night = nightNow() && int32_t(now - s_wake_until) >= 0;

  uint8_t g = 0, r = 0;

  // Yeşil: ağ durumu
  if (net::apActive()) {
    bool phase = (now / 500) % 2;
    g = phase ? full : 0;
    r = phase ? 0 : full;
  } else if (net::connected()) {
    g = dim;
  } else {
    g = ((now / 1000) % 2) ? dim : 0;
  }

  // Kırmızı: uyarılar
  if (!net::apActive()) {
    if (g_rt.hub.battery_critical) {
      r = full;
    } else if (anySensorProblem() && (now % 5000) < 120) {
      r = full;
    }
  }

  if (night) {
    g = 0;
    if (!g_rt.hub.battery_critical) r = 0;
  } else {
    if (int32_t(s_flash_green_until - now) > 0) g = full;
    if (int32_t(s_flash_red_until - now) > 0) r = full;
  }

  // Gece de görünen durumlar
  if (s_learning) r = ((now / 100) % 2) ? full : 0;
  if (int32_t(s_ack_until - now) > 0) {
    bool on = ((s_ack_until - now) / 120) % 2;
    g = on ? full : 0;
    r = on ? full : 0;
  }
  if (s_factory) {
    uint32_t period = s_factory >= 3 ? 80 : (s_factory == 2 ? 160 : 320);
    bool on = (now / period) % 2;
    g = on ? full : 0;
    r = on ? 0 : full;
  }

  write(pins::kLedGreen, g);
  write(pins::kLedRed, r);
}

void flashGreen() { s_flash_green_until = millis() + 150; }
void flashRed() { s_flash_red_until = millis() + 400; }
void setLearning(bool on) { s_learning = on; }
void wake(uint32_t ms) { s_wake_until = millis() + ms; }
void setFactoryWarning(uint8_t level) { s_factory = level; }
void ack() { s_ack_until = millis() + 480; }

}  // namespace hub::led
