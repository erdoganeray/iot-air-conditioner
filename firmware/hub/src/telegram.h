// Telegram botu: dışarıdan komut + bildirim. Ağ işlemleri ayrı bir görevde yapılır;
// komutlar ana döngüde işlenir.
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

namespace hub::telegram {

void begin();
void loop();                              // ana döngü: gelen komutları işler
void syncConfig();                        // ayar değişince çağrılır
void queueBroadcast(const String& text);  // yetkili tüm sohbetlere
void queueTest();
void statusJson(JsonObject o);

}  // namespace hub::telegram
