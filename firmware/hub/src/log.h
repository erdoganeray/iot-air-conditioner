// Basit seri günlük makroları. USB seri portundan (115200) okunur.
#pragma once

#include <Arduino.h>

#define LOG_PRINT(level, fmt, ...) \
  Serial.printf("[%8lu] " level " " fmt "\n", (unsigned long)millis(), ##__VA_ARGS__)

#define LOGI(fmt, ...) LOG_PRINT("I", fmt, ##__VA_ARGS__)
#define LOGW(fmt, ...) LOG_PRINT("W", fmt, ##__VA_ARGS__)
#define LOGE(fmt, ...) LOG_PRINT("E", fmt, ##__VA_ARGS__)
