// Sıcaklık/nem kaynakları: hub'ın kendi SHT31'i (kaynak 0) ve uzak sensörler (1..4)
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "automation_logic.h"
#include "klima_protocol.h"

namespace hub::sensors {

void begin();
void loop();

// ESP-NOW raporunun ana döngüde işlenmesi
void onReport(int slot, const uint8_t* mac, int8_t rssi, const klima::Report& r, bool is_new);

// Otomasyon için güncel ve taze ölçümler (kaynak 0..4)
void snapshot(SourceReading out[kNumSources]);

void toJson(JsonArray arr);
bool configure(int src, JsonObjectConst o, String& err);
bool remove(int slot);
String name(int src);
bool exists(int src);
bool online(int src);

}  // namespace hub::sensors
