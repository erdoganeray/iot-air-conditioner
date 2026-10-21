// Yerel web sunucusu: uygulama sayfası (/), WebSocket API (/ws), OTA (/update),
// kurulum ağı için captive portal.
#pragma once

#include <Arduino.h>

namespace hub::web {

void begin();
void loop();
void send(uint32_t client, const String& msg);
int clients();

}  // namespace hub::web
