# Hazır yazılım dosyaları

PlatformIO kurmadan yüklemek için. Yükleme adımları: [docs/KURULUM.md](../../docs/KURULUM.md) §1.

| Dosya | Kart | Nasıl yüklenir |
|---|---|---|
| `klimahub-hub-full.bin` | Hub (XIAO ESP32-S3, 8 MB) | USB, tarayıcıdan (esptool-js), adres **0x0** |
| `klimahub-sensor-full.bin` | Sensör (XIAO ESP32-C3, 4 MB) | USB, tarayıcıdan (esptool-js), adres **0x0** |
| `klimahub-hub-ota.bin` | Hub | Kablosuz güncelleme: uygulama → Ayarlar → Yazılım güncelle |

Hub yazılımı 1.0.0, sensör yazılımı 1.0. Web uygulaması hub yazılımının içine gömülüdür.

SHA-256:

```
e428606f05e9aba9f9bf97bcd2d2c01f9856a02872e02f359ea677bb4388c598  klimahub-hub-full.bin
d5c5152d93004221d4685be37c716da49c314d781a56bde80a7cf3f27b00000a  klimahub-hub-ota.bin
977ffe7687568c041dcb16cd82026b305809659e11b59b1220673a4e05a9fb17  klimahub-sensor-full.bin
```

Kodda değişiklik yaptıysanız bu dosyalar eskir. En günceli her push'ta GitHub Actions'ta
(**Actions → Firmware → Artifacts**) üretilir veya PlatformIO ile kendiniz derlersiniz
(`.pio/build/<ortam>/firmware-full.bin`).
