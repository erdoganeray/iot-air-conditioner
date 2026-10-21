#include "notify.h"

#include <ArduinoJson.h>

#include "api.h"
#include "app_config.h"
#include "log.h"
#include "telegram.h"
#include "timekeeper.h"

namespace hub::notify {

void send(Kind kind, const char* text) {
  LOGI("Bildirim: %s", text);
  JsonDocument doc;
  doc["text"] = text;
  doc["kind"] = int(kind);
  doc["t"] = timekeeper::now();
  api::broadcastEvent("notify", doc.as<JsonVariantConst>());

  const TelegramCfg& t = g_cfg.tg;
  if (!t.enabled || t.token.isEmpty() || t.n_chats == 0) return;
  bool allowed = true;
  switch (kind) {
    case Kind::kBattery: allowed = t.notify_battery; break;
    case Kind::kOffline: allowed = t.notify_offline; break;
    case Kind::kPower: allowed = t.notify_power; break;
    case Kind::kWifi: allowed = t.notify_wifi; break;
    case Kind::kAuto: allowed = t.notify_auto; break;
    case Kind::kInfo: break;
  }
  if (allowed) telegram::queueBroadcast(text);
}

}  // namespace hub::notify
