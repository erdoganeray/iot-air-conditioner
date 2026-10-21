// LiPo pil voltajından yaklaşık doluluk yüzdesi.
// Eğri, hafif yük altında tek hücreli LiPo için tipik değerlerdir.
#pragma once

#include <stdint.h>

namespace klima {

struct VoltPct {
  uint16_t mv;
  uint8_t pct;
};

// Yüksekten düşüğe sıralı
constexpr VoltPct kLipoCurve[] = {
    {4200, 100}, {4150, 95}, {4110, 90}, {4080, 85}, {4020, 80}, {3980, 75},
    {3950, 70},  {3910, 65}, {3870, 60}, {3850, 55}, {3840, 50}, {3820, 45},
    {3800, 40},  {3790, 35}, {3770, 30}, {3750, 25}, {3730, 20}, {3710, 15},
    {3690, 10},  {3610, 5},  {3270, 0},
};

inline uint8_t lipoPercent(uint16_t mv) {
  constexpr int n = sizeof(kLipoCurve) / sizeof(kLipoCurve[0]);
  if (mv >= kLipoCurve[0].mv) return 100;
  if (mv <= kLipoCurve[n - 1].mv) return 0;
  for (int i = 1; i < n; i++) {
    if (mv >= kLipoCurve[i].mv) {
      const VoltPct& hi = kLipoCurve[i - 1];
      const VoltPct& lo = kLipoCurve[i];
      uint32_t span_mv = hi.mv - lo.mv;
      uint32_t span_pct = hi.pct - lo.pct;
      return static_cast<uint8_t>(lo.pct + (uint32_t(mv - lo.mv) * span_pct + span_mv / 2) / span_mv);
    }
  }
  return 0;
}

// Eşikler (mV)
constexpr uint16_t kBattLowMv = 3550;       // "pil zayıf" uyarısı (~%5)
constexpr uint16_t kBattCriticalMv = 3400;  // hub: derin uyku; sensör: uzun uyku
constexpr uint16_t kBattPresentMinMv = 2500; // bunun altı "pil yok / okunamadı"

}  // namespace klima
