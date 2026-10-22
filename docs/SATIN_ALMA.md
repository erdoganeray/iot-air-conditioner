# Satın Alma Listesi (sepet hazır)

Bir hub + iki sensör için, **3 sitede** toplanmış sipariş listesi. Fiyatlar ve stok durumu
**22 Ekim 2026**'da sitelerden okundu (KDV dahil). Fiyatlar sık değişir; sepete eklerken
kontrol edin. Adetlerde lehimde kaybolanlar için birer yedek var.

| Site | Ne alınıyor | Ara toplam | Kargo |
|---|---|---|---|
| [e-Komponent](https://www.e-komponent.com) | XIAO kartları, anten, IR parçaları, transistör, direnç ve kondansatörler | **≈ 2.515 TL** | 2.500 TL üzeri ücretsiz |
| [Robolink Market](https://www.robolinkmarket.com) | SHT30 sensörler, dokunmatik buton, LED'ler, pin header, kablo | **≈ 968 TL** | 1.500 TL altı kargo ücretli |
| [Hepsiburada](https://www.hepsiburada.com) | LiPo piller | **≈ 850 TL** | 300 TL üzeri ücretsiz |
| **Toplam** | | **≈ 4.330 TL** + Robolink kargosu | |

Neden bu üç site? Taradığım sitelerin hiçbiri her şeyi tek başına karşılamıyor:

- XIAO ESP32-S3 Robotistan'da tükenmiş; XIAO kartları stokta yalnız e-Komponent'te.
- Küçük SHT3x modülü stokta yalnız Robolink'te.
- 500 mAh'lik küçük LiPo, elektronik mağazalarının hiçbirinde yok (Direnc.net, Robotistan,
  Robolink, Robo90, e-Komponent, Motorobit, Robotsepeti).

## 1. e-Komponent

> e-Komponent'teki XIAO ve bazı parçalar **"Yurt Dışı Stok"** olarak görünüyor: teslimat
> birkaç gün ile birkaç hafta sürebilir. Sipariş öncesi teslim süresine bakın.

| Ürün | Adet | Birim | Tutar | Proje kalemi |
|---|---|---|---|---|
| [XIAO ESP32-S3 (Pre-Soldered)](https://www.e-komponent.com/102010634-seeed-studio-xiao-esp32-s3) | 1 | 632,01 | 632,01 | Hub kartı. Header'ları lehimli gelir, **anten dahil** |
| [XIAO ESP32-C3 (3'lü paket)](https://www.e-komponent.com/102010572-seeed-studio-xiao-esp32-c3) | 1 | 1.004,22 | 1.004,22 | 2 sensör + 1 yedek. **Anten dahil değil** |
| [FPC Wi-Fi anten, U.FL, yapışkanlı (AC10256-100)](https://www.e-komponent.com/ac10256-100-the-antenna-company-rf-antenna) | 2 | 162,28 | 324,56 | Sensörlerin anteni |
| [Vishay TSAL6400 IR LED 940 nm, 5 mm](https://www.e-komponent.com/tsal6400-vishay-semiconductor-kizilotesi-ir-uv-emitor) | 3 | 35,73 | 107,19 | LED1, LED2 (+1 yedek) |
| [Vishay TSOP38238 IR alıcı 38 kHz](https://www.e-komponent.com/tsop38238-vishay-semiconductor-fotodedektor-uzaktan-alici) | 1 | 60,03 | 60,03 | U2 (VS1838B yerine, bacak sırası aynı) |
| [onsemi PN2222A, TO-92](https://www.e-komponent.com/pn2222ata-onsemi-bipolar-bjt-transistor-npn) | 2 | 22,87 | 45,74 | Q1 (S8050 yerine, bacak sırası aynı: E-B-C) |
| [220 µF 10 V elektrolitik, Ø8 × 5 mm](https://www.e-komponent.com/220%C2%B5f-10v-aluminum-electrolytic-capacitors-radial-can-1000-hrs-85c) | 2 | 37,16 | 74,32 | C2 (+1 yedek). Yalnız 5 mm boyunda, kutuya rahat sığar |
| [10 Ω 1206](https://www.e-komponent.com/rk73h2bttd10r0f-koa-speer-electronics-chip-resistor) | 2 | 7,44 | 14,88 | R5 |
| [150 Ω 1206](https://www.e-komponent.com/150-ohms-chip-resistor-rc1206fr-07150rl) | 2 | 7,60 | 15,20 | R7 |
| [470 Ω 1206](https://www.e-komponent.com/rc1206jr-07470rl-yageo-chip-resistor) | 3 | 7,15 | 21,45 | R9, R10 |
| [10 kΩ 1206](https://www.e-komponent.com/rc1206jr-0710kl-yageo-chip-resistor) | 2 | 7,15 | 14,30 | R8 |
| [100 kΩ 1206](https://www.e-komponent.com/100-kohms-1-025w-1-4w-chip-resistor-1206-3216-metric-automotive-aec-q200-thick-film) | 4 | 7,15 | 28,60 | Hub R3, sensör R3 ×2 |
| [150 kΩ 1206](https://www.e-komponent.com/150-kohms-chip-resistor-rmcf1206ft150k) | 4 | 6,97 | 27,88 | Hub R4, sensör R4 ×2 |
| [220 kΩ 1206](https://www.e-komponent.com/chip-resistor-moisture-resistant-thick-film-20918) | 3 | 7,15 | 21,45 | Hub R1, R2 |
| [470 kΩ 1206](https://www.e-komponent.com/470-kohms-chip-resistor-ac1206jr-07470kl) | 5 | 7,53 | 37,65 | Sensör R1, R2 ×2 |
| [100 nF 1206 X7R](https://www.e-komponent.com/ceramic-capacitors-22202) | 7 | 12,15 | 85,05 | Hub C1, C3, C5; sensör C1 ×2 |
| | | | **2.514,53** | |

## 2. Robolink Market

| Ürün | Adet | Birim | Tutar | Proje kalemi |
|---|---|---|---|---|
| [SHT30 Sıcaklık ve Nem Sensörü](https://www.robolinkmarket.com/sht30-sicaklik-ve-nem-sensoru) | 3 | 277,50 | 832,50 | Hub + 2 sensör (mor GY-SHT3x modülü, ≈14 × 11 mm) |
| [TTP223 Kapasitif Dokunmatik Sensör (11.5 × 8 mm)](https://www.robolinkmarket.com/ttp223-kapasitif-dokunmatik-sensor) | 2 | 9,00 | 18,00 | Hub butonu (+1 yedek) |
| [3 mm Kırmızı LED, 10 adet](https://www.robolinkmarket.com/3mm-kirmizi-led-10-adet) | 1 | 19,50 | 19,50 | LED3 |
| [3 mm Yeşil LED, 10 adet](https://www.robolinkmarket.com/3mm-yesil-led-10-adet) | 1 | 19,50 | 19,50 | LED4 |
| [40 pin erkek header](https://www.robolinkmarket.com/40-pin-erkek-header) | 2 | 10,50 | 21,00 | 2 adet XIAO ESP32-C3 için (S3 lehimli geliyor) |
| [40'lı ayrılabilen erkek-erkek jumper kablo, 20 cm](https://www.robolinkmarket.com/40-pin-ayrilabilen-erkek-erkek-jumper-kablo-200mm) | 1 | 57,00 | 57,00 | Uçları kesilip XBAT (J2), modül (J3) ve SHT30 kabloları olarak kullanılır |
| | | | **967,50** | |

## 3. Hepsiburada

| Ürün | Adet | Birim | Tutar | Proje kalemi |
|---|---|---|---|---|
| [Powermaster PM-21333 3.7 V 500 mAh 503030 Li-Po (satıcı: Zibox)](https://www.hepsiburada.com/powermaster-pm-21333-3-7v-500mah-503030-3-5mm-soketli-lityum-polimer-li-po-batarya-sarj-edilebilir-pil-pm-HBC0000JF95JT) | 3 | 283,23 | 849,69 | Hub + 2 sensör. Koruma devreli (PCM), 5 × 30 × 30 mm |

Aynı pili birçok satıcı 283–775 TL arasında satıyor; ilan adı aynı. En ucuz satıcı Zibox'tu.

## Tasarımdakinden farklı alınanlar

| Tasarımda | Alınan | Etkisi |
|---|---|---|
| SHT31 modülü | **SHT30** modülü | Aynı aile, aynı komutlar ve adres (0x44/0x45). Yazılım değişmeden çalışır, hassasiyet biraz düşük (±0.3 °C, ±3 %RH) |
| VS1838B | **TSOP38238** (Vishay) | Bacak sırası aynı (OUT, GND, VCC), daha kaliteli |
| S8050 | **PN2222A** (onsemi) | Bacak sırası aynı (E, B, C), 1 A |
| IR LED (genel) | **TSAL6400** | 940 nm, 100 mA, ±25° |
| 220 µF Ø6.3 × 7 mm | 220 µF **Ø8 × 5 mm** | Bacak aralığı aynı (2.5 mm). Daha geniş ama daha alçak; çevresinde yer var |
| 503035 LiPo, JST-PH fişli | **503030** LiPo, **küçük soketli** (3.5 mm genişlik, büyük ihtimalle 1.25 mm aralıklı) | 5 mm daha kısa, kutuya rahat girer. **Fişi JST-PH değil:** fişi kesip kırmızı kabloyu J1'in **+** deliğine, siyahı diğerine doğrudan lehimleyin (önce birini kesip lehimleyin, sonra diğerini; iki uç asla aynı anda açıkta kalmasın). Bu durumda J1 soketi gerekmez |
| XIAO ESP32-C3 tekli | **3'lü paket** | Bir yedek sensör kartı olur. Tek tek satan stoklu bir site bulunamadı |

## Bu listede olmayanlar (genel malzeme)

- **M2 × 10 vida, 3 adet:** herhangi bir hırdavatçıdan. Bu üç sitede tek tek satılmıyor; Hepsiburada'da
  150'lik paket ≈ 395 TL, Robotistan'da tanesi 1,60 TL.
- Çift taraflı köpük bant (VHB), kauçuk ayak, PLA, lehim teli ve flux.
- 5 V USB-C adaptör + kablo (evdeki bir telefon şarj aleti yeter).
- **PCB siparişi** (JLCPCB vb.): [pcb/fab/README.md](../pcb/fab/README.md).

## Taranan siteler

Direnc.net, Robotistan, Robolink, Robo90, e-Komponent, RoboCombo, Motorobit, Robotsepeti,
Hepsiburada.

- **Özdisan:** kurumsal dağıtıcı, fiyatların çoğu talep üzerine; listeye alınmadı.
- **Trendyol, n11, Akakçe:** otomatik erişimi engelliyor, taranamadı.
- **Direnc.net:** pasifler en ucuz burada (1206 direnç ≈ 0,28 TL), ama XIAO, SHT3x ve pil
  bulunmadığı için sepette dördüncü site olurdu.

  İsterseniz e-Komponent'teki pasifleri (≈ 270 TL) Direnc.net'ten ≈ 15 TL'ye alabilirsiniz;
  ancak e-Komponent sepeti 2.500 TL'nin altına düşer ve kargo ücretli olur.
