// Saat: NTP (internet varken) veya telefondan (time.set). İnternet kopsa da
// ESP32'nin iç saati çalışmaya devam eder.
#pragma once

#include <Arduino.h>
#include <time.h>

namespace hub::timekeeper {

void begin();
void loop();
bool valid();
uint32_t now();                  // Unix epoch (geçersizse 0)
bool local(struct tm& out);      // yerel saat
void setEpoch(uint32_t epoch, const char* source);
void applyTimezone();
const char* lastSource();        // "ntp", "app", "-" ...

}  // namespace hub::timekeeper
