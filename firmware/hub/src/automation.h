// Otomasyonun donanım tarafı: periyodik termostat/nem değerlendirmesi ve zamanlayıcılar
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

namespace hub::automation {

void loop();
void statusJson(JsonObject o);
// Ayar değişince hemen yeniden değerlendir
void kick();

}  // namespace hub::automation
