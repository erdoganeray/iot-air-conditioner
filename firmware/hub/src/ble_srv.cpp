#include "ble_srv.h"

#include <NimBLEDevice.h>

#include <deque>
#include <string>

#include "api.h"
#include "app_config.h"
#include "log.h"
#include "wifi_mgr.h"

namespace hub::ble {
namespace {

// Nordic UART Service
constexpr const char* kSvcUuid = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
constexpr const char* kRxUuid = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";  // telefon -> hub (yazma)
constexpr const char* kTxUuid = "6e400003-b5a3-f393-e0a9-e50e24dcca9e";  // hub -> telefon (bildirim)
constexpr int kMaxConns = 2;
constexpr size_t kMaxRx = 4096;
constexpr int kChunksPerLoop = 12;

struct Conn {
  bool used = false;
  uint16_t handle = 0;
  uint16_t mtu = 23;
  std::string rx;                 // yalnızca NimBLE görevi kullanır
  std::deque<std::string> out;    // yalnızca ana döngü kullanır
  volatile bool closed = false;
};

Conn s_conns[kMaxConns];
SemaphoreHandle_t s_lock = nullptr;
NimBLEServer* s_server = nullptr;
NimBLECharacteristic* s_tx = nullptr;
bool s_active = false;
bool s_low_power = false;
uint32_t s_request_until = 0;
uint32_t s_unwanted_since = 0;
volatile int s_client_count = 0;

void lock() { xSemaphoreTake(s_lock, portMAX_DELAY); }
void unlock() { xSemaphoreGive(s_lock); }

Conn* findConn(uint16_t h) {
  for (auto& c : s_conns)
    if (c.used && c.handle == h) return &c;
  return nullptr;
}

void applyAdvInterval() {
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  // birim 0.625 ms
  uint16_t lo = s_low_power ? 1280 : 320;   // 800 ms / 200 ms
  uint16_t hi = s_low_power ? 1600 : 480;   // 1000 ms / 300 ms
  adv->setMinInterval(lo);
  adv->setMaxInterval(hi);
}

class ServerCb : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* srv, NimBLEConnInfo& info) override {
    lock();
    for (auto& c : s_conns) {
      if (!c.used) {
        c.used = true;
        c.handle = info.getConnHandle();
        c.mtu = info.getMTU();
        c.rx.clear();
        c.closed = false;
        break;
      }
    }
    s_client_count = srv->getConnectedCount();
    unlock();
    LOGI("BLE baglandi (%u)", info.getConnHandle());
    // Daha fazla istemci kabul etmek için yayına devam
    if (srv->getConnectedCount() < kMaxConns) NimBLEDevice::startAdvertising();
  }

  void onDisconnect(NimBLEServer* srv, NimBLEConnInfo& info, int reason) override {
    lock();
    Conn* c = findConn(info.getConnHandle());
    if (c) c->closed = true;
    s_client_count = srv->getConnectedCount();
    unlock();
    api::enqueue(api::Transport::kBle, info.getConnHandle(), nullptr, 0);
    LOGI("BLE ayrildi (%u, sebep %d)", info.getConnHandle(), reason);
  }

  void onMTUChange(uint16_t mtu, NimBLEConnInfo& info) override {
    lock();
    Conn* c = findConn(info.getConnHandle());
    if (c) c->mtu = mtu;
    unlock();
  }
};

class RxCb : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* ch, NimBLEConnInfo& info) override {
    NimBLEAttValue v = ch->getValue();
    uint16_t h = info.getConnHandle();
    lock();
    Conn* c = findConn(h);
    if (!c) {
      unlock();
      return;
    }
    for (size_t i = 0; i < v.length(); i++) {
      char ch8 = char(v.data()[i]);
      if (ch8 == '\n' || ch8 == '\r') {
        if (!c->rx.empty()) {
          api::enqueue(api::Transport::kBle, h, c->rx.data(), c->rx.size());
          c->rx.clear();
        }
      } else if (c->rx.size() < kMaxRx) {
        c->rx.push_back(ch8);
      }
    }
    unlock();
  }
};

ServerCb s_server_cb;
RxCb s_rx_cb;

void start() {
  if (s_active) return;
  String name = g_cfg.name;
  if (name.length() > 20) name = name.substring(0, 20);
  NimBLEDevice::init(name.c_str());
  NimBLEDevice::setMTU(517);
  NimBLEDevice::setPower(9);
  s_server = NimBLEDevice::createServer();
  s_server->setCallbacks(&s_server_cb, false);
  s_server->advertiseOnDisconnect(true);
  NimBLEService* svc = s_server->createService(kSvcUuid);
  NimBLECharacteristic* rx =
      svc->createCharacteristic(kRxUuid, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  rx->setCallbacks(&s_rx_cb);
  s_tx = svc->createCharacteristic(kTxUuid, NIMBLE_PROPERTY::NOTIFY);
  s_server->start();
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->setName(name.c_str());
  adv->addServiceUUID(kSvcUuid);
  adv->enableScanResponse(true);
  applyAdvInterval();
  adv->start();
  s_active = true;
  LOGI("BLE acildi: '%s'", name.c_str());
}

void stop() {
  if (!s_active) return;
  lock();
  for (auto& c : s_conns) {
    c.used = false;
    c.out.clear();
    c.rx.clear();
  }
  s_client_count = 0;
  unlock();
  NimBLEDevice::deinit(true);
  s_server = nullptr;
  s_tx = nullptr;
  s_active = false;
  LOGI("BLE kapatildi");
}

bool wanted() {
  if (g_cfg.ble_always) return true;
  if (!net::connected()) return true;
  if (net::apActive()) return true;
  if (millis() < 15UL * 60000UL) return true;
  if (s_request_until && int32_t(millis() - s_request_until) < 0) return true;
  return s_client_count > 0;
}

}  // namespace

void begin() {
  s_lock = xSemaphoreCreateMutex();
  if (wanted()) start();
}

void loop() {
  // Açık/kapalı yönetimi ("gerektiğinde" modu)
  if (wanted()) {
    s_unwanted_since = 0;
    if (!s_active) start();
  } else if (s_active) {
    if (!s_unwanted_since) s_unwanted_since = millis() ? millis() : 1;
    if (millis() - s_unwanted_since > 5UL * 60000UL) stop();
  }
  if (!s_active || !s_tx) return;

  lock();
  for (auto& c : s_conns) {
    if (!c.used) continue;
    if (c.closed) {
      c.used = false;
      c.out.clear();
      c.rx.clear();
      continue;
    }
    int sent = 0;
    while (!c.out.empty() && sent < kChunksPerLoop) {
      const std::string& chunk = c.out.front();
      if (!s_tx->notify(reinterpret_cast<const uint8_t*>(chunk.data()), chunk.size(), c.handle)) break;
      c.out.pop_front();
      sent++;
    }
  }
  unlock();
}

bool active() { return s_active; }
int clients() { return s_client_count; }

void send(uint32_t conn, const String& msg) {
  if (!s_active) return;
  lock();
  Conn* c = findConn(uint16_t(conn));
  if (c && !c->closed) {
    size_t chunk = c->mtu > 3 ? c->mtu - 3 : 20;
    if (chunk > 500) chunk = 500;
    std::string full(msg.c_str(), msg.length());
    full.push_back('\n');
    // Kuyruk aşırı büyürse (bağlantı yavaş) eski mesajları at
    if (c->out.size() > 400) c->out.clear();
    for (size_t i = 0; i < full.size(); i += chunk) c->out.push_back(full.substr(i, chunk));
  }
  unlock();
}

void setLowPower(bool low) {
  if (low == s_low_power) return;
  s_low_power = low;
  if (s_active) applyAdvInterval();
}

void requestOn(uint32_t minutes) { s_request_until = millis() + minutes * 60000UL; }

}  // namespace hub::ble
