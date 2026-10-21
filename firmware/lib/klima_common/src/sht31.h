// Basit SHT31 (SHT3x) sıcaklık/nem sürücüsü.
// Tek seferlik, yüksek tekrarlanabilirlikli ölçüm; CRC denetimli.
#pragma once

#include <stdint.h>

#ifdef ARDUINO
#include <Wire.h>
#endif

namespace klima {

// CRC-8, polinom 0x31, başlangıç 0xFF (Sensirion veri sayfası)
inline uint8_t sht31Crc(const uint8_t* data, int len) {
  uint8_t crc = 0xFF;
  for (int i = 0; i < len; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++) crc = (crc & 0x80) ? uint8_t((crc << 1) ^ 0x31) : uint8_t(crc << 1);
  }
  return crc;
}

inline float sht31RawToTemp(uint16_t raw) { return -45.0f + 175.0f * float(raw) / 65535.0f; }
inline float sht31RawToHum(uint16_t raw) { return 100.0f * float(raw) / 65535.0f; }

// 6 baytlık yanıtı çözer: [T_msb, T_lsb, T_crc, H_msb, H_lsb, H_crc]
inline bool sht31Decode(const uint8_t* b, float& t, float& h) {
  if (sht31Crc(b, 2) != b[2] || sht31Crc(b + 3, 2) != b[5]) return false;
  uint16_t rt = uint16_t(b[0]) << 8 | b[1];
  uint16_t rh = uint16_t(b[3]) << 8 | b[4];
  t = sht31RawToTemp(rt);
  h = sht31RawToHum(rh);
  if (h > 100.0f) h = 100.0f;
  if (h < 0.0f) h = 0.0f;
  return true;
}

#ifdef ARDUINO
class Sht31 {
 public:
  explicit Sht31(uint8_t addr = 0x44) : addr_(addr) {}

  bool begin(TwoWire& wire) {
    wire_ = &wire;
    return probe();
  }

  bool probe() {
    if (!wire_) return false;
    wire_->beginTransmission(addr_);
    return wire_->endTransmission() == 0;
  }

  bool softReset() { return command(0x30A2); }

  // Ölçüm ~15 ms sürer. Başarısızsa false döner.
  bool measure(float& temp_c, float& hum_pct) {
    if (!command(0x2400)) return false;  // yüksek tekrarlanabilirlik, clock stretching yok
    delay(16);
    uint8_t buf[6];
    if (wire_->requestFrom(int(addr_), 6) != 6) return false;
    for (int i = 0; i < 6; i++) buf[i] = uint8_t(wire_->read());
    return sht31Decode(buf, temp_c, hum_pct);
  }

 private:
  bool command(uint16_t cmd) {
    if (!wire_) return false;
    wire_->beginTransmission(addr_);
    wire_->write(uint8_t(cmd >> 8));
    wire_->write(uint8_t(cmd & 0xFF));
    return wire_->endTransmission() == 0;
  }

  uint8_t addr_;
  TwoWire* wire_ = nullptr;
};
#endif

}  // namespace klima
