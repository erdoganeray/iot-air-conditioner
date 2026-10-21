// Bluetooth (BLE) arayüzü. Nordic UART Service (NUS) UUID'leri kullanılır:
// telefon uygulaması dışında "Serial Bluetooth Terminal" gibi hazır uygulamalarla da
// JSON komutları satır satır gönderilebilir.
#pragma once

#include <Arduino.h>

namespace hub::ble {

void begin();
void loop();
bool active();
int clients();
void send(uint32_t conn, const String& msg);  // ana döngüden
void setLowPower(bool low);
void requestOn(uint32_t minutes);  // "gerektiğinde" modunda geçici olarak aç

}  // namespace hub::ble
