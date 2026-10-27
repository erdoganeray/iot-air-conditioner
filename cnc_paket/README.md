# CNC Kart Üretim Paketi (KlimaHub)

Bu paket, iki küçük elektronik kartın (**hub** ve **sensör**) CNC'de bakır plakadan
üretilmesi için gereken her şeyi içerir: kazıma (izolasyon), delik delme ve kart kesimi.

> **Yapay zekaya not:** Bu dosyayı baştan sona oku. Görevin, aşağıdaki kaynak dosyalardan
> **bizim tezgahımıza uygun** G-code üretmek ya da `3_hazir_gcode_GRBL/` altındaki hazır
> G-code'u bizim tezgahımıza uyarlamak. Önce "6. Tezgah bilgileri" bölümündeki soruların
> cevabını mekanikçiden al; cevaplar olmadan G-code üretme.

---

## 1. Kartlar hakkında temel bilgiler

| | Hub | Sensör |
|---|---|---|
| Kart ölçüsü | 54 × 42 mm | 25 × 44 mm |
| Plakada kapladığı alan (4 mm pay dahil) | 62 × 50 mm | 33 × 52 mm |
| Delikler | 13 × Ø0.8 mm, 28 × Ø1.0 mm | 22 × Ø1.0 mm, 1 × Ø2.2 mm (vida, kaplamasız) |
| İç kesim | yok | 8.0 × 2.2 mm yarık |

- İki kart da **tek yüzlü**. Bakır yalnızca **alt** yüzde (KiCad'de `B.Cu`).
- Plaka: tek yüzlü bakır plaket, **1.6 mm**, FR4 veya FR1.
- Kartlar 10 × 15 cm'lik tek bir plakaya sığar (2 sensör + 1 hub bile sığar).

### ⚠️ En önemli kurallar

1. **AYNALAMA:** Plaka tezgaha **bakır yüzü yukarı** bakacak şekilde bağlanır ve tüm işlemler
   (kazıma, delme, kesim) **tek bağlamada** yapılır. Gerber dosyaları KiCad'in **üstten
   görünüşündedir**. Bu yüzden bakır, delik ve kesim katmanlarının **hepsi aynı eksende (X)
   aynalanmalıdır**. `2_gorseller/bakir_alt_aynali.png` doğru (aynalanmış) sonucu gösterir:
   üretilen G-code'un önizlemesi bu resimle **aynı** görünmelidir.
   Hazır G-code'lar zaten aynalanmıştır.
2. **İzler arası en dar boşluk 0.45 mm.** İzolasyon kazıma genişliği bunu **geçmemeli**,
   yoksa izler kesilir. Pratikte 0.15–0.30 mm hedeflenir.
3. **İzolasyon derinliği çok sığ (≈0.1 mm).** Bakır kalınlığı ≈0.035 mm. Plakanın düz olmayışı
   (0.05 mm bile) bazı yerlerde bakırın kesilmemesine yol açar. Çözümlerden biri şart:
   - Z-probe + yükseklik haritası (heightmap / auto-leveling), **veya**
   - feda plakasını (MDF) tezgahta önceden düzlemek (facing) + plakayı tüm yüzeyden
     çift taraflı bantla yapıştırmak, **veya**
   - vakumlu tabla.
4. Delik ve kesim plakanın altına geçer: altta **feda plakası** (MDF vb.) olmalı.

---

## 2. Klasör içeriği

```
cnc_paket/
├── README.md                     ← bu dosya
├── hub/
│   ├── 1_kaynak_gerber/          ← ASIL KAYNAK (CAM programına bunlar yüklenir)
│   ├── 2_gorseller/              ← kontrol resimleri
│   └── 3_hazir_gcode_GRBL/       ← GRBL/Candle için hazırlanmış örnek G-code
├── sensor/                       ← aynı yapı
└── yeniden_uretim/               ← KiCad kart dosyası + G-code üreten Python betiği
```

### 2.1 `1_kaynak_gerber/` (kaynak dosyalar)

| Dosya | Biçim | İçerik | İşlem |
|---|---|---|---|
| `klima-<kart>-B_Cu.gbr` | Gerber RS-274X | Bakır izler ve pedler (alt yüz) | İzolasyon kazıma |
| `klima-<kart>-PTH.drl` | Excellon | Eleman delikleri | Delme |
| `klima-<kart>-NPTH.drl` | Excellon | Ø2.2 mm vida deliği (yalnız sensör) | Delme veya frezeyle oyma |
| `klima-<kart>-Edge_Cuts.gbr` | Gerber RS-274X | Kart dış hattı (+ sensörde iç yarık) | Kesim |

Gerber ve Excellon dosyaları aynı koordinat sistemindedir (KiCad orijini). CAM programında
hepsi **birlikte** açılıp **birlikte** aynalanmalıdır, yoksa delikler pedlere oturmaz.

Uygun CAM programları: FlatCAM, pcb2gcode, Carbide Copper, CopperCAM, Fusion 360 (Electronics),
ya da tezgahın kendi CAM yazılımı (DXF'e dönüştürerek).

### 2.2 `2_gorseller/`

| Dosya | Ne gösterir |
|---|---|
| `bakir_alt_aynali.png` / `.svg` | Kazınacak bakırın **aynalanmış** hali (tezgahta görülecek hali). SVG ölçeklidir, DXF'e dönüştürülebilir |
| `<kart>_onizleme_cnc.png` | Hazır G-code'un önizlemesi: mavi = izolasyon yolu, siyah = delikler, kırmızı = kesim hattı |
| `yerlesim_ust.png` | Elemanların üstten yerleşimi (yalnız bilgi amaçlı, CNC için gerekmez) |

### 2.3 `3_hazir_gcode_GRBL/` (hazır örnek G-code)

Bu dosyalar **GRBL kontrolcülü hobi CNC + Candle** programı için yazılmıştır. Bizim tezgah
farklıysa doğrudan çalıştırılmamalı, **örnek/referans** olarak kullanılmalıdır.

| Dosya | Uç | Ne yapar |
|---|---|---|
| `<kart>_01_izolasyon.nc` | V-uç (30°, 0.2 mm uç) | İzlerin etrafını kazır, 3 geçiş, 0.1 mm derinlik |
| `<kart>_02_delik_0.8mm.nc` | Ø0.8 matkap | 0.8 mm delikler (yalnız hub) |
| `<kart>_02_delik_1.0mm.nc` | Ø1.0 matkap | 1.0 mm delikler |
| `<kart>_02_delik_2.2mm.nc` | Ø2.2 matkap | Vida deliği (yalnız sensör) |
| `<kart>_02_delik_HEPSI_1.0mm.nc` | Ø1.0 matkap | **Tek uç seçeneği:** bütün delikler 1.0 mm (0.8'lik bacaklar 1.0'da da olur) |
| `<kart>_03_kesim_freze_1.0mm.nc` | Ø1.0 parmak freze | Dış hat (4 sekmeli); sensörde yarık + vida deliği de |
| `<kart>_03_kesim_freze_2.0mm.nc` | Ø2.0 parmak freze | Aynısı |
| `<kart>_03_kesim_freze_3.175mm.nc` | Ø3.175 parmak freze | Yalnız dış hat. Sensörün yarığı ve vida deliği bu frezeye dar, **atlanır** |
| `<kart>_03_kesim_ALTERNATIF_vuc_cizgi.nc` | V-uç | Freze yoksa: dış hattı 0.3 mm çizer, kart elle kesilir |

**Bu G-code'ların varsayımları** (uyarlarken dikkat):

| Konu | Hazır dosyadaki değer |
|---|---|
| Birim / koordinat | `G21` mm, `G90` mutlak, `G94` mm/dk, `G17` XY düzlemi |
| Sıfır noktası | X0 Y0 = plakanın **sol-ön köşesi**, kart 4 mm içeriden başlar. Z0 = **bakır yüzey** |
| Eksen yönleri | X sağa, Y ileri (operatörden uzağa), Z yukarı |
| Yükseklikler | Başlangıç/bitiş Z8, hareketler arası Z1.5 |
| İş mili | `M3 S12000`, ardından `G4 P3` (GRBL'de **3 saniye**; Fanuc vb.'de P milisaniyedir!) |
| Bitiş | `M5`, X0 Y0'a dönüş, `M2` |
| Uç değiştirme | `M6` **yok**, takım boyu telafisi **yok**. Her uç ayrı dosya; uç değişince yalnız Z elle yeniden sıfırlanır |
| Yorum satırları | Parantez içinde `( ... )` |
| Yay komutları | Yok, yalnız `G0`/`G1` doğru parçaları |
| İzolasyon | 150 mm/dk ilerleme, 50 mm/dk dalış, 0.1 mm derinlik, 3 geçiş %40 bindirme |
| Delme | 1.9 mm derinlik, 60 mm/dk, tek dalış (gagalama yok) |
| Kesim | 1.8 mm toplam, 0.4 mm paso, 120 mm/dk ilerleme, 40 mm/dk dalış, 4 sekme (2.5 mm geniş, 0.5 mm yüksek) |

### 2.4 `yeniden_uretim/`

`pcb/tools/cnc.py`, KiCad kart dosyasından (`.kicad_pcb`) doğrudan G-code üreten betiktir.
Uç, hız, derinlik vb. değerler komut satırından değiştirilebilir. Her çalıştırmada frezeleme
sonrası kalacak bakırı hesaplayıp **kısa devre ve kopukluk** denetimi yapar (mevcut dosyalarda
ikisi de 0).

Gereksinim: KiCad (Python modülü `pcbnew`), `shapely`, `matplotlib`. KiCad'in kendi Python'uyla
çalıştırılır:

```bash
cd yeniden_uretim
python3 pcb/tools/cnc.py all --angle 20 --tip 0.1 --depth 0.08 --rpm 16000 --passes 4
python3 pcb/tools/cnc.py hub --feed 200            # yalnız hub
python3 pcb/tools/cnc.py --help                    # tüm seçenekler
```

Çıktılar `yeniden_uretim/pcb/cnc/<kart>/` altına yazılır. Seçenekler: `--tip`, `--angle`,
`--depth`, `--passes`, `--overlap`, `--feed`, `--plunge`, `--rpm`, `--z-safe`, `--z-clear`,
`--drill-depth`, `--drill-feed`, `--cut-depth`, `--cut-step`, `--cut-feed`, `--cut-plunge`,
`--tabs`, `--tab-width`, `--tab-height`.

Betik GRBL lehçesinde yazar. Başka bir kontrolcü için çıktının başı/sonu (iş mili, bekleme,
yorum biçimi) uyarlanmalıdır.

---

## 3. V-uç kazıma genişliği

**Genişlik = uç çapı + 2 × derinlik × tan(açı / 2)**

| V-uç | 0.1 mm derinlikte genişlik | Uygun mu (≤ 0.45 mm) |
|---|---|---|
| 20°, 0.1 mm uç | 0.135 mm | ✅ (4 geçiş önerilir) |
| 30°, 0.1 mm uç | 0.154 mm | ✅ |
| 30°, 0.2 mm uç | 0.254 mm | ✅ hazır dosyaların değeri |
| 45°, 0.2 mm uç | 0.283 mm | ✅ |
| 60°, 0.2 mm uç | 0.315 mm | ✅ |
| 90°, 0.2 mm uç | 0.400 mm | ⚠️ sınırda, derinliği 0.08 mm yapın |

CAM programında izolasyon için "takım çapı" olarak **bu genişlik** girilmelidir, uç çapı değil.

Uç açısı bilinmiyorsa: hurda plakaya 0.1 mm derinlikte düz bir çizgi kazıyın, genişliğini
kumpas veya büyüteçle ölçün.

---

## 4. İşlem sırası

| Sıra | İşlem | Uç | Not |
|---|---|---|---|
| 0 | Hazırlık | — | Feda plakası, bakır yüzü yukarı, çift taraflı bant (tüm yüzeye). Bakır 800–1000 zımpara + alkolle temizlenir |
| 1 | Sıfırlama | — | X0 Y0 plakanın sol-ön köşesi, Z0 bakır yüzey |
| 2 | İzolasyon | V-uç | Yükseklik haritası / düzlemiş tabla ile |
| 3 | Delme | Ø0.8, Ø1.0, (Ø2.2) | Uç değişince **yalnız Z** yeniden sıfırlanır, X/Y'ye dokunulmaz |
| 4 | Kesim | Parmak freze | Kart sekmelerle plakada kalır |
| 5 | Bitirme | — | Sekmeler kesilir, çapak alınır, multimetreyle kısa devre kontrolü |

Kıskaç kullanılırsa 8 mm'den alçak olmalı (ya da Z güvenli yüksekliği kıskaca göre artırılmalı).

---

## 5. Kontrol listesi (G-code çalıştırılmadan önce)

- [ ] G-code önizlemesi `2_gorseller/bakir_alt_aynali.png` ile **aynı yönde** (aynalanmış).
- [ ] Delikler pedlerin tam ortasında.
- [ ] Kesim hattı kartın dışında, takım yarıçapı kadar dışarı kaydırılmış (dış kontur).
- [ ] Sensörde iç yarık ve vida deliği **içeriden** kesiliyor (iç kontur).
- [ ] İzolasyon genişliği ≤ 0.45 mm, en dar yerlerde izler birbirinden ayrılıyor.
- [ ] İş mili devri tezgahın sınırları içinde, bekleme komutu tezgahın lehçesinde doğru.
- [ ] Toplam kart alanı tezgahın çalışma alanı ve plakanın içinde.
- [ ] Önce **havada deneme**: Z0'ı plakanın 5 mm yukarısına alıp program bir kez boşta koşturulur.
- [ ] İlk gerçek deneme hurda plakada.

---

## 6. Tezgah bilgileri (mekanikçi dolduracak / yapay zekaya söylenecek)

G-code bunlara göre üretilmeli:

| Soru | Cevap |
|---|---|
| Tezgah marka / model | |
| Kontrolcü (GRBL, Mach3, Mach4, LinuxCNC, Fanuc, Siemens, Syntec, …) | |
| Kullanılan gönderici/kontrol yazılımı (Candle, UGS, Mach3, …) | |
| Çalışma alanı X × Y × Z (mm) | |
| İş mili devir aralığı (min–max dev/dk) | |
| Z-probe var mı? Yükseklik haritası (auto-level) yapılabiliyor mu? | |
| Otomatik takım değiştirici / takım boyu ölçer var mı? | |
| Plakayı tutma: bant / kıskaç / vakum | |
| V-uç: açı ve uç çapı | |
| Elde olan matkaplar (Ø) | |
| Elde olan parmak frezeler (Ø) | |
| Plaka malzemesi ve kalınlığı (FR4/FR1, 1.6 mm?) | |
| Sıfır noktası tercihi (sol-ön köşe dışında bir yer mi?) | |
| Kaç adet kart (hub / sensör) | |

---

## 7. Yapay zekaya verilecek örnek istek

> Ekteki `cnc_paket` klasöründeki README.md'yi oku. Tek yüzlü, bakırı alt yüzde iki PCB'yi
> (hub ve sensör) bizim CNC'de üreteceğiz. Tezgah bilgileri şunlar: *[6. bölümdeki tabloyu
> buraya yapıştır]*. `1_kaynak_gerber/` dosyalarını (veya `3_hazir_gcode_GRBL/` dosyalarını
> uyarlayarak) bizim kontrolcümüze uygun izolasyon, delme ve kesim G-code'larını ayrı ayrı
> dosyalar olarak hazırla. Aynalamayı unutma, izolasyon genişliği 0.45 mm'yi geçmesin.
> Her dosyanın başına hangi ucu takacağımı ve sıfır noktasını yorum olarak yaz.

---

## 8. Güvenlik

- FR4 (cam elyaf) tozu solunmamalı: toz emiş, maske ve gözlük kullanın. FR1 daha az zararlıdır.
- Küçük karbür uçlar (0.8–1.0 mm) çok kırılgandır; dalış ve ilerleme hızlarını artırmadan önce
  hurda plakada deneyin.
