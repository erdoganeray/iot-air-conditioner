# Hub JSON API

Telefon uygulaması, Bluetooth (BLE) ve WebSocket üzerinden aynı komutları kullanır. USB seri
port (115200 baud) da aynı komutları kabul eder. Hata ayıklarken bilgisayardan doğrudan
yazabilirsiniz.

| Yol | Adres | Çerçeve |
|---|---|---|
| Bluetooth | Nordic UART Service `6e400001-b5a3-f393-e0a9-e50e24dcca9e`, yazma `…0002`, bildirim `…0003` | Her mesaj tek satır JSON + `\n`. Uzun mesajlar MTU boyunda parçalanır |
| WebSocket | `ws://klima.local/ws` (veya `ws://<ip>/ws`) | Her WebSocket metin mesajı bir JSON |
| USB seri | 115200 baud | Her satır bir JSON. Bu yolda PIN gerekmez |

> **İpucu:** Android'deki "Serial Bluetooth Terminal" uygulamasında *Devices → Bluetooth LE*
> sekmesinden **KlimaHub**'a bağlanıp `{"cmd":"auth","pin":"1234"}` yazarak da komut
> gönderebilirsiniz.

## Mesaj biçimi

```json
// İstek
{"id": 7, "cmd": "ac.set", "power": true, "temp": 24}
// Yanıt
{"id": 7, "ok": true, "data": {"state": {"power": true, "mode": "cool", "temp": 24, "fan": "auto", "swing": false}}}
// Hata
{"id": 7, "ok": false, "error": "Klima protokolu ayarli degil: once kumandayi ogretin"}
// Hub'ın kendiliğinden gönderdiği olay
{"evt": "status", "data": { ... }}
```

## Kimlik doğrulama

Bağlandıktan sonra `hello` ve `auth` dışındaki komutlar için PIN gerekir (varsayılan **1234**).
5 hatalı denemeden sonra o bağlantı 1 dk kilitlenir.

| Komut | Parametreler | Yanıt |
|---|---|---|
| `hello` | – | `name, fw, api, authed, time_valid, default_pin` |
| `auth` | `pin` | `name` |

## Komutlar

### Durum ve klima

| Komut | Parametreler | Açıklama |
|---|---|---|
| `status` | – | Tam durum (aşağıya bakın) |
| `ac.set` | `power?`, `mode?` (cool/heat/dry/fan/auto), `temp?` (16–30, 0.5 adım), `temp_delta?`, `fan?` (auto/min/low/medium/high/max), `swing?` | Klimayı ayarlar. Kapalıyken yalnızca mod/derece verilirse IR gönderilmez, açılış ayarı olarak saklanır |
| `ac.toggle` | – | Aç/kapa |

### Kumanda (IR)

| Komut | Parametreler | Açıklama |
|---|---|---|
| `ir.learn` | `name?`, `role?`, `raw?` | 30 sn öğrenme. Sonuç `learn` olayıyla gelir |
| `ir.learn.cancel` | – | |
| `ir.cmds` | – | `cmds:[{id,name,role,len}]`, `ir:{...}` |
| `ir.cmd.send` | `id` | Ham komutu gönderir |
| `ir.cmd.update` | `id`, `name?`, `role?` | Rol: `off`, `on`, `cool`, `heat`, `dry`, `fan`, `auto` veya `cool:24` gibi |
| `ir.cmd.delete` | `id` | |
| `ir.protocols` | – | Desteklenen klima protokolleri |
| `ir.models` | `protocol` | Protokolün modelleri, ör. FUJITSU_AC → ARRAH2E, ARDB1… |
| `ir.protocol.set` | `protocol`, `model` | Elle protokol seçimi |
| `ir.mode.set` | `raw` (bool) | Ham komut modu |

### Sensörler ve geçmiş

| Komut | Parametreler | Açıklama |
|---|---|---|
| `sensors` | – | `sensors:[...]`, `auto_accept` |
| `sensor.set` | `id` (0 = hub, 1–4), `name?`, `interval_min?` (1–60), `t_off?`, `h_off?` | Aralık, sensörün bir sonraki uyanışında geçerli olur |
| `sensor.remove` | `id` | |
| `history` | `src`, `hours` (1–168), `step_min` | `{src, t0, step, temp:[x10…], hum:[x10…]}`. Eksik değer `null` |
| `events` | `max?` | Klima olay günlüğü |

### Otomasyon

| Komut | Parametreler |
|---|---|
| `auto.get` | – |
| `auto.set` | `thermo?`, `dry?`, `timing?`, `schedules?` (yapı aşağıda) |

```json
{
  "thermo": {"enabled": true, "source": 1, "mode": "cool", "target": 25, "hyst": 0.5, "ac_temp": 24, "fan": "auto"},
  "dry": {"enabled": true, "source": 0, "on": 65, "off": 55, "ac_temp": 24},
  "timing": {"min_on_min": 5, "min_off_min": 5, "override_min": 60},
  "schedules": [
    {"enabled": true, "days": 31, "time": "07:00", "action": "on", "mode": "cool", "temp": 24},
    {"enabled": true, "days": 127, "time": "23:30", "action": "off"}
  ]
}
```

- `source`: 0 = hub, 1–4 = sensör, 255 = ortalama.
- `days`: bit0 = Pazartesi … bit6 = Pazar (127 = her gün, 31 = hafta içi).
- `action`: `on`, `off`, `thermo_on`, `thermo_off`, `dry_on`, `dry_off`.

### WiFi

| Komut | Parametreler | Açıklama |
|---|---|---|
| `wifi.list` | – | Kayıtlı ağlar ve durum |
| `wifi.add` | `ssid`, `pass` | En başa ekler, hemen bağlanmayı dener (en fazla 3 ağ) |
| `wifi.remove` | `ssid` | |
| `wifi.scan` | – | Sonuç `wifi.scan` olayıyla gelir |
| `wifi.reconnect` | – | |

### Telegram

| Komut | Parametreler |
|---|---|
| `telegram.get` | – |
| `telegram.set` | `enabled?`, `token?`, `chats?` (dizi), `notify?` `{battery, offline, power, wifi, auto}`, `insecure?` |
| `telegram.test` | – |

### Ayarlar ve sistem

| Komut | Parametreler |
|---|---|
| `settings.get` | – |
| `settings.set` | `name?`, `pin?`, `tz?`, `ble_always?`, `auto_accept?`, `led?` `{brightness, night, night_start, night_end}`, `ac_default?`, `hub_sensor?` `{enabled, name, t_off, h_off}` |
| `time.set` | `epoch` (Unix saniye) |
| `system.info` | – |
| `system.reboot` | – |
| `system.factory_reset` | `confirm: true` |
| `system.ap` | `minutes` (0 = kapat) |
| `ble.on` | `minutes` ("gerektiğinde" Bluetooth modunda) |

## Olaylar

| Olay | Ne zaman |
|---|---|
| `status` | Durum değişince (en fazla saniyede bir) |
| `learn` | Öğrenme sonucu: `{ok, kind:"protocol", protocol, model, model_name, desc}` veya `{ok, kind:"raw", id, name, role, len}` veya `{ok:false, error}` |
| `notify` | Bildirim metni (Telegram'a gidenlerle aynı) |
| `wifi.scan` | `{networks:[{ssid, rssi, ch, open}]}` |

## `status` yapısı (özet)

```json
{
  "hub": {"name": "KlimaHub", "fw": "1.0.0", "uptime": 86400, "time": 1761000000, "time_valid": true,
          "wifi": {"state": "connected", "ssid": "Ev", "rssi": -60, "ip": "192.168.1.50", "ch": 6,
                   "outage_s": 0, "reconnects": 3, "retry_in": 0, "internet": true},
          "ap": {"on": false, "ssid": "KlimaHub-A1B2"}, "ble": {"on": true, "clients": 1},
          "power": {"usb": true, "battery": true, "batt_mv": 4150, "batt_pct": 95, "low": false},
          "telegram": {"enabled": true, "ok": true, "error": "", "chats": 1, "queued": 0}},
  "sensors": [{"id": 0, "kind": "hub", "name": "Hub", "online": true, "temp": 24.6, "hum": 55.1, "age_s": 12},
              {"id": 1, "kind": "sensor", "name": "Salon", "online": true, "temp": 25.3, "hum": 61,
               "batt_pct": 72, "batt_mv": 3950, "usb": false, "rssi": -67, "interval_min": 10}],
  "ac": {"state": {"power": true, "mode": "cool", "temp": 24, "fan": "auto", "swing": false},
         "src": "thermostat", "changed": 1761000000,
         "ir": {"mode": "protocol", "protocol": "FUJITSU_AC", "model": 1, "ready": true, "learning": false}},
  "auto": {"thermo": {"enabled": true, "want": "keep", "value": 25.3}, "dry": {"enabled": false},
           "owner": "thermostat", "deferred": false, "override_s": 0}
}
```

## HTTP uç noktaları

| Yol | Açıklama |
|---|---|
| `GET /` | Web uygulaması (gzip, yazılıma gömülü) |
| `GET /api/status?pin=1234` | Durum JSON'u |
| `POST /update?pin=1234` | Firmware yükleme (multipart, `firmware` alanı) |
| `ws://…/ws` | WebSocket API |

Kablosuz güncelleme PlatformIO ile de yapılabilir:

```
pio run -e hub-ota -t upload
```

`platformio.ini` içindeki `--auth=` değeri hub PIN'iyle aynı olmalıdır.

## Donanımsız deneme

`app/tools/mock_hub.py` gerçek hub'ı taklit eder. Arayüzü bilgisayarda denemek için:

```
pip install aiohttp
python3 app/tools/mock_hub.py
# tarayıcıda http://localhost:8080  (PIN 1234)
```
