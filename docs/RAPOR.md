# Sabah Raporu: 21 Ekim 2026

İstediğin her şey hazır ve `main` dalında: hub ve sensör yazılımları, Türkçe telefon uygulaması,
Telegram botu, KiCad kartları, Candle için G-code, 3D kutular, malzeme listesi ve belgeler.
**Hiçbiri gerçek donanımda denenmedi** (elimde kart yoktu). Neyin nasıl doğrulandığı aşağıda.

## 1. Ne hazır

| Parça | Nerede | Nasıl doğrulandı |
|---|---|---|
| Hub yazılımı (XIAO ESP32-S3) | `firmware/hub/` | Gerçek ESP32 derleyicisiyle derlendi (flash %78.7). 20 birim testi geçiyor. GitHub Actions her push'ta derliyor, hepsi yeşil |
| Sensör yazılımı (XIAO ESP32-C3) | `firmware/sensor/` | Derlendi (flash %32.5) |
| Hazır yazılım dosyaları | `firmware/bin/` | Tarayıcıdan 0x0 adresine yüklenir; başlıklar (8 MB / 4 MB) kontrol edildi |
| Telefon uygulaması | `app/` + hub'ın içine gömülü | Sahte hub'la, başsız tarayıcıda otomatik testle. Ekran görüntüleri `docs/images/app/` |
| Telegram botu | Hub yazılımında | Derlendi. Gerçek Telegram'la denenmedi |
| Hub kartı (54 × 42 mm) | `pcb/hub/` | Tek yüzlü, **köprü telsiz**, bağlanmamış hat 0, bakır DRC hatası 0 |
| Sensör kartı (25 × 44 mm) | `pcb/sensor/` | Aynı. Isıl yarık ve M2 vida deliği var |
| G-code (Candle) | `pcb/cnc/<kart>/*.nc` | Frezeleme sonrası kalan bakır hesaplandı: **kısa devre 0, kopukluk 0**. Komutlar GRBL uyumlu |
| Gerber + Excellon | `pcb/cnc/<kart>/gerber/` | KiCad'den |
| Montaj haritaları | `pcb/cnc/<kart>/montaj_ust.png`, `montaj_alt.png` | Karttan üretildi: yön işaretleri, SMD değerleri |
| Hub kutusu (61.4 × 52 × 24.4 mm) | `enclosure/hub/` | Kart, pil, XIAO, LED'ler, USB fişi vb. temsili gövdelerle **çakışma denetimi temiz** |
| Sensör kutusu (33.8 × 48.6 × 24 mm) | `enclosure/sensor/` | Aynı |
| 3D görüntüleyici | `enclosure/onizleme/*_3d.html` | Tarayıcıda aç, döndür, patlat, kesit al |
| Belgeler | `docs/` | Malzeme listesi, CNC, montaj, kurulum, mimari, API |

## 2. Senin yapacakların (sırayla)

1. **GitHub Pages'i aç:** GitHub → Settings → Pages → Source: **GitHub Actions**. Sonra
   Actions → **Pages** → **Run workflow**. Uygulama şu adreste açılır:
   https://erdoganeray.github.io/iot-air-conditioner/
2. **CNC uçlarını bana yaz:** V-uç açısı ve uç çapı, matkap uçların, kesim frezesinin çapı,
   iş milinin en yüksek devri. G-code'u varsayımlarla hazırladım (30°, 0.2 mm uç, 0.1 mm
   derinlik, 12000 devir; ayrıntı `docs/CNC.md`). Değerleri söylersen yeniden üretirim, ya da
   tek komutla kendin üretirsin.
3. **Siparişi ver:** [MALZEME_LISTESI.md](MALZEME_LISTESI.md). Özellikle bunlara dikkat et:
   - XIAO'ların **antenli** gelmesi.
   - 220 µF kondansatör **en fazla 7 mm** boyunda olmalı.
   - TTP223'ün **küçük** (≈15 × 11 mm) modülü.
4. **Önce bir sensör kutusu bas:** Küçük ve hızlı basılır. Tırnakların oturuşunu ve kartın
   yerleşimini parçalar gelince denersin. Sıkı veya gevşekse `FIT` / `BUMP` ayarını söyle,
   güncellerim.
5. **Kart → montaj → yazılım → kurulum:** [CNC.md](CNC.md) → [MONTAJ.md](MONTAJ.md) →
   [KURULUM.md](KURULUM.md). Pil fişinin kutuplarını takmadan önce ölç!
6. **Klimanın kumandası:** Öğrenme sırasında protokol otomatik tanınmazsa kumandanın marka ve
   model yazısını söyle.

## 3. Gece benim verdiğim kararlar (gözden geçir)

- **Pin eşlemesi değişti:** Tek yüzlü kartta izler kesişmesin diye hub ve sensör pinleri
  yeniden atandı. Yazılım ve belgeler güncel (`docs/MIMARI.md` §3–4).
- **IR LED'ler seri bağlı ve doğrudan pilden besleniyor.** Diyot gerekmedi, pilde de çalışıyor.
  Bunun bedeli: **hub'da pil takılı olmalı**, yoksa IR gönderilmez.
- **Hub pili kartın altında.** Ara plaka pili lehim uçlarından ayırıyor. Kutu böylece en dar
  (61 × 52 mm) ama 24 mm yüksek.
- **Hub'da tek modül konnektörü (J3, 7 pin):** TTP223 ve SHT31 kabloları buraya lehimleniyor.
- **SHT31 ayrı havalandırmalı odacıklarda:** hub'da sağ önde, sensörde üstte (kartın yarığından
  geçen perdeyle ayrılmış). Hub yine de birkaç derece yüksek ölçebilir; uygulamada düzeltme
  ayarı var.
- **Sensör kartına M2 delik ekledim.** Arkadan giren vida kartı ve kapağı birlikte sıkıştırıyor,
  vida başı duvar bandının altında kalıyor.
- **Kapaklar:** 4 tırnak + 1 adet M2 × 10 vida. Hub'da vida alttan girer.
- **XIAO'lar erkek pin header ile lehimlenir** (2.5 mm ara). Dişi soket kullanırsan kutu kapanmaz.
- **SHT31 sürücüsü** hem 0x44 hem 0x45 adresini dener (modül farkı sorun olmasın).
- **Hazır .bin dosyalarını** depoya koydum. PlatformIO'suz, tarayıcıdan yükleyebilesin diye.
  Kod değişirse güncelini Actions üretir.

## 4. Bilinen riskler

| Konu | Durum | Ne yapılır |
|---|---|---|
| Gerçek donanım testi | Hiçbir parça çalışırken denenmedi | İlk açılışta seri port günlüğünü (115200) izle. Sorun olursa günlüğü bana gönder |
| Bluetooth + WiFi + ESP-NOW aynı anda | Hub radyoyu paylaşıyor. Sensör paketleri 400 ms boyunca tekrar gönderiliyor | Sensör verisi kaçarsa Ayarlar → "Bluetooth her zaman açık" seçeneğini kapat |
| Parça ölçüleri | Kutu bazı varsayımlarla çizildi: JST soket + fiş ≈10 mm, modül boyutları, 503035 ≤ 39 mm (koruma devresi dahil) | Parçalar gelince ölç. Farklıysa söyle |
| IR menzili | Seri LED'ler ≈80–140 mA darbe. 3–4 m için yeterli olmalı | Yetmezse R5'i 10 Ω'dan 4.7 Ω'a düşür |
| Dokunmatik | TTP223 1 mm PLA'nın arkasından algılamalı | Algılamazsa modülü çukurun altına daha sıkı yapıştır |
| Android'de `klima.local` | Bazı sürümler açmıyor | Uygulamada görünen IP adresini kullan |
| 3.175 mm freze | Sensör yarığı (2.2 mm) ve vida deliği bu frezeye dar | 1.0 / 2.0 mm freze dosyasını kullan veya elle aç |
| Kurulum ağı şifresi | Sabit: `klima1234`. Ağ yalnız WiFi ayarı yokken veya istenince 10 dk açılır, komutlar ayrıca PIN ister | Varsayılan PIN'i (1234) mutlaka değiştir |

## 5. Dallar

Hepsi push'landı, `develop` ve `main`'e birleştirildi. PR açılmadı.
`claude/sleepy-heisenberg-03o7yb` dalına hiç dokunulmadı.

| Dal | İçerik |
|---|---|
| `main` | Kararlı sürüm (her şey) |
| `develop` | Birleştirme dalı |
| `feature/architecture` | Mimari belgesi |
| `feature/sensor-firmware` | Sensör yazılımı + ortak protokol |
| `feature/hub-firmware` | Hub yazılımı + birim testleri |
| `feature/web-app` | Telefon uygulaması, API belgesi, CI |
| `feature/pcb` | Kartlar, G-code, Gerber |
| `feature/enclosure` | 3D kutular |
| `feature/docs` | Malzeme listesi, montaj, kurulum, CNC, bu rapor |
