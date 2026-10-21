// Kırmızı/yeşil durum LED'leri. Desenler: docs/MIMARI.md §9
#pragma once

#include <Arduino.h>

namespace hub::led {

void begin();
void loop();
void flashGreen();             // IR gönderildi
void flashRed();               // hata
void setLearning(bool on);
void wake(uint32_t ms);        // gece modunda geçici olarak durum göster
void setFactoryWarning(uint8_t level);  // 0 = yok, 1..3 hızlanan uyarı
void ack();                    // uzun basma algılandı: çift flaş

}  // namespace hub::led
