#include "espnow_hub.h"

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include "app_config.h"
#include "log.h"
#include "sensors.h"
#include "timekeeper.h"
#include "wifi_mgr.h"

namespace hub::espnow {
namespace {

struct RxFrame {
  uint8_t mac[6];
  int8_t rssi;
  uint8_t len;
  uint8_t data[klima::kEspNowMaxPayload];
};

struct DirEntry {
  bool used = false;
  uint8_t mac[6] = {0};
  uint16_t interval = klima::kDefaultIntervalS;
};

struct ReportMsg {
  int slot;
  int8_t rssi;
  bool is_new;
  uint8_t mac[6];
  klima::Report rep;
};

QueueHandle_t s_rx_q = nullptr;
QueueHandle_t s_report_q = nullptr;
TaskHandle_t s_task = nullptr;
portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
DirEntry s_dir[kMaxSensors];
bool s_auto_accept = true;
volatile bool s_running = false;
Stats s_stats;
uint16_t s_seq = 0;

void onRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  if (!s_running || len <= 0 || len > int(klima::kEspNowMaxPayload)) return;
  RxFrame f;
  memcpy(f.mac, info->src_addr, 6);
  f.rssi = info->rx_ctrl ? int8_t(info->rx_ctrl->rssi) : 0;
  f.len = uint8_t(len);
  memcpy(f.data, data, size_t(len));
  if (xQueueSend(s_rx_q, &f, 0) != pdTRUE) s_stats.queue_full++;
}

int findOrAssign(const uint8_t* mac, bool& is_new) {
  int slot = -1, free_slot = -1;
  portENTER_CRITICAL(&s_mux);
  for (int i = 0; i < kMaxSensors; i++) {
    if (s_dir[i].used && memcmp(s_dir[i].mac, mac, 6) == 0) slot = i;
    if (!s_dir[i].used && free_slot < 0) free_slot = i;
  }
  if (slot < 0 && s_auto_accept && free_slot >= 0) {
    s_dir[free_slot].used = true;
    memcpy(s_dir[free_slot].mac, mac, 6);
    s_dir[free_slot].interval = klima::kDefaultIntervalS;
    slot = free_slot;
    is_new = true;
  }
  portEXIT_CRITICAL(&s_mux);
  return slot;
}

bool ensurePeer(const uint8_t* mac) {
  if (esp_now_is_peer_exist(mac)) return true;
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, mac, 6);
  peer.channel = 0;  // o anki kanal
  peer.ifidx = WIFI_IF_STA;
  peer.encrypt = false;
  return esp_now_add_peer(&peer) == ESP_OK;
}

void task(void*) {
  RxFrame f;
  ReportMsg m;
  for (;;) {
    if (xQueueReceive(s_rx_q, &f, portMAX_DELAY) != pdTRUE) continue;
    if (!s_running) continue;
    s_stats.rx_frames++;
    if (!klima::parseReport(f.data, f.len, m.rep)) {
      s_stats.rx_invalid++;
      continue;
    }
    bool is_new = false;
    int slot = findOrAssign(f.mac, is_new);
    if (slot < 0) {
      s_stats.rx_unknown++;
      continue;
    }
    uint16_t interval;
    portENTER_CRITICAL(&s_mux);
    interval = s_dir[slot].interval;
    portEXIT_CRITICAL(&s_mux);

    // ACK'i hemen gönder: sensör yalnızca kısa bir süre dinliyor
    klima::Ack ack = {};
    klima::fillHeader(ack.h, klima::MsgType::kAck, ++s_seq);
    ack.ack_seq = m.rep.f.h.seq;
    ack.interval_s = interval;
    uint32_t ep = timekeeper::now();
    ack.epoch = ep;
    ack.channel = net::channel();
    ack.flags = klima::kAckAccepted | (ep ? klima::kAckTimeValid : 0);
    if (ensurePeer(f.mac) &&
        esp_now_send(f.mac, reinterpret_cast<const uint8_t*>(&ack), sizeof(ack)) == ESP_OK) {
      s_stats.acks_sent++;
    } else {
      s_stats.ack_fail++;
    }

    m.slot = slot;
    m.rssi = f.rssi;
    m.is_new = is_new;
    memcpy(m.mac, f.mac, 6);
    xQueueSend(s_report_q, &m, 0);
  }
}

}  // namespace

bool begin() {
  if (!s_rx_q) s_rx_q = xQueueCreate(8, sizeof(RxFrame));
  if (!s_report_q) s_report_q = xQueueCreate(8, sizeof(ReportMsg));
  if (!s_task) xTaskCreatePinnedToCore(task, "espnow", 4096, nullptr, 20, &s_task, 0);
  syncDirectory();
  if (esp_now_init() != ESP_OK) {
    LOGE("ESP-NOW baslatilamadi");
    return false;
  }
  esp_now_register_recv_cb(onRecv);
  s_running = true;
  LOGI("ESP-NOW hazir (kanal %u)", net::channel());
  return true;
}

void end() {
  s_running = false;
  esp_now_unregister_recv_cb();
  esp_now_deinit();
}

void syncDirectory() {
  portENTER_CRITICAL(&s_mux);
  for (int i = 0; i < kMaxSensors; i++) {
    const SensorCfg& c = g_cfg.sensors[i];
    s_dir[i].used = c.used;
    memcpy(s_dir[i].mac, c.mac, 6);
    s_dir[i].interval = klima::clampInterval(c.interval_s);
  }
  s_auto_accept = g_cfg.auto_accept_sensors;
  portEXIT_CRITICAL(&s_mux);
}

void loop() {
  ReportMsg m;
  // Tek döngüde en fazla 4 rapor işle (döngüyü uzun süre bloklamamak için)
  for (int i = 0; i < 4 && s_report_q && xQueueReceive(s_report_q, &m, 0) == pdTRUE; i++) {
    sensors::onReport(m.slot, m.mac, m.rssi, m.rep, m.is_new);
  }
}

Stats stats() { return s_stats; }

}  // namespace hub::espnow
