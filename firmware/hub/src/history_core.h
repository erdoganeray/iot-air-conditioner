// 7 günlük sıcaklık/nem geçmişi: kaynak başına 5 dakikalık kutular (2016 adet).
// Bellek ayırma ve dosyaya yazma dışarıdan yapılır; çekirdek saf C++'tır.
#pragma once

#include <stdint.h>
#include <string.h>

#include "model.h"

namespace hub {

#pragma pack(push, 1)
struct HistBin {
  uint32_t id;      // epoch / kBinS; 0 = boş
  int16_t t100;     // ortalama °C x100
  uint16_t h100;    // ortalama %RH x100
  uint8_t n;        // ortalamaya giren ölçüm sayısı
  uint8_t reserved;
};
#pragma pack(pop)
static_assert(sizeof(HistBin) == 10, "HistBin boyutu");

constexpr int16_t kHistNoTemp = INT16_MIN;
constexpr uint16_t kHistNoHum = 0xFFFF;

class HistoryCore {
 public:
  static constexpr uint32_t kBinS = 300;  // 5 dk
  static constexpr int kBins = 2016;      // 7 gün
  static constexpr int kDirtyWords = (kBins + 31) / 32;

  // bins[src] en az kBins elemanlık bellek göstermeli (nullptr = kaynak kapalı)
  void attach(int src, HistBin* mem) {
    if (src < 0 || src >= kNumSources) return;
    bins_[src] = mem;
    if (mem) memset(mem, 0, sizeof(HistBin) * kBins);
    memset(dirty_[src], 0, sizeof(dirty_[src]));
  }

  bool available(int src) const { return src >= 0 && src < kNumSources && bins_[src]; }
  HistBin* raw(int src) { return available(src) ? bins_[src] : nullptr; }

  void add(int src, uint32_t epoch, float t, float h) {
    if (!available(src) || epoch < 1000000000u) return;
    uint32_t id = epoch / kBinS;
    int idx = int(id % kBins);
    HistBin& b = bins_[src][idx];
    int32_t t100 = int32_t(t * 100.0f + (t >= 0 ? 0.5f : -0.5f));
    int32_t h100 = int32_t(h * 100.0f + 0.5f);
    if (t100 < -32000) t100 = -32000;
    if (t100 > 32000) t100 = 32000;
    if (h100 < 0) h100 = 0;
    if (h100 > 10000) h100 = 10000;
    if (b.id != id || b.n == 0) {
      b.id = id;
      b.t100 = int16_t(t100);
      b.h100 = uint16_t(h100);
      b.n = 1;
    } else {
      uint32_t n = b.n;
      b.t100 = int16_t((int32_t(b.t100) * int32_t(n) + t100) / int32_t(n + 1));
      b.h100 = uint16_t((uint32_t(b.h100) * n + uint32_t(h100)) / (n + 1));
      if (b.n < 255) b.n++;
    }
    dirty_[src][idx / 32] |= (1u << (idx % 32));
  }

  void clear(int src) {
    if (!available(src)) return;
    memset(bins_[src], 0, sizeof(HistBin) * kBins);
    for (int i = 0; i < kDirtyWords; i++) dirty_[src][i] = 0xFFFFFFFFu;
  }

  // [from, to) aralığını step saniyelik adımlarla özetler.
  // Eksik adımlar kHistNoTemp / kHistNoHum olarak doldurulur. Dönüş: nokta sayısı.
  int query(int src, uint32_t from, uint32_t to, uint32_t step, int16_t* out_t, uint16_t* out_h,
            int max_points) const {
    if (!available(src) || step < kBinS || to <= from || max_points <= 0) return 0;
    step = (step / kBinS) * kBinS;
    from = (from / step) * step;
    int count = 0;
    for (uint32_t t0 = from; t0 < to && count < max_points; t0 += step, count++) {
      int32_t st = 0, sh = 0;
      int k = 0;
      for (uint32_t t = t0; t < t0 + step; t += kBinS) {
        uint32_t id = t / kBinS;
        const HistBin& b = bins_[src][id % kBins];
        if (b.n == 0 || b.id != id) continue;
        st += b.t100;
        sh += b.h100;
        k++;
      }
      if (k == 0) {
        out_t[count] = kHistNoTemp;
        out_h[count] = kHistNoHum;
      } else {
        out_t[count] = int16_t(st / k);
        out_h[count] = uint16_t(sh / k);
      }
    }
    return count;
  }

  // Kirli kutuları gezmek için: fn(index) çağrılır ve bayrak temizlenir.
  template <typename F>
  int forEachDirty(int src, F fn) {
    if (!available(src)) return 0;
    int n = 0;
    for (int w = 0; w < kDirtyWords; w++) {
      uint32_t bits = dirty_[src][w];
      if (!bits) continue;
      for (int b = 0; b < 32; b++) {
        if (!(bits & (1u << b))) continue;
        int idx = w * 32 + b;
        if (idx < kBins) {
          fn(idx);
          n++;
        }
      }
      dirty_[src][w] = 0;
    }
    return n;
  }

  bool anyDirty(int src) const {
    if (!available(src)) return false;
    for (int w = 0; w < kDirtyWords; w++)
      if (dirty_[src][w]) return true;
    return false;
  }

  void markAllDirty(int src) {
    for (int i = 0; i < kDirtyWords; i++) dirty_[src][i] = 0xFFFFFFFFu;
  }

 private:
  HistBin* bins_[kNumSources] = {};
  uint32_t dirty_[kNumSources][kDirtyWords] = {};
};

}  // namespace hub
