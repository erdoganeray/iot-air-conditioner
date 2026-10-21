// Bildirimler: Telegram'a (ayarlara göre) ve bağlı uygulamalara iletilir.
// İnternet yokken Telegram mesajları sıraya alınır.
#pragma once

#include <Arduino.h>

namespace hub::notify {

enum class Kind : uint8_t {
  kInfo = 0,   // her zaman gönderilir
  kBattery,    // pil zayıf
  kOffline,    // sensör yanıt vermiyor / geri geldi
  kPower,      // adaptör çekildi / takıldı
  kWifi,       // WiFi kesintisi bitti
  kAuto,       // otomasyon klimayı açtı/kapattı
};

void send(Kind kind, const char* text);

}  // namespace hub::notify
