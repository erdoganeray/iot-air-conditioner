// Hub pin eşlemesi (Seeed XIAO ESP32-S3). PCB ile aynıdır: pcb/tools/boards.py
// Pin görevleri, tek yüzlü PCB'de izler kesişmeyecek şekilde seçildi.
// Ayrıntı: docs/MIMARI.md §3.1
#pragma once

namespace pins {
// Sol sütun (USB arkada iken)
constexpr int kIrSend = 44;    // D7 — S8050 bazı (150 Ω, 10k pull-down). Açılışta giriş: IR kapalı
constexpr int kLedRed = 7;     // D8 — kırmızı LED (470 Ω)
constexpr int kBattAdc = 8;    // D9 — pil bölücü 220k/220k (ADC1_CH7)
constexpr int kUsbSense = 9;   // D10 — 5V bölücü 100k/150k (ADC1_CH8, RTC GPIO: uyandırma)
// Sağ sütun
constexpr int kLedGreen = 43;  // D6 — yeşil LED (470 Ω). Açılışta ROM log'u nedeniyle kısa titrer
constexpr int kIrRecv = 6;     // D5 — VS1838B çıkışı
constexpr int kTouch = 5;      // D4 — TTP223 çıkışı (aktif HIGH)
constexpr int kSda = 4;        // D3 — I²C SDA (SHT31)
constexpr int kScl = 3;        // D2 — I²C SCL (SHT31)
constexpr int kBuiltinLed = 21;  // Kart üzerindeki turuncu LED (aktif LOW)
}  // namespace pins

// Bölücü oranları
constexpr float kBattDividerRatio = 2.0f;            // 220k / 220k
constexpr float kUsbDividerRatio = 250.0f / 150.0f;  // 100k üst, 150k alt
