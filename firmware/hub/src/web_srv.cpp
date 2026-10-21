#include "web_srv.h"

#include <ArduinoOTA.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <WiFi.h>

#include <map>
#include <string>

#include "api.h"
#include "app_config.h"
#include "generated/app_html.h"
#include "history.h"
#include "log.h"
#include "wifi_mgr.h"

namespace hub::web {
namespace {

AsyncWebServer s_server(80);
AsyncWebSocket s_ws("/ws");
std::map<uint32_t, std::string> s_partial;  // yalnızca async_tcp görevi kullanır
uint32_t s_last_cleanup = 0;
volatile bool s_reboot = false;
uint32_t s_reboot_at = 0;
bool s_update_authed = false;
bool s_ota_started = false;

bool isCaptiveRequest(AsyncWebServerRequest* req) {
  if (!net::apActive()) return false;
  String host = req->host();
  if (host.isEmpty()) return false;
  // Kurulum ağındaki istemci başka bir alan adı istiyorsa portala yönlendir
  return host != WiFi.softAPIP().toString() && host != "klima.local" && host != WiFi.localIP().toString();
}

void redirectToPortal(AsyncWebServerRequest* req) {
  req->redirect(String("http://") + WiFi.softAPIP().toString() + "/");
}

void serveApp(AsyncWebServerRequest* req) {
  if (isCaptiveRequest(req)) {
    redirectToPortal(req);
    return;
  }
  AsyncWebServerResponse* res = req->beginResponse(200, "text/html", kAppHtmlGz, kAppHtmlGzLen);
  res->addHeader("Content-Encoding", "gzip");
  res->addHeader("Cache-Control", "no-cache");
  req->send(res);
}

bool pinOk(AsyncWebServerRequest* req) {
  if (req->hasParam("pin")) return req->getParam("pin")->value() == g_cfg.pin;
  if (req->hasHeader("X-Pin")) return req->getHeader("X-Pin")->value() == g_cfg.pin;
  return false;
}

void onWsEvent(AsyncWebSocket*, AsyncWebSocketClient* client, AwsEventType type, void* arg,
               uint8_t* data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      LOGI("WS baglandi #%lu (%s)", (unsigned long)client->id(), client->remoteIP().toString().c_str());
      break;
    case WS_EVT_DISCONNECT:
      s_partial.erase(client->id());
      api::enqueue(api::Transport::kWs, client->id(), nullptr, 0);
      break;
    case WS_EVT_DATA: {
      auto* info = static_cast<AwsFrameInfo*>(arg);
      if (info->opcode != WS_TEXT && info->message_opcode != WS_TEXT) return;
      if (info->final && info->index == 0 && info->len == len) {
        api::enqueue(api::Transport::kWs, client->id(), reinterpret_cast<const char*>(data), len);
      } else {
        std::string& buf = s_partial[client->id()];
        if (info->index == 0 && info->num == 0) buf.clear();
        if (buf.size() + len <= 4096) buf.append(reinterpret_cast<const char*>(data), len);
        if (info->final && info->index + len == info->len) {
          api::enqueue(api::Transport::kWs, client->id(), buf.data(), buf.size());
          s_partial.erase(client->id());
        }
      }
      break;
    }
    default:
      break;
  }
}

void setupOta() {
  ArduinoOTA.setHostname("klima");
  ArduinoOTA.setPassword(g_cfg.pin.c_str());
  ArduinoOTA.setMdnsEnabled(false);
  ArduinoOTA.onStart([]() {
    LOGW("OTA guncelleme basladi");
    config::saveNow();
    history::saveNow();
  });
  ArduinoOTA.onError([](ota_error_t e) { LOGE("OTA hatasi %d", int(e)); });
  ArduinoOTA.begin();
  s_ota_started = true;
}

}  // namespace

void begin() {
  s_ws.onEvent(onWsEvent);
  s_server.addHandler(&s_ws);

  s_server.on("/", HTTP_GET, serveApp);
  s_server.on("/index.html", HTTP_GET, serveApp);

  // Captive portal algılama adresleri (Android, iOS, Windows)
  for (const char* p : {"/generate_204", "/gen_204", "/hotspot-detect.html", "/connecttest.txt",
                        "/ncsi.txt", "/fwlink", "/canonical.html", "/success.txt"}) {
    s_server.on(p, HTTP_GET, [](AsyncWebServerRequest* req) { redirectToPortal(req); });
  }

  s_server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!pinOk(req)) {
      req->send(401, "application/json", "{\"error\":\"pin\"}");
      return;
    }
    JsonDocument doc;
    api::buildStatus(doc.to<JsonObject>());
    String s;
    serializeJson(doc, s);
    req->send(200, "application/json", s);
  });

  // Tarayıcıdan firmware yükleme: POST /update?pin=XXXX (multipart, alan adı "firmware")
  s_server.on(
      "/update", HTTP_POST,
      [](AsyncWebServerRequest* req) {
        bool ok = s_update_authed && !Update.hasError();
        req->send(ok ? 200 : 500, "application/json",
                  ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"guncelleme basarisiz\"}");
        if (ok) {
          s_reboot = true;
          s_reboot_at = millis() + 1000;
        }
      },
      [](AsyncWebServerRequest* req, const String& filename, size_t index, uint8_t* data, size_t len,
         bool final) {
        if (index == 0) {
          s_update_authed = pinOk(req);
          if (!s_update_authed) return;
          LOGW("Web OTA basladi: %s", filename.c_str());
          config::saveNow();
          history::saveNow();
          if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) Update.printError(Serial);
        }
        if (!s_update_authed) return;
        if (Update.write(data, len) != len) Update.printError(Serial);
        if (final) {
          if (Update.end(true)) {
            LOGW("Web OTA tamam: %u bayt", unsigned(index + len));
          } else {
            Update.printError(Serial);
          }
        }
      });

  s_server.onNotFound([](AsyncWebServerRequest* req) {
    if (net::apActive()) {
      redirectToPortal(req);
    } else {
      req->send(404, "text/plain", "Bulunamadi");
    }
  });

  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
  s_server.begin();
  setupOta();
  LOGI("Web sunucusu hazir (port 80)");
}

void loop() {
  uint32_t now = millis();
  if (now - s_last_cleanup > 1000) {
    s_last_cleanup = now;
    s_ws.cleanupClients(4);
  }
  if (s_ota_started) ArduinoOTA.handle();
  if (s_reboot && int32_t(now - s_reboot_at) >= 0) ESP.restart();
}

void send(uint32_t client, const String& msg) {
  AsyncWebSocketClient* c = s_ws.client(client);
  if (c && c->status() == WS_CONNECTED && c->canSend()) c->text(msg);
}

int clients() { return int(s_ws.count()); }

}  // namespace hub::web
