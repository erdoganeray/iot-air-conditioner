# Kurulum

Kartlar lehimlendikten sonra yazılımı yükleme, telefonu bağlama, WiFi, kumanda öğretme,
sensörler ve Telegram. Montaj: [MONTAJ.md](MONTAJ.md).

## 1. Yazılımı yükleme

Her XIAO'ya bir kez USB ile yazılım yüklenir. Sonraki hub güncellemeleri kablosuz yapılabilir.

### Yol A: Tarayıcıdan (en kolay, kurulum gerektirmez)

Bilgisayarda **Chrome** veya **Edge** gerekir (Android telefonda çalışmaz).

1. Yüklenecek dosyalar:
   - Depodaki hazır dosyalar: `firmware/bin/klimahub-hub-full.bin` ve
     `firmware/bin/klimahub-sensor-full.bin`.
   - Kodda değişiklik yapıldıysa en güncel dosyalar: GitHub → **Actions** → **Firmware** →
     son çalıştırma → **Artifacts → firmware** (zip). İçindeki `*-firmware-full.bin`'i kullanın.
2. https://espressif.github.io/esptool-js/ adresini açın.
3. XIAO'yu USB-C ile bağlayın, **Connect**'e basın ve listeden kartı seçin.
   - Kart listede yoksa veya bağlanmıyorsa: XIAO'daki **B (BOOT)** düğmesini basılı tutarak
     USB'yi takın, sonra bırakın ve tekrar deneyin.
4. **Flash Address**: `0x0`, dosya: `...-full.bin`. **Program**'a basın.
5. Bitince kartın **R (RESET)** düğmesine basın veya USB'yi çıkarıp takın.

Hub için `hub`, sensörler için `sensor` dosyasını kullanın. Karıştırmayın: hub dosyası
ESP32-C3'e yüklenmez (araç uyarı verir).

### Yol B: PlatformIO (VS Code)

```bash
# Hub
cd firmware/hub
pio run -t upload          # derle ve yükle
pio device monitor         # günlük (log) izleme, 115200 baud

# Sensör (her biri için)
cd firmware/sensor
pio run -t upload
```

VS Code'da: `firmware/hub` klasörünü açın, alttaki PlatformIO çubuğundan **→ (Upload)**.
Sensör yalnızca USB takılıyken günlük yazar.

### Sonraki güncellemeler (yalnız hub)

- **Uygulamadan:** Ayarlar → 🛠️ Sistem → **Yazılım güncelle** → `klimahub-hub-ota.bin`
  (Actions'tan indirdiyseniz `hub-firmware.bin`; birleşik `-full` dosyası **değil**) → Yükle.
  Yalnız WiFi bağlantısında, `http://klima.local` sayfasından çalışır.
- **PlatformIO ile:** `pio run -e hub-ota -t upload`. `platformio.ini` içindeki `--auth=1234`
  değeri hub PIN'iyle aynı olmalıdır.
- Sensörler kablosuz güncellenmez (pil tasarrufu). USB ile yükleyin.

## 2. Web uygulaması

Uygulama iki yerden açılır:

| Adres | Ne zaman | Bağlantı |
|---|---|---|
| `https://erdoganeray.github.io/iot-air-conditioner/` | Her zaman (modem kapalıyken de) | **Bluetooth** |
| `http://klima.local` veya hub'ın IP adresi | Telefon evin WiFi'ındayken | WiFi (WebSocket) |

**GitHub Pages'i bir kez açmanız gerekir:**

1. GitHub'da depo → **Settings → Pages → Build and deployment → Source: GitHub Actions**.
2. **Actions → Pages → Run workflow** (veya `app/` klasörüne yapılan bir sonraki push) ile
   yayınlanır. Birkaç dakika sonra adres açılır.

**Telefona uygulama gibi kurmak:** Chrome'da adresi açın → ⋮ menü → **Ana ekrana ekle**.

**Bluetooth için:** Android'de Chrome kullanın. Telefonun Bluetooth'u ve konumu açık olmalı;
Chrome ilk bağlantıda izin ister. iPhone'da Safari Web Bluetooth desteklemez.

> Alternatif: `app/` klasörünü https://app.netlify.com/drop sayfasına sürükleyip bırakın. Size
> verilen HTTPS adresi de aynı şekilde çalışır.

## 3. İlk bağlantı ve WiFi

1. Hub'ı USB adaptöre takın. Yeşil LED yavaş yanıp söner: WiFi tanımlı değil.
2. Uygulamayı açın → **🔵 Bluetooth ile bağlan** → listeden **KlimaHub**'ı seçin.
3. PIN sorulur: **1234**. Hemen değiştirin: **Ayarlar → 🔒 Güvenlik → PIN'i değiştir**.
4. **Ayarlar → 📶 WiFi → 🔍 Ağları tara**, ağınızı seçin, şifreyi girin. Hub hemen bağlanmayı
   dener. Yeşil LED sabit (kısık) yanınca bağlantı tamamdır.
   - En fazla 3 ağ kaydedilebilir (ör. modem + tekrarlayıcı). Gizli ağlar için **Elle ekle**.
   - Hub **yalnız 2.4 GHz** ağlara bağlanır.
5. WiFi bağlıyken aynı ağdaki telefondan `http://klima.local` açılır. Android bazı sürümlerde
   `.local` adreslerini açamaz: o zaman **Ayarlar → 📶 WiFi** bölümünde görünen IP adresini
   (ör. `http://192.168.1.50`) kullanın.

**Bluetooth yoksa (kurulum ağı):** Hub'ın dokunmatik butonuna **çift dokunun** (veya
uygulamadan **Kurulum ağını aç**). Hub 10 dk boyunca `KlimaHub-XXXX` adlı bir ağ açar, şifre
`klima1234`. Telefonu bu ağa bağlayıp `http://192.168.4.1` adresini açın. WiFi bilgisi hiç
girilmemişse kurulum ağı açılışta kendiliğinden açılır. İki LED sırayla yanarken kurulum ağı
açıktır.

**WiFi koparsa:** Hub hiç vazgeçmeden yeniden dener (5 sn'den başlayıp en fazla 5 dk aralıkla).
Bu sırada klima kontrolü, sensörler, otomasyon ve Bluetooth çalışmaya devam eder.

## 4. Klima kumandasını öğretme

1. **Kumanda** sekmesi → **🎓 Kumandayı öğret** (veya hub'ın butonuna 3 sn basılı tutun;
   kırmızı LED hızlı yanıp söner).
2. Klimanın kumandasını hub'ın **ön yüzüne** 5–20 cm'den tutun ve bir tuşa basın. En iyisi:
   **Açık, Soğutma, 24 °C**.
3. Hub protokolü tanırsa (Fujitsu ve benzerleri için `FUJITSU_AC`) uygulama bunu gösterir.
   Artık klimanın **her ayarı** (mod, derece, fan, kanat) uygulamadan gönderilebilir.
   - Fujitsu'nun birçok modeli vardır (ARRAH2E, ARDB1, ARREB1E…). Tanınan model düzgün çalışmazsa
     **Protokolü elle seç** ile diğer modelleri deneyin.
4. **Deneme:** Ana sekmede klimayı açıp kapatın. Hub'ı klimanın karşısına, IR LED'ler klimaya
   bakacak şekilde yerleştirin (3–4 m sorun değil).
5. **Protokol tanınmazsa:** **+ Ham komut öğret** ile tuşları tek tek kaydedin ve her kayda bir
   rol verin: `on` (aç), `off` (kapat), `cool`, `dry`, `heat` veya `cool:24` gibi. Otomasyon bu
   rolleri kullanır.

Orijinal kumandayı kullanmaya devam edebilirsiniz: hub sinyali duyarsa uygulamadaki klima
durumu da güncellenir (hub sinyali duyabilecek konumdaysa).

## 5. Sensörleri ekleme

1. Sensöre pili takın (veya USB'ye bağlayın). Sensör hemen bir ölçüm gönderir.
2. **Ayarlar → 🌡️ Sensörler**'de "Yeni sensörleri otomatik kabul et" açıksa sensör listeye
   kendiliğinden eklenir (en fazla 4 sensör). Kapalıysa açın, sensörü yeniden başlatın.
3. Sensörün yanındaki **✏️** ile:
   - **Ad** verin (Salon, Yatak odası…).
   - **Ölçüm aralığı** (1–60 dk, varsayılan 10). Yeni aralık sensörün **bir sonraki
     uyanışında** geçerli olur.
   - Sıcaklık/nem düzeltmesi (bir referans termometreye göre).
4. Hub'ın kendi sensörü **Ayarlar → Hub sensörü**'ndedir. Hub ısındığı için sıcaklık düzeltmesi
   genelde −1 … −3 °C arasındadır; bir termometreyle karşılaştırıp ayarlayın.

Pil ömrü: 10 dk aralıkla 500 mAh pil 6–9 ay dayanır. Şarj için sensörü duvardan sökmeden
alttan USB-C takın.

## 6. Otomasyon

**Otomasyon** sekmesi:

- **🤖 Termostat:** Kaynak sensör (hub, bir sensör veya ortalama), hedef sıcaklık, eşik payı,
  klimaya gönderilecek derece. Soğutmada sıcaklık hedef + eşiği geçince açar, hedef − eşiğin
  altına inince kapatır.
- **💧 Nem (kurutma):** Nem açılma eşiğini (örn. %65) geçince klimayı **kuru (dry)** modda
  açar, kapanma eşiğinin (örn. %55) altına inince kapatır.
- **⏰ Zamanlayıcılar:** Gün + saat + işlem (aç, kapat, termostatı aç/kapat, nemi aç/kapat).
- **⏱️ Koruma süreleri:** Klima en az 5 dk açık / 5 dk kapalı kalır. Elle komut verince
  otomasyon 60 dk bekler.

Saat internetten alınır. İnternet yoksa uygulama Bluetooth ile bağlanınca telefonun saatini
gönderir (**Ayarlar → Telefon saatini gönder**).

## 7. Telegram

1. Telegram'da **@BotFather**'a yazın → `/newbot` → bota bir ad ve kullanıcı adı verin. Size bir
   **token** verir (`123456:ABC…`). Bu token gizlidir, kimseyle ve hiçbir dosyada paylaşmayın.
2. Uygulamada **Ayarlar → ✈️ Telegram**: token'ı yapıştırın, başlığın yanındaki anahtarı açın,
   istediğiniz bildirimleri seçin (🔋 Pil, 📡 Sensör, ⚡ Elektrik, 📶 WiFi, 🤖 Otomasyon) ve
   **Kaydet**. Token yalnızca hub'ın içinde saklanır.
3. Telegram'da kendi botunuzu açıp **`/start 1234`** yazın (1234 yerine hub PIN'iniz). Sohbet
   yetkilendirilir. Ailedeki diğer kişiler de aynı şekilde ekleyebilir.
4. **Test mesajı** ile deneyin.

Komutlar: `/durum`, `/ac 24 sogutma`, `/kapat`, `/sicaklik 24`, `/mod kuru`, `/fan otomatik`,
`/termostat ac`, `/nem 65`, `/sensorler`, `/menu` (butonlu menü), `/yardim`.

Bildirimler: pil zayıf, sensör yanıt vermiyor, adaptör çekildi (elektrik kesintisi), WiFi
kesintisi bitti (süresiyle), otomasyon klimayı açtı/kapattı. İnternet yokken oluşan bildirimler
sıraya alınır, bağlantı gelince gönderilir. Telegram'a bağlanamıyorsa (bazı modemlerde HTTPS
sertifika sorunu) ayarlardaki "Sertifika doğrulamasını kapat" seçeneğini son çare olarak
deneyin.

## 8. LED'ler ve dokunmatik buton

| LED | Anlamı |
|---|---|
| Yeşil sabit (kısık) | Her şey yolunda, WiFi bağlı |
| Yeşil yavaş yanıp sönme | WiFi yok, yeniden deniyor (diğer her şey çalışıyor) |
| Yeşil kısa flaş | IR komutu gönderildi |
| Kırmızı hızlı yanıp sönme | Kumanda öğrenme modu |
| Kırmızı 5 sn'de bir flaş | Bir sensör yanıt vermiyor |
| Kırmızı sabit | Pil kritik |
| Kırmızı-yeşil sırayla | Kurulum ağı açık |

Gece (varsayılan 23:00–07:00) LED'ler kapalıdır; butona dokununca 5 sn yanar. Saatler ve
parlaklık **Ayarlar → ⚙️ Hub**'dan değiştirilir.

| Dokunma | İşlev |
|---|---|
| Tek dokunma | Klimayı aç/kapa |
| Çift dokunma | Kurulum ağını 10 dk aç |
| 3 sn basılı tutma | Kumanda öğrenme |
| 10 sn basılı tutma | Fabrika ayarları (LED'ler hızlanarak uyarır, erken bırakınca iptal) |

## 9. Donanım olmadan deneme

Uygulamayı bilgisayarda sahte bir hub'la deneyebilirsiniz:

```bash
pip install aiohttp
python3 app/tools/mock_hub.py
# tarayıcıda http://localhost:8080  (PIN 1234)
```

Komutların tam listesi: [API.md](API.md). Seri port (USB, 115200 baud) aynı JSON komutlarını
PIN'siz kabul eder, sorun ararken işe yarar.
