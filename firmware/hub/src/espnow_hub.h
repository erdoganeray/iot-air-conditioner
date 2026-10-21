// ESP-NOW alıcısı: sensör raporlarını alır, hemen ACK gönderir (ayrı görevde),
// raporları ana döngüye kuyrukla aktarır.
#pragma once

#include <Arduino.h>

#include "klima_protocol.h"

namespace hub::espnow {

bool begin();  // WiFi başlatıldıktan sonra çağrılır
void end();
void loop();   // ana döngü: kuyruktaki raporları işler

// Ayarlardaki sensör listesini (MAC, aralık) ESP-NOW görevinin kullandığı tabloya kopyalar
void syncDirectory();

struct Stats {
  uint32_t rx_frames = 0;
  uint32_t rx_invalid = 0;
  uint32_t rx_unknown = 0;   // kayıtsız ve kabul edilmeyen sensör
  uint32_t acks_sent = 0;
  uint32_t ack_fail = 0;
  uint32_t queue_full = 0;
};
Stats stats();

}  // namespace hub::espnow
