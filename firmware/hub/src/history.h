// 7 günlük sıcaklık/nem geçmişi ve klima olay günlüğü (LittleFS'e kaydedilir)
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "model.h"

namespace hub::history {

void begin();
void loop();       // 30 dk'da bir değişen kutuları dosyaya yazar
void saveNow();

void add(int src, uint32_t epoch, float temp, float hum);
void clearSource(int src);

// Son `hours` saati `step_min` dakikalık adımlarla döndürür:
// {"src":1,"t0":..., "step":1800, "temp":[245,...], "hum":[553,...]}  (değer x10, eksik = null)
bool queryJson(int src, uint32_t hours, uint32_t step_min, JsonObject out);

void logAc(const AcState& s, Source src);
// Son kaydedilen klima durumu (yeniden başlatmada geri yüklemek için)
bool lastAc(AcState& s, Source& src, uint32_t& epoch);
void eventsJson(JsonArray out, int max_items);

}  // namespace hub::history
