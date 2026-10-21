# CNC ile Kart Üretimi (Candle / GRBL)

Her iki kart da **tek yüzlüdür**: bakır **alt** yüzdedir. Plakayı **bakır yüzü yukarı** bakacak
şekilde tezgaha bağlarsınız; tüm işlemler (izolasyon, delme, kesim) tek bağlamada yapılır.
Bu yüzden G-code çizimi **aynalanmıştır** (kartın alttan görünüşü). KiCad'deki üstten
görünüşün tersi olması normaldir.

## 1. Dosyalar

`pcb/cnc/hub/` ve `pcb/cnc/sensor/` klasörlerinde:

| Dosya | Uç | Ne yapar |
|---|---|---|
| `<kart>_01_izolasyon.nc` | V-uç | İzlerin etrafını kazır (3 geçiş) |
| `<kart>_02_delik_0.8mm.nc` | 0.8 mm matkap | LED, transistör, kondansatör delikleri (yalnız hub) |
| `<kart>_02_delik_1.0mm.nc` | 1.0 mm matkap | Pin header, XIAO, JST delikleri |
| `<kart>_02_delik_2.2mm.nc` | 2.2 mm matkap | Sensör vida deliği (yalnız sensör) |
| `<kart>_02_delik_HEPSI_1.0mm.nc` | 1.0 mm matkap | **Tek uç seçeneği:** bütün delikler 1.0 mm (0.8 mm'lik bacaklar 1.0 mm delikte de rahat lehimlenir) |
| `<kart>_03_kesim_freze_1.0mm.nc` | 1.0 mm parmak freze | Kart dış hattı (4 tutma sekmeli) + sensör yarığı + vida deliği |
| `<kart>_03_kesim_freze_2.0mm.nc` | 2.0 mm parmak freze | Aynısı |
| `<kart>_03_kesim_freze_3.175mm.nc` | 3.175 mm (1/8") freze | Dış hat. Sensör yarığı ve vida deliği bu frezeye dar: dosya bunları atlar (dosyadaki notta yazar) |
| `<kart>_03_kesim_ALTERNATIF_vuc_cizgi.nc` | V-uç | Freze yoksa: dış hattı 0.3 mm kazır, kartı sonra elle kesersiniz |
| `<kart>_onizleme_cnc.png` | — | Görsel: mavi izolasyon yolu, siyah delikler, kırmızı kesim |
| `gerber/` | — | FlatCAM vb. için Gerber ve Excellon dosyaları |

![Hub CNC önizleme](../pcb/cnc/hub/hub_onizleme_cnc.png)

**Ölçüler:**

| Kart | Kart | Plakada kapladığı alan |
|---|---|---|
| Hub | 54 × 42 mm | 62 × 50 mm |
| Sensör | 25 × 44 mm | 33 × 52 mm |

Üç kart da 10 × 15 cm'lik bir plakaya sığar.

## 2. Varsayılan değerler (uçlarınızı teyit edince güncelleyin)

| Ayar | Değer | Komut satırı seçeneği |
|---|---|---|
| V-uç uç çapı | 0.2 mm | `--tip` |
| V-uç açısı | 30° | `--angle` |
| İzolasyon derinliği | 0.10 mm | `--depth` |
| İzolasyon geçiş sayısı / bindirme | 3 / %40 | `--passes`, `--overlap` |
| İzolasyon ilerleme / dalış | 150 / 50 mm/dk | `--feed`, `--plunge` |
| İş mili devri | 12000 dev/dk | `--rpm` |
| Güvenli yükseklik / başlangıç yüksekliği | 1.5 / 8 mm | `--z-safe`, `--z-clear` |
| Delme derinliği / ilerleme | 1.9 mm / 60 mm/dk | `--drill-depth`, `--drill-feed` |
| Kesim derinliği / paso | 1.8 / 0.4 mm | `--cut-depth`, `--cut-step` |
| Kesim ilerleme / dalış | 120 / 40 mm/dk | `--cut-feed`, `--cut-plunge` |
| Tutma sekmesi sayısı / genişlik / yükseklik | 4 / 2.5 / 0.5 mm | `--tabs`, `--tab-width`, `--tab-height` |

**V-uç kesim genişliği** = uç çapı + 2 × derinlik × tan(açı / 2). Varsayılanlarla
0.2 + 2 × 0.1 × tan(15°) = **0.254 mm**. Kartta en dar boşluk 0.45 mm olduğundan genişlik
0.45 mm'yi geçmemelidir:

| V-uç | 0.1 mm derinlikte genişlik | Uygun mu |
|---|---|---|
| 20°, 0.1 mm uç | 0.135 mm | ✅ (daha ince, 4 geçiş önerilir: `--passes 4`) |
| 30°, 0.1 mm uç | 0.154 mm | ✅ |
| 30°, 0.2 mm uç | 0.254 mm | ✅ varsayılan |
| 45°, 0.2 mm uç | 0.283 mm | ✅ |
| 60°, 0.2 mm uç | 0.315 mm | ✅ |
| 90°, 0.2 mm uç | 0.400 mm | ✅ (sınırda, derinliği 0.08 yapın) |

**Uç açısını bilmiyorsanız:** Hurda bir plakaya 0.1 mm derinlikte düz bir çizgi kazıyın.
Çizginin genişliğini kumpas veya büyüteçle ölçün. Uç çapı biliniyorsa açı tablodan
kestirilir. Yaklaşık değer bile yeterlidir; asıl önemli olan izolasyonun bakırı tam kesmesi.

### Dosyaları yeniden üretmek

```bash
# Örnek: 20° uç, 0.1 mm uç çapı, 0.08 mm derinlik, 16000 devir
python3 pcb/tools/cnc.py all --angle 20 --tip 0.1 --depth 0.08 --rpm 16000 --passes 4
python3 pcb/tools/cnc.py hub --feed 200          # yalnız hub
```

Betik KiCad'in Python modülünü (`pcbnew`) ve `shapely` kütüphanesini kullanır. Bilgisayarınızda
KiCad kuruluysa KiCad'in Python'uyla çalışır. İsterseniz bana yeni değerleri söyleyin, ben
üretip depoya eklerim.

Her çalıştırmada betik, frezeleme sonrası kalacak bakırı hesaplayıp **kısa devre ve kopukluk**
denetimi yapar. Şu anki dosyalarda ikisi de **0**.

## 3. Candle ile adım adım

### Hazırlık

1. Tezgah tablasına MDF gibi bir **feda plakası** koyun (matkap ve kesim ucu plakanın altına
   geçer, tablaya zarar vermesin).
2. Bakır plakayı **bakır yüzü yukarı** gelecek şekilde çift taraflı bantla yapıştırın. Bant
   plakanın tüm yüzeyine yayılsın, ortası kalkmasın. Kıskaç kullanıyorsanız 8 mm'den alçak
   olsun (ilk hareket 8 mm yükseklikte yapılır, sonrakiler 1.5 mm'de).
3. Bakırı ince zımparayla (800–1000) veya bulaşık süngerinin yeşil yüzüyle parlatın, alkolle
   silin. Z-probe ölçümleri temiz bakırda daha güvenilirdir.

### Sıfırlama

- **X0 Y0:** Plakanın **sol-ön köşesi** (operatöre yakın, solda). Kart bu köşeden 4 mm içeride
  başlar. Plakanın başka bir yerine kesmek istiyorsanız (aynı plakaya üç kart), X0 Y0'ı o
  noktaya alın: kartın kapladığı alan yukarıdaki tabloda.
- **Z0:** Bakır yüzey. Candle'da Z-probe ile alın (probe klipsini uca, plakayı plakaya
  bağlayın).

### Yükseklik haritası (önerilir)

Bakır plaka tamamen düz değildir. 0.1 mm'lik izolasyonda 0.05 mm'lik eğrilik bile bazı
yerlerde bakırı kesmemeye yol açar. Candle'ın **Heightmap** özelliği bunu düzeltir:

1. İzolasyon dosyasını açın (`File → Open`).
2. **Heightmap** bölümünde **Create** deyin. Alanı dosyanın sınırlarına göre girin
   (hub: X 3–59, Y 3–47; sensör: X 3–30, Y 3–49), ızgara 5 × 5 yeterlidir.
3. **Probe** ile ölçümü başlatın. Bitince haritayı kaydedin.
4. Haritayı yükleyip **Use heightmap** seçeneğini işaretleyin. Candle G-code'u haritaya göre
   düzeltir.

(Menü adları Candle sürümüne göre biraz farklı olabilir.)

### İşlem sırası

| Sıra | Dosya | Uç | Not |
|---|---|---|---|
| 1 | `_01_izolasyon.nc` | V-uç | Yükseklik haritasıyla. ≈15 dk (hub), ≈9 dk (sensör) |
| 2 | `_02_delik_0.8mm.nc` | 0.8 mm | Uç değişince **yalnız Z'yi** yeniden sıfırlayın, X/Y'ye dokunmayın |
| 3 | `_02_delik_1.0mm.nc` | 1.0 mm | Ya da 2+3 yerine `_02_delik_HEPSI_1.0mm.nc` |
| 4 | `_02_delik_2.2mm.nc` | 2.2 mm | Yalnız sensör. 2.2 mm ucunuz yoksa atlayın: 1.0 ve 2.0 mm kesim dosyaları bu deliği frezeyle oyar |
| 5 | `_03_kesim_freze_X.Xmm.nc` | Parmak freze | Elinizdeki frezenin dosyası. Kart 4 sekmeyle plakaya tutunur |

Her dosya iş milini kendisi çalıştırır (`M3 S12000`), 3 sn bekler, bitince durdurur (`M5`) ve
X0 Y0'a döner. Uç değiştirme komutu (`M6`) kullanılmaz. Her uç için ayrı dosyayı açarsınız.

### Bitirme

1. Sekmeleri maket bıçağı veya yan keskiyle kesin, kenarları zımparalayın.
2. Bakırı ince zımpara veya bulaşık süngeriyle çapaklardan temizleyin.
3. **Multimetreyle kontrol:** Yan yana geçen izlerle GND dolgusu arasında kısa devre olmamalı.
   Özellikle XIAO pinleri arası ve J3 pinleri arası. Şüpheli yerde izolasyon çizgisini maket
   bıçağıyla bir kez daha çizin.
4. İsterseniz lehimlemeyi kolaylaştırmak için bakır yüzeye flux sürün (veya kalay kaplayın).

## 4. FlatCAM ile (alternatif)

Kendi ayarlarınızla FlatCAM'de çalışmak isterseniz `gerber/` klasöründeki dosyaları kullanın:

| Dosya | İçerik |
|---|---|
| `klima-<kart>-B_Cu.gbr` | Bakır (alt yüz) |
| `klima-<kart>-PTH.drl` | Bacaklı eleman delikleri (Excellon) |
| `klima-<kart>-NPTH.drl` | Kaplamasız delikler (sensör vida deliği) |
| `klima-<kart>-Edge_Cuts.gbr` | Kart dış hattı ve yarıklar |

FlatCAM'de alt yüz için **X ekseninde aynalama** (Double-Sided PCB aracı, kart sınırına göre)
yapmayı unutmayın. İzolasyon için uç çapı yerine yukarıdaki tabloda hesaplanan **kesim
genişliğini** girin.

## 5. Güvenlik

- FR4 (cam elyaf) tozu solunmamalıdır: frezelerken tozu elektrik süpürgesiyle çekin, maske
  ve gözlük takın. FR1 (pertinaks) daha az zararlıdır.
- Karbür uçlar kırılgandır. Dalış hızlarını artırmadan önce hurda plakada deneyin.
