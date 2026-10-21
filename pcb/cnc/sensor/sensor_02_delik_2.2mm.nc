(KlimaHub sensor karti - delik 2.2 mm [1 adet])
(Takim: 2.2 mm matkap ucu)
(Birim mm, mutlak koordinat. X/Y sifiri: plakanin sol-on kosesi, kart 4 mm iceriden baslar)
(Z sifiri: bakir yuzey - Candle ile Z-probe yapin; izolasyonda heightmap onerilir)
(Uretici: pcb/tools/cnc.py - github.com/erdoganeray/iot-air-conditioner)
G21
G90
G94
G17
G0 Z8.000
M3 S12000
G4 P3
(Delme derinligi 1.9 mm: 1.6 mm plaka + pay, altta feda plakasi olsun)
G0 X25.4 Y45
G0 Z1.5
G1 Z-1.9 F60
G0 Z8.000
M5
G0 X0 Y0
M2
