# Malzeme Listesi

Bir hub + iki sensör için gereken her şey. Parçaların hepsi Türkiye'deki elektronik
mağazalarında (Direnc.net, Robotistan, Robolink Market, Motorobit, Samm Market, Özdisan,
Komponentci vb.) bulunur. Fiyatlar sık değiştiği için yazılmadı. **Arama terimi** sütunu,
mağazanın arama kutusuna yazabileceğiniz ifadedir.

> Pasif elemanlar (direnç, kondansatör) ucuzdur. Lehimlerken kaybolanlar için listedeki
> adetlerin **en az iki katını** alın.

## 1. Ana kartlar ve modüller

| # | Parça | Adet | Arama terimi | Not |
|---|---|---|---|---|
| 1 | Seeed Studio **XIAO ESP32-S3** | 1 | `XIAO ESP32S3` | Hub. "Sense" (kameralı) sürümü **değil**, düz sürüm. 8 MB flash + 8 MB PSRAM |
| 2 | Seeed Studio **XIAO ESP32-C3** | 2 | `XIAO ESP32C3` | Sensörler |
| 3 | 2.4 GHz U.FL (IPEX) FPC anten | 3 | `U.FL FPC anten 2.4GHz` | XIAO kutusundan genelde çıkar. Ürün sayfasında "anten dahil" yazmıyorsa ayrıca alın. **Antensiz WiFi/BLE çalışmaz** |
| 4 | LiPo pil **503035**, 500 mAh, 3.7 V, koruma devreli, JST-PH 2.0 fişli | 3 | `503035 lipo pil 500mAh JST` | Ölçü 5 × 30 × 35 mm (+ koruma devresi en fazla 4 mm). **Kutuplar ters gelebilir, aşağıdaki uyarıya bakın** |
| 5 | **SHT31** sıcaklık/nem modülü (GY-SHT31-D) | 3 | `SHT31 sıcaklık nem sensörü modülü` | I²C. Yaklaşık 14 × 11 mm. 4 veya 6 pinli olabilir, ikisi de olur |
| 6 | **TTP223** dokunmatik buton modülü, küçük (3 pin) | 1 | `TTP223 dokunmatik sensör modülü` | ≈15 × 11 mm. Büyük (24 × 24 mm) modül kutuya sığmaz |

## 2. Hub kartı elemanları

| Ref | Değer | Kılıf | Adet | Arama terimi | Not |
|---|---|---|---|---|---|
| LED1, LED2 | IR LED 940 nm, 5 mm | THT | 2 | `IR LED 940nm 5mm` | TSAL6200 bulursanız daha iyi (daha güçlü, dar açı) |
| U2 | VS1838B IR alıcı, 38 kHz | THT | 1 | `VS1838B` | TSOP4838 de olur (aynı bacak sırası: OUT, GND, VCC) |
| Q1 | S8050 NPN transistör | TO-92 | 1 | `S8050 transistör` | Yerine 2N2222A olur (aynı E-B-C sırası). BC337'nin sırası ters (C-B-E), onu kullanırsanız 180° döndürün |
| LED3 | Kırmızı LED, 3 mm, mat (difüz) | THT | 1 | `3mm kırmızı led difüz` | |
| LED4 | Yeşil LED, 3 mm, mat (difüz) | THT | 1 | `3mm yeşil led difüz` | |
| C2 | 220 µF, 6.3 V veya 10 V elektrolitik | Ø6.3 mm, **en fazla 7 mm yükseklik**, bacak aralığı 2.5 mm | 1 | `220uF 10V 6.3x7` | 11 mm boyundaki kutuya sığmaz |
| R5 | 10 Ω | 1206 SMD | 1 | `1206 smd direnç 10 ohm` | IR LED akımı |
| R7 | 150 Ω | 1206 | 1 | `1206 smd direnç 150 ohm` | |
| R9, R10 | 470 Ω | 1206 | 2 | `1206 smd direnç 470 ohm` | Durum LED'leri. Daha kısık isterseniz 1k |
| R8 | 10 kΩ | 1206 | 1 | `1206 smd direnç 10K` | |
| R3 | 100 kΩ | 1206 | 1 | `1206 smd direnç 100K` | |
| R4 | 150 kΩ | 1206 | 1 | `1206 smd direnç 150K` | |
| R1, R2 | 220 kΩ | 1206 | 2 | `1206 smd direnç 220K` | |
| C1, C3, C5 | 100 nF | 1206 SMD | 3 | `1206 smd kondansatör 100nF` | |
| J1 | JST-PH 2.0 mm, 2 pin, dik erkek (B2B-PH-K) | THT | 1 | `JST PH 2.0 2 pin dik konnektör` | Pil fişi buna takılır |
| — | 2.54 mm erkek pin header 1×40 | THT | 1 | `2.54mm erkek pin header` | XIAO için 2 × 7 pin kesilir |

## 3. Sensör kartı elemanları (her sensör için)

| Ref | Değer | Kılıf | Adet (1 sensör) | Adet (2 sensör) | Not |
|---|---|---|---|---|---|
| R1, R2 | 470 kΩ | 1206 | 2 | 4 | `1206 smd direnç 470K` |
| R3 | 100 kΩ | 1206 | 1 | 2 | |
| R4 | 150 kΩ | 1206 | 1 | 2 | |
| C1 | 100 nF | 1206 | 1 | 2 | |
| J1 | JST-PH 2.0 2 pin dik | THT | 1 | 2 | |
| — | Pin header | THT | 2 × 7 | 2 × 14 | Hub'la aynı şeritten |

## 4. Toplu pasif eleman listesi (hub + 2 sensör)

Mağazada tek seferde sepete eklemek için:

| Değer (1206) | Gereken | Önerilen alım |
|---|---|---|
| 10 Ω | 1 | 10 |
| 150 Ω | 1 | 10 |
| 470 Ω | 2 | 10 |
| 10 kΩ | 1 | 10 |
| 100 kΩ | 3 | 10 |
| 150 kΩ | 3 | 10 |
| 220 kΩ | 2 | 10 |
| 470 kΩ | 4 | 10 |
| 100 nF kondansatör | 5 | 20 |

## 5. Kablo, mekanik ve sarf

| Parça | Adet | Arama terimi | Not |
|---|---|---|---|
| İnce silikon kablo, 26–28 AWG (kırmızı + siyah + bir renk daha) | 1 m her biri | `28 AWG silikon kablo` | XBAT (J2) ve hub modül (J3) kabloları |
| M2 × 10 vida (havşa veya silindir başlı) | 3 (+ yedek) | `M2x10 vida` | Kutu kapakları. PLA'ya kendi yolunu açar |
| Kendinden yapışkanlı kauçuk ayak, 8 mm | 4 | `yapışkanlı kauçuk ayak` | Hub altı, isteğe bağlı |
| Çift taraflı köpük bant (VHB) | 1 rulo | `3M VHB çift taraflı bant` | Sensörleri duvara yapıştırmak için |
| PLA filament | ≈70 g | — | 3 kutunun toplamı |
| Sıcak silikon | birkaç çubuk | — | SHT31 ve TTP223 modüllerini sabitlemek için |
| İzole bant veya makaron | az | `daralan makaron` | Kablo uçları |
| 5 V USB-C adaptör (≥1 A) + USB-C kablo | 1 | — | Hub sürekli buna bağlı çalışır. Sensörler arada bir şarj edilir |

## 6. Bakır plaket (CNC)

> Kartları fabrikaya ürettirirseniz ([pcb/fab/README.md](../pcb/fab/README.md)) 6. ve 7. bölümler gerekmez.

| Parça | Adet | Arama terimi | Not |
|---|---|---|---|
| **Tek yüzlü** bakır plaket, 1.6 mm, 10 × 15 cm | 1–2 | `tek yüzlü bakır plaket 10x15` | Üç kart (62×50 + 2 × 33×52 mm) bir plakaya sığar. İkinciyi deneme için alın |

**FR1 (pertinaks) mi, FR4 (cam elyaf) mi?** FR1 daha kolay işlenir ve uçları daha az
köreltir. FR4 daha sağlamdır, ama tozu sağlığa zararlıdır: frezelerken mutlaka elektrik
süpürgesiyle tozu çekin ve maske takın. İkisi de bu tasarım için uygundur.

## 7. CNC uçları

Uç ölçülerinizi henüz bilmediğiniz için dosyalar varsayılan değerlerle üretildi. Elinizdeki
uçlara göre yeniden üretmek kolaydır (`CNC.md`).

| Uç | Ne için | Arama terimi | Not |
|---|---|---|---|
| V-uç, 0.1–0.2 mm uç çapı, 20° veya 30° | İzolasyon | `PCB V uç gravür freze 30 derece 0.2mm` | Dosyalar 0.2 mm / 30° varsayar |
| Matkap ucu 0.8 mm | LED, transistör, kondansatör delikleri | `PCB matkap ucu 0.8mm karbür` | |
| Matkap ucu 1.0 mm | Pin header, XIAO, JST delikleri | `PCB matkap ucu 1.0mm karbür` | Tek uçla her şeyi delmek isterseniz yalnız 1.0 mm yeter |
| Parmak freze 1.0 / 2.0 mm veya 3.175 mm | Kart kenarı kesimi | `karbür parmak freze 2mm` | Biri yeterli. Sensördeki 2.2 mm yarığı ve vida deliğini yalnızca 1.0 ve 2.0 mm frezeler açabilir |

## 8. Aletler

- Havya (sıcaklık ayarlı, 320–350 °C), 0.6–0.8 mm lehim teli, **flux** (SMD için çok önemli)
- **Multimetre (zorunlu):** pil fişinin kutuplarını ölçmek için
- Cımbız, yan keski, kablo sıyırıcı
- Sıcak silikon tabancası
- 3D yazıcı (PLA, 0.4 mm nozül)

## ⚠️ Pil kutup uyarısı

JST-PH fişli LiPo pillerde **kırmızı kablonun hangi tarafta olduğu satıcıya göre değişir**.
Ters takılan pil XIAO'yu anında yakar.

1. Karttaki J1 soketinin **1 numaralı** bacağı **+** uçtur. Alt yüzde bu ped köşeli (yuvarlatılmış
   dikdörtgen), 2 numaralı ped ise ovaldir. Yerleşim görsellerinde de J1 yönü görülür
   (`pcb/cnc/<kart>/yerlesim_ust.png`).
2. Pilin fişini multimetreyle ölçün: kırmızı prob fişin hangi deliğine değdiğinde **+3.7 V**
   (eksi işaretsiz) görüyorsanız, o delik J1'in kare pedine gelmeli.
3. Ters ise: fişin içindeki iki metal ucu bir iğneyle tırnaklarını kaldırarak çıkarıp yer
   değiştirin. Ya da pil kablolarını fişten kesip J1'in deliklerine doğru kutupla doğrudan
   lehimleyin (önce bir kabloyu kesip lehimleyin, sonra diğerini; iki ucu asla aynı anda açık
   bırakmayın).
