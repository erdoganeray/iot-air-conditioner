#include "touch.h"

#include "led.h"
#include "pins.h"

namespace hub::touch {
namespace {

constexpr uint32_t kDebounceMs = 30;
constexpr uint32_t kTapMaxMs = 600;
constexpr uint32_t kDoubleGapMs = 400;
constexpr uint32_t kLongMs = 3000;
constexpr uint32_t kVeryLongMs = 10000;

Handler s_handler = nullptr;
bool s_stable = false;      // debounce edilmiş durum
bool s_raw_last = false;
uint32_t s_raw_change_ms = 0;
uint32_t s_press_start = 0;
uint32_t s_last_tap_ms = 0;
bool s_tap_pending = false;
bool s_long_signaled = false;
bool s_very_long_fired = false;

void fire(Gesture g) {
  if (s_handler) s_handler(g);
}

}  // namespace

void begin(Handler h) {
  s_handler = h;
  pinMode(pins::kTouch, INPUT_PULLDOWN);
}

void loop() {
  uint32_t now = millis();
  bool raw = digitalRead(pins::kTouch) == HIGH;
  if (raw != s_raw_last) {
    s_raw_last = raw;
    s_raw_change_ms = now;
  }
  if (raw != s_stable && now - s_raw_change_ms >= kDebounceMs) {
    s_stable = raw;
    if (s_stable) {
      s_press_start = now;
      s_long_signaled = false;
      s_very_long_fired = false;
      led::wake(5000);
    } else {
      uint32_t held = now - s_press_start;
      led::setFactoryWarning(0);
      if (s_very_long_fired) {
        // zaten işlendi
      } else if (held >= kLongMs) {
        s_tap_pending = false;
        fire(Gesture::kLong);
      } else if (held <= kTapMaxMs) {
        if (s_tap_pending && now - s_last_tap_ms <= kDoubleGapMs) {
          s_tap_pending = false;
          fire(Gesture::kDoubleTap);
        } else {
          s_tap_pending = true;
          s_last_tap_ms = now;
        }
      }
    }
  }

  if (s_stable) {
    uint32_t held = now - s_press_start;
    if (held >= kLongMs && !s_long_signaled) {
      s_long_signaled = true;
      led::ack();
    }
    if (held >= 5000 && !s_very_long_fired) {
      led::setFactoryWarning(held >= 8000 ? 3 : (held >= 6500 ? 2 : 1));
    }
    if (held >= kVeryLongMs && !s_very_long_fired) {
      s_very_long_fired = true;
      led::setFactoryWarning(0);
      fire(Gesture::kVeryLong);
    }
  }

  // Çift dokunma için bekleme süresi doldu: tek dokunma
  if (s_tap_pending && !s_stable && now - s_last_tap_ms > kDoubleGapMs) {
    s_tap_pending = false;
    fire(Gesture::kTap);
  }
}

bool pressed() { return s_stable; }

}  // namespace hub::touch
