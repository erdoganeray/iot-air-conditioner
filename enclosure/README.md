# Kutular (3D baskı)

Hub ve sensör kutuları üçer parçadır: **gövde**, **kapak** ve pil ile kartı ayıran **ara plaka**.
Kaynak dosya `build_enclosures.py`'dir (CadQuery). Tüm ölçüler kodun başındadır, PCB ile uyumludur.

| Kutu | Dış ölçü | Parçalar |
|---|---|---|
| Hub | 61.4 × 52.0 × 24.4 mm | `hub/hub_alt_govde`, `hub/hub_kapak`, `hub/hub_ara_plaka` |
| Sensör (×2) | 33.8 × 48.6 × 24.0 mm | `sensor/sensor_arka_govde`, `sensor/sensor_on_kapak`, `sensor/sensor_ara_plaka` |

Her parçanın `.stl` (baskı) ve `.step` (düzenleme) dosyası vardır. `*_montaj.step` dosyası kutuyu,
kartı ve temsili elemanları (XIAO, pil, LED'ler, USB fişi…) birlikte gösterir.

**3D görüntüleyici:** `onizleme/hub_3d.html` ve `onizleme/sensor_3d.html` dosyalarını tarayıcıda
açın. Parçayı fareyle döndürebilir, patlatılmış görünüme geçebilir, kesit alabilir ve parçaları tek
tek gizleyebilirsiniz (internet gerekir: three.js CDN'den yüklenir).

![Hub](onizleme/hub_patlatilmis.png)
![Sensör](onizleme/sensor_patlatilmis.png)

## Baskı ayarları (PLA, 0.4 mm nozül)

| Ayar | Değer |
|---|---|
| Katman | 0.2 mm (ilk katman 0.2–0.3) |
| Duvar | 3 çevre (1.2 mm) |
| Doluluk | %20, ızgara veya gyroid |
| Destek | **Gerekmez** |
| Yön | Gövde: tabanı tablada. Kapak: **üst yüzü tablada** (ters). Ara plaka: düz |

Kapakta tırnak pencereleri ve havalandırma yarıkları köprüleme ile basılır; köprü ayarları
varsayılan olabilir.

## Gereken küçük parçalar

| Parça | Adet | Not |
|---|---|---|
| M2 × 10 vida (havşa veya silindir başlı) | 1 kutu başına | PLA'daki 1.7 mm deliğe kendi yolunu açar. Sacvidası (M2.2 × 9.5) da olur |
| Kauçuk ayak, 8 mm yapışkanlı | 4 (hub) | İsteğe bağlı |
| Çift taraflı köpük bant | 2 (sensör) | Duvar montajı. 3M VHB önerilir |
| Sıcak silikon | az | SHT31 ve TTP223 modüllerini sabitlemek için |

## Kutunun özellikleri

**Hub**

- Ön yüz klimaya bakar. İki IR LED'in kubbesi ön duvardaki deliklerden 1 mm dışarı çıkar. LED'ler
  alt gövde ile kapağın birleşim çizgisindedir: yarım delikler kapak kapanınca tam daire olur.
- IR alıcısı (VS1838B) için 10 × 8 mm pencere.
- Sağ önde **SHT31 odacığı**: ön duvarda, tabanda ve kapakta havalandırma yarıkları vardır. Kutunun
  geri kalanından duvarla ayrılır; XIAO'nun ısısı ölçümü daha az etkiler (kalan farkı uygulamadaki
  sıcaklık düzeltmesiyle giderin).
- Kapakta iki durum LED'i deliği (kırmızı, yeşil) ve **dokunmatik çukur**. TTP223 modülü kapağın
  altındaki çerçeveye yapıştırılır; çukurdaki 1.0 mm plastikten dokunuşu algılar.
- Arkada USB-C (12 × 7 mm, kalın kablo başlıkları da girer) ve havalandırma yarıkları.
- Pil kartın altındadır. Ara plaka pili lehim uçlarından korur.
- Kapak 4 tırnakla oturur, alttan 1 adet M2 vida ile sabitlenir.

**Sensör**

- Arka gövde düzdür, duvara bantla yapışır. USB-C alttadır.
- Üstte **SHT31 odacığı**: kapaktaki perde, karttaki yarıktan geçerek odacığı XIAO ve pil
  bölmesinden ayırır. Ön yüzde, üst kenarda ve yanlarda havalandırma yarıkları vardır.
- M2 vida arkadan girer: arka gövde, kart ve ön kapak birlikte sıkışır. Vida bandın altında kalır.
- Ön kapak 4 tırnakla oturur.

## Açma

Tırnak pencerelerinden (yanlardaki küçük dikdörtgenler) ince bir tornavida veya ataşla tırnağı
içeri bastırıp kapağı kaldırın. Önce vidayı sökün.

## Değiştirmek isterseniz

```bash
pip install cadquery trimesh matplotlib
python3 enclosure/build_enclosures.py          # hub ve sensör
python3 enclosure/build_enclosures.py sensor   # yalnız sensör
```

Betik her çalıştığında kutu ile kart, pil ve diğer elemanlar arasında **çakışma denetimi** yapar.
Çakışma varsa hata verir. Sık değişebilecek ayarlar:

- `FIT` (0.15): kapak ile tırnaklar arası boşluk. Kapak zor oturuyorsa 0.2–0.25 yapın.
- `BUMP` (0.5): tırnak çıkıntısı. Kapak gevşekse 0.6 yapın.
- `USB_W`, `USB_H`: USB-C fiş açıklığı.
- `build_hub()` / `build_sensor()` içindeki `CAP_IN`: kapak iç yüksekliği. Pil soketi (JST-PH) ile
  fişin toplam yüksekliği kapağa değiyorsa artırın (kapakta soket üstünde ayrıca cep vardır).

PNG önizlemeler için Playwright ve Chromium gerekir (`pip install playwright && playwright install
chromium`). Yoksa betik yalnızca uyarı verir, STL/STEP dosyaları yine üretilir.

Montaj adımları: [docs/MONTAJ.md](../docs/MONTAJ.md)
