#include "ir_ctrl.h"

#include <IRac.h>
#include <IRrecv.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <IRutils.h>
#include <LittleFS.h>

#include <vector>

#include "api.h"
#include "app_config.h"
#include "history.h"
#include "led.h"
#include "log.h"
#include "notify.h"
#include "pins.h"
#include "runtime.h"
#include "timekeeper.h"

namespace hub::ir {
namespace {

constexpr uint16_t kCaptureBufferSize = 1024;  // klima mesajları uzun olabilir
constexpr uint8_t kTimeoutMs = 50;             // klimalar için önerilen değer
constexpr uint16_t kMinUnknownSize = 12;
constexpr uint32_t kLearnTimeoutMs = 30000;
constexpr uint32_t kTxGuardMs = 400;           // kendi gönderdiğimizi duymayalım
constexpr int kMaxCommands = 32;
constexpr uint16_t kRawFreqKhz = 38;

IRrecv s_recv(pins::kIrRecv, kCaptureBufferSize, kTimeoutMs, true);
IRac s_ac(pins::kIrSend);
IRsend s_send(pins::kIrSend);
decode_results s_results;

stdAc::state_t s_prev;
bool s_has_prev = false;
uint32_t s_tx_guard_until = 0;

struct RawCmd {
  int id = 0;
  String name;
  String role;
  std::vector<uint16_t> data;
};
std::vector<RawCmd> s_cmds;
int s_next_id = 1;

bool s_learning = false;
uint32_t s_learn_start = 0;
String s_learn_name, s_learn_role;
bool s_learn_force_raw = false;

// ------------------------------------------------------------- dönüşümler
stdAc::opmode_t toStdMode(AcMode m) {
  switch (m) {
    case AcMode::kAuto: return stdAc::opmode_t::kAuto;
    case AcMode::kCool: return stdAc::opmode_t::kCool;
    case AcMode::kHeat: return stdAc::opmode_t::kHeat;
    case AcMode::kDry: return stdAc::opmode_t::kDry;
    case AcMode::kFan: return stdAc::opmode_t::kFan;
  }
  return stdAc::opmode_t::kCool;
}

AcMode fromStdMode(stdAc::opmode_t m, AcMode fallback) {
  switch (m) {
    case stdAc::opmode_t::kAuto: return AcMode::kAuto;
    case stdAc::opmode_t::kCool: return AcMode::kCool;
    case stdAc::opmode_t::kHeat: return AcMode::kHeat;
    case stdAc::opmode_t::kDry: return AcMode::kDry;
    case stdAc::opmode_t::kFan: return AcMode::kFan;
    default: return fallback;
  }
}

stdAc::fanspeed_t toStdFan(AcFan f) {
  switch (f) {
    case AcFan::kAuto: return stdAc::fanspeed_t::kAuto;
    case AcFan::kMin: return stdAc::fanspeed_t::kMin;
    case AcFan::kLow: return stdAc::fanspeed_t::kLow;
    case AcFan::kMedium: return stdAc::fanspeed_t::kMedium;
    case AcFan::kHigh: return stdAc::fanspeed_t::kHigh;
    case AcFan::kMax: return stdAc::fanspeed_t::kMax;
  }
  return stdAc::fanspeed_t::kAuto;
}

AcFan fromStdFan(stdAc::fanspeed_t f) {
  switch (f) {
    case stdAc::fanspeed_t::kMin: return AcFan::kMin;
    case stdAc::fanspeed_t::kLow: return AcFan::kLow;
    case stdAc::fanspeed_t::kMedium:
    case stdAc::fanspeed_t::kMediumHigh: return AcFan::kMedium;
    case stdAc::fanspeed_t::kHigh: return AcFan::kHigh;
    case stdAc::fanspeed_t::kMax: return AcFan::kMax;
    default: return AcFan::kAuto;
  }
}

decode_type_t configuredProtocol() {
  if (g_cfg.ir.protocol.isEmpty()) return decode_type_t::UNKNOWN;
  return strToDecodeType(g_cfg.ir.protocol.c_str());
}

stdAc::state_t toStd(const AcState& s) {
  stdAc::state_t st;
  if (s_has_prev) st = s_prev;  // diğer alanlar (ışık, sessiz vb.) korunsun
  st.protocol = configuredProtocol();
  st.model = g_cfg.ir.model;
  st.power = s.power;
  st.mode = toStdMode(s.mode);
  st.degrees = s.temp;
  st.celsius = true;
  st.fanspeed = toStdFan(s.fan);
  st.swingv = s.swing ? stdAc::swingv_t::kAuto : stdAc::swingv_t::kOff;
  st.swingh = stdAc::swingh_t::kOff;
  st.sleep = -1;
  st.clock = -1;
  return st;
}

AcState fromStd(const stdAc::state_t& st) {
  AcState s = g_rt.ac.state;
  s.power = st.power;
  s.mode = fromStdMode(st.mode, s.mode);
  if (st.degrees >= kAcMinTemp && st.degrees <= kAcMaxTemp) s.temp = clampAcTemp(st.degrees);
  s.fan = fromStdFan(st.fanspeed);
  s.swing = st.swingv != stdAc::swingv_t::kOff;
  return s;
}

// ----------------------------------------------------------------- durum
void commitState(const AcState& s, Source src) {
  AcRuntime& ac = g_rt.ac;
  bool power_changed = s.power != ac.state.power;
  ac.state = s;
  ac.last_src = src;
  ac.last_change_epoch = timekeeper::now();
  if (power_changed) {
    ac.has_power_change = true;
    ac.last_power_change_s = uptimeS();
  }
  if (isManualSource(src)) {
    ac.has_manual = true;
    ac.last_manual_s = uptimeS();
    g_rt.auto_mem.owner = Owner::kNone;  // elle müdahale: otomasyon sahipliği bırakır
  }
  history::logAc(s, src);
  markStateDirty();
  if (!isManualSource(src) && src != Source::kNone) {
    char msg[96];
    if (s.power) {
      snprintf(msg, sizeof(msg), "🤖 Otomasyon (%s): klima açıldı, %s %.1f°C", sourceName(src),
               modeName(s.mode), s.temp);
    } else {
      snprintf(msg, sizeof(msg), "🤖 Otomasyon (%s): klima kapatıldı", sourceName(src));
    }
    notify::send(notify::Kind::kAuto, msg);
  }
}

void beforeTx() {
  s_recv.disableIRIn();
  led::flashGreen();
}

void afterTx() {
  s_tx_guard_until = millis() + kTxGuardMs;
  s_recv.enableIRIn();
}

// ----------------------------------------------------------- ham komutlar
RawCmd* findCmd(int id) {
  for (auto& c : s_cmds)
    if (c.id == id) return &c;
  return nullptr;
}

const RawCmd* findRole(const String& role) {
  for (auto& c : s_cmds)
    if (c.role == role) return &c;
  return nullptr;
}

void saveIndex() {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (auto& c : s_cmds) {
    JsonObject o = arr.add<JsonObject>();
    o["id"] = c.id;
    o["name"] = c.name;
    o["role"] = c.role;
  }
  File f = LittleFS.open("/ir/index.json", "w");
  if (!f) return;
  serializeJson(doc, f);
  f.close();
}

bool saveData(const RawCmd& c) {
  File f = LittleFS.open(String("/ir/") + c.id + ".bin", "w");
  if (!f) return false;
  size_t n = f.write(reinterpret_cast<const uint8_t*>(c.data.data()), c.data.size() * 2);
  f.close();
  return n == c.data.size() * 2;
}

void loadCommands() {
  if (!LittleFS.exists("/ir")) LittleFS.mkdir("/ir");
  File f = LittleFS.open("/ir/index.json", "r");
  if (!f) return;
  JsonDocument doc;
  if (deserializeJson(doc, f)) {
    f.close();
    return;
  }
  f.close();
  for (JsonObjectConst o : doc.as<JsonArrayConst>()) {
    RawCmd c;
    c.id = o["id"] | 0;
    c.name = o["name"] | "";
    c.role = o["role"] | "";
    File d = LittleFS.open(String("/ir/") + c.id + ".bin", "r");
    if (!d) continue;
    size_t n = d.size() / 2;
    c.data.resize(n);
    d.read(reinterpret_cast<uint8_t*>(c.data.data()), n * 2);
    d.close();
    if (c.id >= s_next_id) s_next_id = c.id + 1;
    s_cmds.push_back(std::move(c));
  }
  LOGI("%u ham IR komutu yuklendi", unsigned(s_cmds.size()));
}

// Rol -> klima durumu ("off", "on", "cool", "cool:24" ...)
bool stateFromRole(const String& role, AcState& s) {
  if (role.isEmpty()) return false;
  if (role == "off") {
    s.power = false;
    return true;
  }
  if (role == "on") {
    s.power = true;
    return true;
  }
  int colon = role.indexOf(':');
  String m = colon > 0 ? role.substring(0, colon) : role;
  AcMode mode;
  if (!parseMode(m.c_str(), mode)) return false;
  s.power = true;
  s.mode = mode;
  if (colon > 0) s.temp = clampAcTemp(role.substring(colon + 1).toFloat());
  return true;
}

const RawCmd* pickRoleCommand(const AcState& s) {
  if (!s.power) return findRole("off");
  char exact[16];
  snprintf(exact, sizeof(exact), "%s:%d", modeName(s.mode), int(s.temp));
  const RawCmd* c = findRole(exact);
  if (!c) c = findRole(modeName(s.mode));
  if (!c) c = findRole("on");
  return c;
}

void sendRaw(const RawCmd& c) {
  beforeTx();
  s_send.sendRaw(c.data.data(), uint16_t(c.data.size()), kRawFreqKhz);
  afterTx();
}

// Gelen ham sinyal kayıtlı bir komuta benziyor mu?
int matchRaw(const uint16_t* raw, uint16_t len) {
  for (auto& c : s_cmds) {
    int diff = int(c.data.size()) - int(len);
    if (diff < -4 || diff > 4) continue;
    uint16_t n = len < c.data.size() ? len : uint16_t(c.data.size());
    uint16_t ok = 0;
    for (uint16_t i = 0; i < n; i++) {
      uint32_t a = raw[i], b = c.data[i];
      uint32_t tol = b * 30 / 100 + 150;
      if ((a > b ? a - b : b - a) <= tol) ok++;
    }
    if (n > 0 && ok * 100 / n >= 90) return c.id;
  }
  return -1;
}

void emitLearn(JsonDocument& doc) { api::broadcastEvent("learn", doc.as<JsonVariantConst>()); }

void finishLearning() {
  s_learning = false;
  led::setLearning(false);
}

void handleLearned(const decode_results& r) {
  JsonDocument doc;
  decode_type_t type = r.decode_type;
  if (!s_learn_force_raw && type != decode_type_t::UNKNOWN && IRac::isProtocolSupported(type)) {
    stdAc::state_t st;
    if (IRAcUtils::decodeToState(&r, &st)) {
      g_cfg.ir.protocol = typeToString(type);
      g_cfg.ir.model = st.model;
      g_cfg.ir.raw_mode = false;
      config::markDirty();
      s_prev = st;
      s_has_prev = true;
      commitState(fromStd(st), Source::kRemote);
      doc["ok"] = true;
      doc["kind"] = "protocol";
      doc["protocol"] = g_cfg.ir.protocol;
      doc["model"] = st.model;
      doc["model_name"] = irutils::modelToStr(type, st.model);
      doc["desc"] = IRAcUtils::resultAcToString(&r);
      LOGI("Ogrenildi: protokol %s model %d", g_cfg.ir.protocol.c_str(), st.model);
      finishLearning();
      emitLearn(doc);
      return;
    }
  }
  uint16_t len = getCorrectedRawLength(&r);
  if (len < kMinUnknownSize) return;  // gürültü, beklemeye devam
  if (int(s_cmds.size()) >= kMaxCommands) {
    doc["ok"] = false;
    doc["error"] = "En fazla 32 komut kaydedilebilir";
    finishLearning();
    emitLearn(doc);
    return;
  }
  uint16_t* raw = resultToRawArray(&r);
  RawCmd c;
  c.id = s_next_id++;
  c.name = s_learn_name.length() ? s_learn_name : String("Komut ") + c.id;
  c.role = s_learn_role;
  c.data.assign(raw, raw + len);
  delete[] raw;
  // Aynı rol başka komutta varsa o rolü kaldır
  if (c.role.length()) {
    for (auto& o : s_cmds)
      if (o.role == c.role) o.role = "";
  }
  bool ok = saveData(c);
  s_cmds.push_back(c);
  saveIndex();
  doc["ok"] = ok;
  doc["kind"] = "raw";
  doc["id"] = c.id;
  doc["name"] = c.name;
  doc["role"] = c.role;
  doc["len"] = len;
  doc["detected"] = typeToString(type, r.repeat);
  LOGI("Ham komut kaydedildi: #%d '%s' (%u)", c.id, c.name.c_str(), len);
  finishLearning();
  emitLearn(doc);
}

void handlePassive(const decode_results& r) {
  if (!g_cfg.ir.raw_mode) {
    decode_type_t proto = configuredProtocol();
    if (proto == decode_type_t::UNKNOWN || r.decode_type != proto) return;
    stdAc::state_t st;
    if (!IRAcUtils::decodeToState(&r, &st, s_has_prev ? &s_prev : nullptr)) return;
    s_prev = st;
    s_has_prev = true;
    AcState s = fromStd(st);
    if (!s.sameAs(g_rt.ac.state)) {
      LOGI("Kumanda duyuldu: %s", IRAcUtils::resultAcToString(&r).c_str());
      commitState(s, Source::kRemote);
    }
    return;
  }
  uint16_t len = getCorrectedRawLength(&r);
  if (len < kMinUnknownSize) return;
  uint16_t* raw = resultToRawArray(&r);
  int id = matchRaw(raw, len);
  delete[] raw;
  if (id < 0) return;
  RawCmd* c = findCmd(id);
  AcState s = g_rt.ac.state;
  if (c && stateFromRole(c->role, s) && !s.sameAs(g_rt.ac.state)) {
    LOGI("Kumanda duyuldu: ham komut #%d", id);
    commitState(s, Source::kRemote);
  }
}

}  // namespace

void begin() {
  s_send.begin();
  s_recv.setUnknownThreshold(kMinUnknownSize);
  s_recv.setTolerance(kTolerance);
  s_recv.enableIRIn();
  if (!LittleFS.exists("/ir")) LittleFS.mkdir("/ir");
  loadCommands();
  // Son bilinen klima durumunu geri yükle (hub yeniden başlasa da klima aynı durumda kalır)
  AcState last;
  Source src;
  uint32_t ep;
  if (history::lastAc(last, src, ep)) {
    g_rt.ac.state = last;
    g_rt.ac.last_src = src;
    g_rt.ac.last_change_epoch = ep;
  } else {
    g_rt.ac.state = g_cfg.ac_default;
    g_rt.ac.state.power = false;
  }
  LOGI("IR hazir: %s", g_cfg.ir.raw_mode ? "ham komut modu"
                                          : (g_cfg.ir.protocol.length() ? g_cfg.ir.protocol.c_str()
                                                                        : "protokol yok"));
}

void loop() {
  if (s_learning && millis() - s_learn_start > kLearnTimeoutMs) {
    finishLearning();
    JsonDocument doc;
    doc["ok"] = false;
    doc["error"] = "timeout";
    emitLearn(doc);
  }
  if (!s_recv.decode(&s_results)) return;
  if (s_results.overflow) LOGW("IR tamponu tasti (sinyal cok uzun)");
  if (int32_t(millis() - s_tx_guard_until) < 0) return;
  if (s_learning) {
    handleLearned(s_results);
  } else {
    handlePassive(s_results);
  }
}

bool apply(const AcState& in, Source src, String& err) {
  AcState s = in;
  s.temp = clampAcTemp(s.temp);
  if (!g_cfg.ir.raw_mode) {
    decode_type_t proto = configuredProtocol();
    if (proto == decode_type_t::UNKNOWN || !IRac::isProtocolSupported(proto)) {
      err = "Klima protokolu ayarli degil: once kumandayi ogretin";
      return false;
    }
    stdAc::state_t st = toStd(s);
    beforeTx();
    bool ok = s_ac.sendAc(st, s_has_prev ? &s_prev : nullptr);
    afterTx();
    if (!ok) {
      err = "IR gonderilemedi";
      return false;
    }
    s_prev = st;
    s_has_prev = true;
  } else {
    const RawCmd* c = pickRoleCommand(s);
    if (!c) {
      err = s.power ? String("Bu mod icin ogretilmis komut yok: ") + modeName(s.mode)
                    : String("'Kapat' rolunde komut yok");
      return false;
    }
    sendRaw(*c);
    AcState from_role = s;
    if (stateFromRole(c->role, from_role)) {
      // Ham modda derece komuta bağlıdır; rol derecesi yoksa istenen derece gösterilir
      if (c->role.indexOf(':') < 0) from_role.temp = s.temp;
      s = from_role;
    }
  }
  commitState(s, src);
  return true;
}

bool sendCommand(int id, Source src, String& err) {
  RawCmd* c = findCmd(id);
  if (!c) {
    err = "komut bulunamadi";
    return false;
  }
  sendRaw(*c);
  AcState s = g_rt.ac.state;
  if (stateFromRole(c->role, s)) commitState(s, src);
  return true;
}

bool startLearning(const String& name, const String& role, bool force_raw, String& err) {
  if (role.length()) {
    AcState tmp;
    if (!stateFromRole(role, tmp)) {
      err = "gecersiz rol (off, on, cool, heat, dry, fan, auto veya cool:24)";
      return false;
    }
  }
  s_learning = true;
  s_learn_start = millis();
  s_learn_name = name;
  s_learn_role = role;
  s_learn_force_raw = force_raw;
  led::setLearning(true);
  LOGI("IR ogrenme basladi (%s)", force_raw ? "ham" : "otomatik");
  return true;
}

void cancelLearning() {
  if (s_learning) finishLearning();
}

bool learning() { return s_learning; }

uint32_t learnRemainingS() {
  if (!s_learning) return 0;
  uint32_t el = millis() - s_learn_start;
  return el >= kLearnTimeoutMs ? 0 : (kLearnTimeoutMs - el) / 1000;
}

void commandsJson(JsonArray arr) {
  for (auto& c : s_cmds) {
    JsonObject o = arr.add<JsonObject>();
    o["id"] = c.id;
    o["name"] = c.name;
    o["role"] = c.role;
    o["len"] = c.data.size();
  }
}

bool updateCommand(int id, JsonObjectConst o, String& err) {
  RawCmd* c = findCmd(id);
  if (!c) {
    err = "komut bulunamadi";
    return false;
  }
  if (o["name"].is<const char*>()) c->name = o["name"].as<String>();
  if (o["role"].is<const char*>()) {
    String role = o["role"].as<String>();
    AcState tmp;
    if (role.length() && !stateFromRole(role, tmp)) {
      err = "gecersiz rol";
      return false;
    }
    if (role.length()) {
      for (auto& x : s_cmds)
        if (x.role == role) x.role = "";
    }
    c->role = role;
  }
  saveIndex();
  return true;
}

bool deleteCommand(int id) {
  for (size_t i = 0; i < s_cmds.size(); i++) {
    if (s_cmds[i].id == id) {
      LittleFS.remove(String("/ir/") + id + ".bin");
      s_cmds.erase(s_cmds.begin() + long(i));
      saveIndex();
      return true;
    }
  }
  return false;
}

void protocolsJson(JsonArray arr) {
  for (int i = 0; i <= int(kLastDecodeType); i++) {
    decode_type_t p = decode_type_t(i);
    if (IRac::isProtocolSupported(p)) arr.add(typeToString(p));
  }
}

void modelsJson(const char* proto, JsonArray arr) {
  decode_type_t p = strToDecodeType(proto);
  if (p == decode_type_t::UNKNOWN) return;
  for (int m = 1; m <= 12; m++) {
    String n = irutils::modelToStr(p, int16_t(m));
    if (n == "UNKNOWN" || n.isEmpty()) continue;
    JsonObject o = arr.add<JsonObject>();
    o["id"] = m;
    o["name"] = n;
  }
}

bool setProtocol(const char* proto, int model, String& err) {
  decode_type_t p = strToDecodeType(proto ? proto : "");
  if (p == decode_type_t::UNKNOWN || !IRac::isProtocolSupported(p)) {
    err = "desteklenmeyen protokol";
    return false;
  }
  g_cfg.ir.protocol = typeToString(p);
  g_cfg.ir.model = int16_t(model);
  g_cfg.ir.raw_mode = false;
  s_has_prev = false;
  config::markDirty();
  markStateDirty();
  return true;
}

bool setRawMode(bool raw, String& err) {
  if (!raw && g_cfg.ir.protocol.isEmpty()) {
    err = "once bir protokol secin veya ogretin";
    return false;
  }
  g_cfg.ir.raw_mode = raw;
  config::markDirty();
  markStateDirty();
  return true;
}

void infoJson(JsonObject o) {
  o["mode"] = g_cfg.ir.raw_mode ? "raw" : "protocol";
  o["protocol"] = g_cfg.ir.protocol;
  o["model"] = g_cfg.ir.model;
  decode_type_t p = configuredProtocol();
  if (p != decode_type_t::UNKNOWN && g_cfg.ir.model > 0) {
    o["model_name"] = irutils::modelToStr(p, g_cfg.ir.model);
  }
  o["commands"] = s_cmds.size();
  o["ready"] = ready();
  o["learning"] = s_learning;
  if (s_learning) o["learn_remaining"] = learnRemainingS();
}

bool ready() {
  if (g_cfg.ir.raw_mode) return findRole("off") != nullptr || !s_cmds.empty();
  decode_type_t p = configuredProtocol();
  return p != decode_type_t::UNKNOWN && IRac::isProtocolSupported(p);
}

}  // namespace hub::ir
