// JSON komut API'si. Bluetooth (BLE), WebSocket ve USB seri port aynı komutları kullanır.
// Komut listesi: docs/API.md
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

namespace hub::api {

enum class Transport : uint8_t { kBle = 1, kWs = 2, kSerial = 3 };

void begin();
void loop();

// Herhangi bir görevden çağrılabilir (kopya alınır). data == nullptr: istemci bağlantısı koptu
bool enqueue(Transport t, uint32_t client, const char* data, size_t len);

// Yetkili tüm istemcilere {"evt":evt,"data":...} gönderir
void broadcastEvent(const char* evt, JsonVariantConst data);

// Tam durum özeti (uygulama ve Telegram /durum için)
void buildStatus(JsonObject out);

int sessionCount();

// Klima durumunu kısmi JSON ile değiştirir ({"power":true,"temp":24,"mode":"cool"...}).
// Telegram ve dokunmatik buton da bunu kullanır.
bool setAc(JsonObjectConst changes, uint8_t source, String& err);

}  // namespace hub::api
