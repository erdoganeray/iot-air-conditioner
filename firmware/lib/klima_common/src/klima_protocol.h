// Hub <-> sensör ESP-NOW paket tanımları.
// Bu dosya hem hub hem sensör yazılımı tarafından kullanılır; iki tarafta da
// aynı sürüm olmalıdır. Yapılar "packed" olduğundan ESP32-C3 (RISC-V) ve
// ESP32-S3 (Xtensa) arasında bayt düzeni birebir aynıdır (ikisi de little-endian).
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace klima {

// "KL" — paket başındaki sihirli sayı
constexpr uint16_t kMagic = 0x4C4B;
constexpr uint8_t kProtocolVersion = 1;

// Ağ kimliği: aynı binada başka bir "Klima" sistemi varsa paketler karışmasın.
// İstenirse iki tarafta da -DKLIMA_NET_ID=0x... ile değiştirilebilir.
#ifndef KLIMA_NET_ID
#define KLIMA_NET_ID 0x4B4C4D41u
#endif
constexpr uint32_t kNetId = KLIMA_NET_ID;

// ESP-NOW v1 en fazla 250 bayt taşır.
constexpr size_t kEspNowMaxPayload = 250;

enum class MsgType : uint8_t {
  kReport = 1,  // sensör -> hub
  kAck = 2,     // hub -> sensör
};

// Rapor bayrakları
enum ReportFlags : uint8_t {
  kFlagUsbPower = 1 << 0,    // USB takılı (şarj oluyor)
  kFlagSensorError = 1 << 1, // SHT31 okunamadı (ölçüm listesi boş olabilir)
  kFlagFirstBoot = 1 << 2,   // soğuk açılış (pil takıldı / reset)
  kFlagLowBattery = 1 << 3,  // pil zayıf
  kFlagCritical = 1 << 4,    // pil kritik, sensör uzun uykuya geçiyor
};

// ACK bayrakları
enum AckFlags : uint8_t {
  kAckAccepted = 1 << 0,  // hub sensörü kayıtlı tutuyor
  kAckTimeValid = 1 << 1, // epoch alanı geçerli (hub saati NTP/telefon ile ayarlı)
};

#pragma pack(push, 1)

struct MsgHeader {
  uint16_t magic;
  uint8_t version;
  MsgType type;
  uint32_t net_id;
  uint16_t seq;
};
static_assert(sizeof(MsgHeader) == 10, "MsgHeader boyutu");

// Tek ölçüm. age_s: gönderim anına göre kaç saniye önce ölçüldü.
struct Reading {
  uint32_t age_s;
  int16_t temp_c100;  // °C x 100  (ör. 2345 = 23.45 °C)
  uint16_t hum_c100;  // %RH x 100
};
static_assert(sizeof(Reading) == 8, "Reading boyutu");

struct ReportFixed {
  MsgHeader h;
  uint16_t batt_mv;
  uint8_t batt_pct;
  uint8_t flags;
  uint16_t interval_s;  // sensörün şu an kullandığı aralık
  uint32_t boot_count;
  uint8_t fw_major;
  uint8_t fw_minor;
  uint8_t n_readings;   // ardından gelen Reading sayısı (en yenisi ilk sırada)
};
static_assert(sizeof(ReportFixed) == 23, "ReportFixed boyutu");

constexpr size_t kMaxReadingsPerReport =
    (kEspNowMaxPayload - sizeof(ReportFixed)) / sizeof(Reading);  // = 28
static_assert(kMaxReadingsPerReport >= 25, "Rapor kapasitesi");

struct Report {
  ReportFixed f;
  Reading r[kMaxReadingsPerReport];
};

struct Ack {
  MsgHeader h;
  uint16_t ack_seq;     // onaylanan raporun seq değeri
  uint16_t interval_s;  // sensörün bundan sonra kullanacağı aralık
  uint32_t epoch;       // Unix zamanı (kAckTimeValid ise)
  uint8_t channel;      // hub'ın WiFi kanalı
  uint8_t flags;
};
static_assert(sizeof(Ack) == 20, "Ack boyutu");

#pragma pack(pop)

inline void fillHeader(MsgHeader& h, MsgType type, uint16_t seq) {
  h.magic = kMagic;
  h.version = kProtocolVersion;
  h.type = type;
  h.net_id = kNetId;
  h.seq = seq;
}

inline bool headerValid(const MsgHeader& h, MsgType expected) {
  return h.magic == kMagic && h.version == kProtocolVersion &&
         h.type == expected && h.net_id == kNetId;
}

inline size_t reportSize(uint8_t n_readings) {
  return sizeof(ReportFixed) + size_t(n_readings) * sizeof(Reading);
}

// Gelen ham baytların geçerli bir rapor olup olmadığını denetler.
inline bool parseReport(const uint8_t* data, size_t len, Report& out) {
  if (len < sizeof(ReportFixed) || len > sizeof(Report)) return false;
  const auto* f = reinterpret_cast<const ReportFixed*>(data);
  if (!headerValid(f->h, MsgType::kReport)) return false;
  if (f->n_readings > kMaxReadingsPerReport) return false;
  if (len != reportSize(f->n_readings)) return false;
  // memcpy yerine bayt bayt kopya: hizalanmamış erişimden kaçınmak için
  const uint8_t* src = data;
  uint8_t* dst = reinterpret_cast<uint8_t*>(&out);
  for (size_t i = 0; i < len; i++) dst[i] = src[i];
  return true;
}

inline bool parseAck(const uint8_t* data, size_t len, Ack& out) {
  if (len != sizeof(Ack)) return false;
  uint8_t* dst = reinterpret_cast<uint8_t*>(&out);
  for (size_t i = 0; i < len; i++) dst[i] = data[i];
  return headerValid(out.h, MsgType::kAck);
}

// Ölçüm aralığı sınırları (saniye)
constexpr uint16_t kMinIntervalS = 60;
constexpr uint16_t kMaxIntervalS = 3600;
constexpr uint16_t kDefaultIntervalS = 600;

inline uint16_t clampInterval(uint32_t s) {
  if (s < kMinIntervalS) return kMinIntervalS;
  if (s > kMaxIntervalS) return kMaxIntervalS;
  return static_cast<uint16_t>(s);
}

}  // namespace klima
