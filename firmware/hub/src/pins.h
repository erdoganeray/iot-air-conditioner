// Hub pin eşlemesi (Seeed XIAO ESP32-S3). Ayrıntı: docs/MIMARI.md §3.1
#pragma once

namespace pins {
constexpr int kBattAdc = 1;    // D0 — pil bölücü 220k/220k
constexpr int kUsbSense = 2;   // D1 — 5V bölücü 100k/150k (RTC GPIO, uyandırma)
constexpr int kTouch = 3;      // D2 — TTP223 çıkışı (aktif HIGH)
constexpr int kIrRecv = 4;     // D3 — VS1838B çıkışı
constexpr int kSda = 5;        // D4
constexpr int kScl = 6;        // D5
constexpr int kIrSend = 7;     // D8 — S8050 bazı (150 Ω)
constexpr int kLedGreen = 8;   // D9
constexpr int kLedRed = 9;     // D10
constexpr int kBuiltinLed = 21;  // Kart üzerindeki turuncu LED (aktif LOW)
}  // namespace pins

// Bölücü oranları
constexpr float kBattDividerRatio = 2.0f;            // 220k / 220k
constexpr float kUsbDividerRatio = 250.0f / 150.0f;  // 100k üst, 150k alt
