# IoT Klima Kumandası + Sıcaklık/Nem Sensörleri

ESP32 tabanlı, evdeki WiFi kopmalarına dayanıklı bir klima kontrol sistemi:

- **Hub** (Seeed XIAO ESP32-S3): IR alıcı + IR verici, kırmızı/yeşil LED, dokunmatik buton,
  SHT31 sıcaklık/nem sensörü, LiPo pil (adaptörle ya da pille çalışır).
- **2 adet sensör** (Seeed XIAO ESP32-C3): SHT31 sıcaklık/nem, LiPo pil, USB-C ile şarj,
  ESP-NOW ile hub'a bağlanır (modem kopsa bile çalışır).

> Bu depo geliştirme aşamasındadır. Ayrıntılı belgeler `docs/` klasöründedir.
