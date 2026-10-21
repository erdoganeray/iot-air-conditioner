// Klima IR kontrolü: protokol tabanlı (IRac) veya öğrenilmiş ham komutlarla.
// Ayrıntı: docs/MIMARI.md §6
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "model.h"

namespace hub::ir {

void begin();
void loop();

// Klimayı istenen duruma getirir: IR gönderir ve çalışma zamanı durumunu günceller
bool apply(const AcState& s, Source src, String& err);
// Öğrenilmiş ham komutu gönderir (rolü varsa klima durumu da güncellenir)
bool sendCommand(int id, Source src, String& err);

// Öğrenme: kumandadan gelecek ilk sinyali yakalar (30 sn)
bool startLearning(const String& name, const String& role, bool force_raw, String& err);
void cancelLearning();
bool learning();
uint32_t learnRemainingS();

void commandsJson(JsonArray arr);
bool updateCommand(int id, JsonObjectConst o, String& err);
bool deleteCommand(int id);

void protocolsJson(JsonArray arr);
void modelsJson(const char* proto, JsonArray arr);
bool setProtocol(const char* proto, int model, String& err);
bool setRawMode(bool raw, String& err);
void infoJson(JsonObject o);
bool ready();

}  // namespace hub::ir
