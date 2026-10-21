#include "timekeeper.h"

#include <esp_sntp.h>
#include <sys/time.h>

#include "app_config.h"
#include "log.h"
#include "wifi_mgr.h"

namespace hub::timekeeper {
namespace {
constexpr uint32_t kMinValidEpoch = 1735689600;  // 2025-01-01
bool s_ntp_started = false;
const char* s_source = "-";
volatile bool s_ntp_synced = false;

void onNtpSync(struct timeval*) { s_ntp_synced = true; }
}  // namespace

void applyTimezone() {
  setenv("TZ", g_cfg.tz.c_str(), 1);
  tzset();
}

void begin() {
  applyTimezone();
  sntp_set_time_sync_notification_cb(onNtpSync);
}

void loop() {
  if (!s_ntp_started && net::connected()) {
    configTzTime(g_cfg.tz.c_str(), "pool.ntp.org", "time.google.com", "tr.pool.ntp.org");
    s_ntp_started = true;
    LOGI("NTP baslatildi");
  }
  if (s_ntp_synced) {
    s_ntp_synced = false;
    s_source = "ntp";
    net::setInternetOk(true);
    LOGI("Saat NTP ile ayarlandi: %lu", (unsigned long)now());
  }
}

bool valid() { return time(nullptr) > time_t(kMinValidEpoch); }

uint32_t now() {
  time_t t = time(nullptr);
  return t > time_t(kMinValidEpoch) ? uint32_t(t) : 0;
}

bool local(struct tm& out) {
  if (!valid()) return false;
  time_t t = time(nullptr);
  localtime_r(&t, &out);
  return true;
}

void setEpoch(uint32_t epoch, const char* source) {
  if (epoch < kMinValidEpoch) return;
  // NTP çalışıyorsa ve saat zaten geçerliyse küçük sapmaları yok say
  int64_t diff = int64_t(epoch) - int64_t(time(nullptr));
  if (valid() && diff > -30 && diff < 30) return;
  timeval tv = {time_t(epoch), 0};
  settimeofday(&tv, nullptr);
  s_source = source;
  LOGI("Saat ayarlandi (%s): %lu", source, (unsigned long)epoch);
}

const char* lastSource() { return s_source; }

}  // namespace hub::timekeeper
