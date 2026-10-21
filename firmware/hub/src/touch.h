// TTP223 dokunmatik buton hareketleri: tek, çift, uzun (3 sn), çok uzun (10 sn)
#pragma once

#include <Arduino.h>

namespace hub::touch {

enum class Gesture : uint8_t { kTap, kDoubleTap, kLong, kVeryLong };
using Handler = void (*)(Gesture g);

void begin(Handler h);
void loop();
bool pressed();

}  // namespace hub::touch
