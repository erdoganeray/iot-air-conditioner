"""Hub ve sensör kartlarının tanımı: kart ölçüsü, eleman yerleşimi ve bağlantı listesi.

Koordinatlar KiCad üst görünümündedir (mm): X sağa, Y aşağıya, (0,0) kartın sol-üst köşesi.
Tek yüzlü kart: bakır ALT yüzde (B.Cu). Bacaklı (THT) elemanlar üstten takılır, alttan
lehimlenir. SMD elemanlar (1206 direnç/kondansatör, SMA diyot) doğrudan alt yüze lehimlenir.

Hub: ön kenar (y=0) klimaya bakar; IR LED'ler, IR alıcı ve durum LED'leri buradadır.
     XIAO'nun USB-C'si arka kenara (y=max) bakar.
Sensör: duvarda dikey durur. Üst kenarda SHT31 (ısıl yarıkla ayrılmış), alt kenarda USB-C.
"""

FP = "/usr/share/kicad/footprints"
LOCAL = "klima"  # pcb/lib/klima.pretty

R1206 = ("Resistor_SMD", "R_1206_3216Metric_Pad1.30x1.75mm_HandSolder")
C1206 = ("Capacitor_SMD", "C_1206_3216Metric_Pad1.33x1.80mm_HandSolder")
SMA = ("Diode_SMD", "D_SMA_Handsoldering")
CP63 = ("Capacitor_THT", "CP_Radial_D6.3mm_P2.50mm")
TO92W = ("Package_TO_SOT_THT", "TO-92_Inline_Wide")
LED3 = ("LED_THT", "LED_D3.0mm_Horizontal_O3.81mm_Z2.0mm")
LED5 = ("LED_THT", "LED_D5.0mm_Horizontal_O3.81mm_Z3.0mm")
LED3V = ("LED_THT", "LED_D3.0mm")
JST2 = ("Connector_JST", "JST_PH_B2B-PH-K_1x02_P2.00mm_Vertical")
H2 = ("Connector_PinHeader_2.54mm", "PinHeader_1x02_P2.54mm_Vertical")
H3 = ("Connector_PinHeader_2.54mm", "PinHeader_1x03_P2.54mm_Vertical")
H4 = ("Connector_PinHeader_2.54mm", "PinHeader_1x04_P2.54mm_Vertical")
H7 = ("Connector_PinHeader_2.54mm", "PinHeader_1x07_P2.54mm_Vertical")
XIAO = (LOCAL, "XIAO_DIP14")


def comp(ref, fp, value, x, y, rot=0, side="top", desc=""):
    return dict(ref=ref, fp=fp, value=value, x=x, y=y, rot=rot, side=side, desc=desc)


# --------------------------------------------------------------------- HUB
# XIAO ESP32-S3 merkezi (29, 30.2), 180° döndürülmüş: USB arkaya (+Y), USB-C yüzü kart kenarında.
#  sol sütun x=21.38: D7(22.58) D8(25.12) D9(27.66) D10(30.2) 3V3(32.74) GND(35.28) 5V(37.82)
#  sağ sütun x=36.62: D6(22.58) D5(25.12) D4(27.66) D3(30.2)  D2(32.74)  D1(35.28) D0(37.82)
# Pin görevleri tek yüzlü yönlendirme için seçildi (firmware/hub/src/pins.h ile aynı):
#  sol: D7=IR_TX, D8=LED_R, D9=BAT_SENSE, D10=USB_SENSE
#  sağ: D6=LED_G, D5=IR_RX, D4=TOUCH, D3=SDA, D2=SCL
# IR LED'leri seri bağlı ve doğrudan pilden (VBAT) beslenir: VBAT -> R5 -> LED1 -> LED2 -> Q1.
# Kart kenarında sürekli GND halkası vardır (tek yüzde GND'nin her bölgeye ulaşması için).
HUB = dict(
    name="klima-hub",
    title="KlimaHub - Hub karti (XIAO ESP32-S3)",
    width=54.0,
    height=42.0,
    corner=2.0,
    slots=[],
    gnd_ring=1.0,
    components=[
        comp("U1", XIAO, "XIAO ESP32-S3", 29.0, 30.2, 180, desc="Seeed XIAO ESP32-S3"),
        # Ön sıra: IR LED gövdeleri ön kenardan dışarı, ön duvardaki deliklere (bacaklar 90° bükülür)
        comp("LED1", LED5, "IR 940nm 5mm", 9.5, 3.8, 180, desc="IR verici LED"),
        comp("LED2", LED5, "IR 940nm 5mm", 19.0, 3.8, 180, desc="IR verici LED"),
        comp("U2", H3, "VS1838B", 33.08, 3.8, 270, desc="IR alıcı (OUT, GND, VCC), bacaklar öne bükülür"),
        # Durum LED'leri dik durur, üst kapaktaki deliklerden görünür
        comp("LED3", LED3V, "Kirmizi 3mm", 11.2, 25.12, 0, desc="Durum LED'i (kırmızı)"),
        comp("LED4", LED3V, "Yesil 3mm", 41.5, 17.0, 0, desc="Durum LED'i (yeşil)"),
        comp("J3", H7, "MODUL", 50.0, 23.0, 0,
             desc="Modül konnektörü: TOUCH, SDA, SCL, GND, GND, 3V3, 3V3 (TTP223 + SHT31)"),
        comp("Q1", TO92W, "S8050", 17.0, 12.0, 0, desc="IR LED sürücü NPN (E-B-C)"),
        comp("C2", CP63, "220uF 10V", 6.5, 13.0, 0, desc="IR besleme tamponu"),
        comp("J1", JST2, "BAT", 4.5, 36.5, 0, desc="LiPo pil 503035 (JST-PH 2.0)"),
        comp("J2", H2, "XBAT", 10.5, 19.5, 90, desc="XIAO alt BAT+/BAT- pedlerine kablo"),
        # SMD (alt yüz)
        comp("C3", C1206, "100nF", 4.0, 19.0, 90, "bottom", "VBAT dekuplaj"),
        comp("R5", R1206, "10R", 6.5, 9.0, 0, "bottom", "IR LED akım (seri)"),
        comp("R7", R1206, "150R", 20.5, 17.0, 90, "bottom", "Q1 baz"),
        comp("R8", R1206, "10k", 15.5, 16.8, 90, "bottom", "Q1 baz pull-down"),
        comp("R9", R1206, "470R", 41.5, 21.0, 90, "bottom", "Yeşil LED"),
        comp("R10", R1206, "470R", 17.6, 25.12, 180, "bottom", "Kırmızı LED"),
        comp("C5", C1206, "100nF", 29.3, 8.2, 0, "bottom", "IR alıcı dekuplaj"),
        comp("R1", R1206, "220k", 9.0, 29.0, 0, "bottom", "Pil bölücü üst"),
        comp("R2", R1206, "220k", 13.5, 30.5, 90, "bottom", "Pil bölücü alt"),
        comp("C1", C1206, "100nF", 17.0, 30.5, 90, "bottom", "Pil ADC filtresi"),
        comp("R3", R1206, "100k", 17.0, 35.5, 90, "bottom", "USB bölücü üst"),
        comp("R4", R1206, "150k", 12.4, 37.8, 180, "bottom", "USB bölücü alt"),
    ],
    nets={
        "GND": ["U1.13", "J1.2", "J2.2", "J3.4", "J3.5", "Q1.1", "R2.2", "R4.2", "R8.2",
                "C1.2", "C2.2", "C3.2", "C5.2", "LED3.1", "LED4.1", "U2.2"],
        "+3V3": ["U1.12", "J3.6", "J3.7", "U2.3", "C5.1"],
        "VBUS": ["U1.14", "R3.1"],
        "VBAT": ["J1.1", "J2.1", "R1.1", "C2.1", "C3.1", "R5.1"],
        "BAT_SENSE": ["R1.2", "R2.1", "C1.1", "U1.10"],
        "USB_SENSE": ["R3.2", "R4.1", "U1.11"],
        "IR_TX": ["U1.8", "R7.1"],
        "Q1_B": ["R7.2", "R8.1", "Q1.2"],
        "LED1_A": ["R5.2", "LED1.2"],
        "IR_MID": ["LED1.1", "LED2.2"],
        "IR_K": ["LED2.1", "Q1.3"],
        "LEDR_A": ["U1.9", "R10.1"],
        "LEDR": ["R10.2", "LED3.2"],
        "LEDG_A": ["U1.7", "R9.1"],
        "LEDG": ["R9.2", "LED4.2"],
        "IR_RX": ["U1.6", "U2.1"],
        "TOUCH": ["U1.5", "J3.1"],
        "SDA": ["U1.4", "J3.2"],
        "SCL": ["U1.3", "J3.3"],
    },
    power_nets=["GND", "VBAT", "VBUS", "IR_K", "IR_MID", "LED1_A"],
)

# ------------------------------------------------------------------ SENSÖR
# XIAO ESP32-C3 merkezi (12, 33), 180°: USB aşağı (+Y, kartın alt kenarı)
#  sol sütun x=4.38 : 5V(40.62) GND(38.08) 3V3(35.54) D10(33.0) D9(30.46) D8(27.92) D7(25.38)
#  sağ sütun x=19.62: D0(40.62) D1(38.08) D2(35.54) D3(33.0) D4(30.46) D5(27.92) D6(25.38)
# Pin görevleri (firmware/sensor/src/main.cpp ile aynı), tek yüzde kesişmesiz olacak şekilde:
#  D7=SENS_PWR (GPIO20), D6=SDA (GPIO21), D5=SCL (GPIO7), D2=BAT_SENSE (GPIO4), D1=USB_SENSE (GPIO3)
SENSOR = dict(
    name="klima-sensor",
    title="KlimaHub - Sensor karti (XIAO ESP32-C3)",
    width=25.0,
    height=44.0,
    corner=2.0,
    # SHT31 bölgesini XIAO'dan ayıran ısıl yarık (x1, y1, x2, y2), iki yanda köprü kalır
    slots=[(8.0, 12.4, 16.0, 14.6)],
    gnd_ring=1.0,
    components=[
        comp("U1", XIAO, "XIAO ESP32-C3", 12.0, 33.0, 180, desc="Seeed XIAO ESP32-C3"),
        comp("J3", H4, "SHT31", 8.19, 5.0, 90, desc="SHT31 modülü: GND, VIN, SDA, SCL"),
        comp("J1", JST2, "BAT", 7.3, 18.2, 0, desc="LiPo pil 503035 (JST-PH 2.0)"),
        comp("J2", H2, "XBAT", 14.0, 19.0, 90, desc="XIAO alt BAT+/BAT- pedlerine kablo"),
        comp("R1", R1206, "470k", 10.0, 33.0, 0, "bottom", "Pil bölücü üst"),
        comp("R2", R1206, "470k", 12.0, 36.6, 90, "bottom", "Pil bölücü alt"),
        comp("C1", C1206, "100nF", 15.0, 36.6, 90, "bottom", "Pil ADC filtresi"),
        comp("R3", R1206, "100k", 9.0, 40.6, 0, "bottom", "USB bölücü üst"),
        comp("R4", R1206, "150k", 14.5, 40.6, 0, "bottom", "USB bölücü alt"),
    ],
    nets={
        "GND": ["U1.13", "J1.2", "J2.2", "J3.1", "R2.2", "R4.2", "C1.2"],
        "VBAT": ["J1.1", "J2.1", "R1.1"],
        "BAT_SENSE": ["R1.2", "R2.1", "C1.1", "U1.3"],
        "VBUS": ["U1.14", "R3.1"],
        "USB_SENSE": ["R3.2", "R4.1", "U1.2"],
        "SENS_PWR": ["U1.8", "J3.2"],
        "SDA": ["U1.7", "J3.3"],
        "SCL": ["U1.6", "J3.4"],
    },
    power_nets=["GND", "VBAT"],
)

BOARDS = {"hub": HUB, "sensor": SENSOR}
