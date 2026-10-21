#!/usr/bin/env python3
"""KlimaHub kutuları: hub ve sensör için alt gövde, kapak ve ara plaka (CadQuery).

Çalıştırma (CadQuery gerekir: pip install cadquery trimesh matplotlib):
    python3 enclosure/build_enclosures.py            # hepsi
    python3 enclosure/build_enclosures.py hub        # yalnız hub

Çıktılar:
    enclosure/<kutu>/*.stl     3D yazıcı için (PLA, 0.4 mm nozül)
    enclosure/<kutu>/*.step    düzenlemek için (FreeCAD, Fusion, KiCad 3D)
    enclosure/<kutu>/*_montaj.step   kutu + kart + temsili elemanlar
    enclosure/onizleme/*.png   görseller
Betik ayrıca kutu ile temsili elemanlar (kart, XIAO, pil, LED, USB fişi...) arasında
çakışma denetimi yapar; çakışma varsa hata koduyla biter.

Koordinatlar KiCad kart koordinatlarıdır (mm): x sağa, y aşağıya (kartın ön/üst kenarı y=0).
z yukarı, kutunun dış tabanı z=0. CadQuery'ye aktarılırken Y = -y alınır.
Ölçüler pcb/tools/boards.py ile uyumludur.
"""
import os
import sys

import cadquery as cq

HERE = os.path.dirname(os.path.abspath(__file__))
V = cq.Vector

# ------------------------------------------------------------------ genel ayarlar
FIT = 0.15        # kapak iç yüzeyi ile alt gövde tırnakları arası boşluk
BUMP = 0.5        # tırnak çıkıntısı
FINGER_L = 6.5    # tırnak boyu (bölme düzleminin üstünde)
FINGER_T = 1.0    # tırnak kalınlığı
FINGER_W = 6.0    # tırnak genişliği
USB_W, USB_H = 12.0, 7.0   # USB-C fiş açıklığı (kalıplı fiş başlığı için)


# ------------------------------------------------------------------ yardımcılar
def box(x0, x1, y0, y1, z0, z1):
    X0, X1 = sorted((x0, x1))
    Y0, Y1 = sorted((-y0, -y1))
    Z0, Z1 = sorted((z0, z1))
    return cq.Solid.makeBox(X1 - X0, Y1 - Y0, Z1 - Z0, V(X0, Y0, Z0))


def rbox(x0, x1, y0, y1, z0, z1, r):
    X0, X1 = sorted((x0, x1))
    Y0, Y1 = sorted((-y0, -y1))
    Z0, Z1 = sorted((z0, z1))
    wp = cq.Workplane("XY").box(X1 - X0, Y1 - Y0, Z1 - Z0, centered=False).translate((X0, Y0, Z0))
    if r > 0:
        wp = wp.edges("|Z").fillet(r)
    return wp.val()


def cylz(x, y, d, z0, z1):
    return cq.Solid.makeCylinder(d / 2.0, z1 - z0, V(x, -y, z0), V(0, 0, 1))


def cyly(x, z, d, y0, y1):
    """Ekseni kart y yönünde olan silindir."""
    lo, hi = sorted((y0, y1))
    return cq.Solid.makeCylinder(d / 2.0, hi - lo, V(x, -hi, z), V(0, 1, 0))


def cone_z(x, y, d0, d1, z0, z1):
    return cq.Solid.makeCone(d0 / 2.0, d1 / 2.0, z1 - z0, V(x, -y, z0), V(0, 0, 1))


def prism_xz(pts, y0, y1):
    """(x, z) çokgenini kart y aralığı boyunca uzatır."""
    lo, hi = sorted((y0, y1))
    plane = cq.Plane(origin=(0, -hi, 0), xDir=(1, 0, 0), normal=(0, 1, 0))  # yerel v = -Z
    return cq.Workplane(plane).polyline([(x, -z) for x, z in pts]).close().extrude(hi - lo).val()


def fuse(base, *others):
    out = base
    for o in others:
        out = out.fuse(o)
    return out.clean()


def cut(base, *others):
    return base.cut(*others).clean() if others else base


def finger(side, yc, x_face, zs):
    """Alt gövdeden yükselen geçme tırnağı. side=-1 sol duvar, +1 sağ duvar.
    x_face: duvarın iç yüzü. Çıkıntı dışa (duvara) doğru, üstte rampa."""
    y0, y1 = yc - FINGER_W / 2, yc + FINGER_W / 2
    if side < 0:
        body = box(x_face, x_face + FINGER_T, y0, y1, zs - 3.0, zs + FINGER_L)
        o = x_face
        bump = prism_xz([(o, zs + 4.0), (o - BUMP, zs + 4.0), (o - BUMP, zs + 4.6), (o, zs + 5.8)], y0, y1)
    else:
        body = box(x_face - FINGER_T, x_face, y0, y1, zs - 3.0, zs + FINGER_L)
        o = x_face
        bump = prism_xz([(o, zs + 4.0), (o + BUMP, zs + 4.0), (o + BUMP, zs + 4.6), (o, zs + 5.8)], y0, y1)
    return body.fuse(bump)


def finger_window(side, yc, x_wall_in, x_wall_out, zs):
    lo, hi = sorted((x_wall_in, x_wall_out))
    return box(lo - 0.5, hi + 0.5, yc - FINGER_W / 2 - 0.3, yc + FINGER_W / 2 + 0.3, zs + 3.8, zs + 6.0)


# ===================================================================== HUB
def build_hub():
    """Hub: kart 54 x 42 mm. Ön kenar (y<0) klimaya bakar: IR LED'ler ve alıcı.
    Pil kartın altında, ara plakanın altında. Sağ önde SHT31 için havalandırmalı odacık.
    Kapak alt gövdeye 4 tırnakla oturur, alttan 1 adet M2x10 vida ile sabitlenir."""
    P = {}
    T = 2.0
    IN = (-2.0, 55.4, -5.6, 42.4)       # iç x0, x1, y0, y1
    OUT = (IN[0] - T, IN[1] + T, IN[2] - T, IN[3] + T)
    R_OUT, R_IN = 3.0, 1.0
    FLOOR, CAR0, CAR1 = 1.6, 7.6, 8.6
    PCB_BOT, PCB_TOP = 11.0, 12.6
    ZS = 15.6                           # bölme düzlemi = IR LED ekseni
    CAP_IN, TOP = 22.6, 1.8
    H = CAP_IN + TOP
    LED_X = (8.23, 17.73)               # IR LED eksenleri (LED1, LED2)
    LED_Z = PCB_TOP + 3.0
    RX_X = 30.54                        # VS1838B orta bacağı
    USB_X, USB_Z = 29.0, PCB_TOP + 2.5 + 1.0 + 1.6
    SCREW = (1.0, -3.0)
    CH_X0 = 37.2                        # SHT31 odacığı sol duvarı
    VENT_X = [39.6 + 2.4 * i for i in range(7)]
    POSTS = [(1.0, 1.0), (53.0, 1.0), (1.0, 41.0), (53.0, 41.0)]
    FINGERS = [(-1, 6.0), (-1, 22.0), (1, 4.0), (1, 16.0)]
    STATUS_LEDS = [(12.47, 25.12), (42.77, 17.0)]
    TOUCH = (46.5, 30.5)

    # ---------------------------------------------------------- alt gövde
    base = cut(rbox(OUT[0], OUT[1], OUT[2], OUT[3], 0, ZS, R_OUT),
               rbox(IN[0], IN[1], IN[2], IN[3], FLOOR, ZS + 1, R_IN))
    adds = []
    # Ara plaka ayakları
    for x0, y0 in [(1.0, 0.5), (50.0, 0.5), (1.0, 38.0), (50.0, 38.0)]:
        adds.append(box(x0, x0 + 3.0, y0, y0 + 3.0, FLOOR - 0.1, CAR0))
    # Pil yuvası köşe kılavuzları (503035: 30 x 35 mm + koruma devresi)
    bx0, bx1, by0, by1 = 7.2, 46.8, 5.7, 36.3
    for cx, cy, sx, sy in [(bx0, by0, 1, 1), (bx1, by0, -1, 1), (bx0, by1, 1, -1), (bx1, by1, -1, -1)]:
        adds.append(box(cx, cx - sx * 1.2, cy - sy * 1.2, cy + sy * 5, FLOOR - 0.1, FLOOR + 1.5))
        adds.append(box(cx - sx * 1.2, cx + sx * 5, cy, cy - sy * 1.2, FLOOR - 0.1, FLOOR + 1.5))
    # Vida sütunu (sol ön köşe) ve duvara bağlantısı
    adds.append(cylz(SCREW[0], SCREW[1], 5.6, 0.5, ZS))
    adds.append(box(IN[0] - 0.1, SCREW[0], -4.0, -2.0, 0.5, ZS))
    # SHT31 odacığı duvarları
    adds.append(box(CH_X0, CH_X0 + 1.2, IN[2] - 0.1, -0.4, FLOOR - 0.1, ZS))
    adds.append(box(CH_X0, IN[1] + 0.1, -1.6, -0.4, FLOOR - 0.1, ZS))
    # Geçme tırnakları
    for side, yc in FINGERS:
        adds.append(finger(side, yc, IN[0] if side < 0 else IN[1], ZS))
    base = fuse(base, *adds)

    cuts = []
    # Vida: geçiş deliği + alttan baş yuvası
    cuts.append(cylz(SCREW[0], SCREW[1], 2.3, -1, ZS + 1))
    cuts.append(cylz(SCREW[0], SCREW[1], 4.2, -1, ZS - 4.0))
    # Ön duvar: IR LED yarım delikleri, alıcı penceresi, odacık ızgarası
    for x in LED_X:
        cuts.append(cyly(x, LED_Z, 5.3, OUT[2] - 1, IN[2] + 0.6))
    cuts.append(box(RX_X - 5.0, RX_X + 5.0, OUT[2] - 1, IN[2] + 0.6, PCB_TOP + 0.5, ZS + 1))
    for x in VENT_X:
        cuts.append(box(x, x + 1.2, OUT[2] - 1, IN[2] + 0.6, 3.0, ZS - 1.5))
        cuts.append(box(x, x + 1.2, -5.0, -2.2, -1, FLOOR + 0.5))  # taban ızgarası
    # Arka duvar: USB-C
    cuts.append(box(USB_X - USB_W / 2, USB_X + USB_W / 2, IN[3] - 0.5, OUT[3] + 1, USB_Z - USB_H / 2, ZS + 1))
    base = cut(base, *cuts)

    # ---------------------------------------------------------- kapak
    cap = cut(rbox(OUT[0], OUT[1], OUT[2], OUT[3], ZS, H, R_OUT),
              rbox(IN[0] - FIT, IN[1] + FIT, IN[2] - FIT, IN[3] + FIT, ZS - 1, CAP_IN, R_IN))
    adds = []
    for x, y in POSTS:  # kartı bastıran ayaklar
        adds.append(cylz(x, y, 2.2, PCB_TOP + 0.05, CAP_IN + 0.1))
    adds.append(cylz(SCREW[0], SCREW[1], 5.6, ZS, CAP_IN + 0.1))
    adds.append(box(IN[0] - FIT - 0.1, SCREW[0], -4.0, -2.0, ZS, CAP_IN + 0.1))
    adds.append(box(CH_X0, CH_X0 + 1.2, IN[2] - FIT - 0.1, -0.4, ZS, CAP_IN + 0.1))
    adds.append(box(CH_X0, IN[1] + FIT + 0.1, -1.6, -0.4, ZS, CAP_IN + 0.1))
    # TTP223 çerçevesi (modül 11-12 x 15-16 mm, dokunma yüzü kapağa yapışık)
    fx0, fx1, fy0, fy1 = TOUCH[0] - 6.3, TOUCH[0] + 6.3, TOUCH[1] - 8.3, TOUCH[1] + 8.3
    frame = cut(box(fx0 - 1.0, fx1 + 1.0, fy0 - 1.0, fy1 + 1.0, CAP_IN - 1.5, CAP_IN + 0.1),
                box(fx0, fx1, fy0, fy1, CAP_IN - 2, CAP_IN + 1),
                box(fx0 - 2, fx0 + 1, TOUCH[1] - 3.5, TOUCH[1] + 3.5, CAP_IN - 2, CAP_IN + 1))
    adds.append(frame)
    cap = fuse(cap, *adds)

    cuts = []
    cuts.append(cylz(SCREW[0], SCREW[1], 1.7, ZS - 1, ZS + 6.5))           # M2 kılavuz deliği
    cuts.append(box(50.0, 53.0, -1.7, -0.3, CAP_IN - 2.5, CAP_IN + 0.2))  # SHT31 kablo geçişi
    for x in LED_X:
        cuts.append(cyly(x, LED_Z, 5.3, OUT[2] - 1, IN[2] + 0.6))
    cuts.append(box(RX_X - 5.0, RX_X + 5.0, OUT[2] - 1, IN[2] + 0.6, ZS - 1, PCB_TOP + 8.5))
    for x in VENT_X:
        cuts.append(box(x, x + 1.2, OUT[2] - 1, IN[2] + 0.6, ZS + 1.0, CAP_IN - 1.0))
        cuts.append(box(x, x + 1.2, -5.0, -2.2, CAP_IN - 1, H + 1))         # üst ızgara
    cuts.append(box(USB_X - USB_W / 2, USB_X + USB_W / 2, IN[3] - 0.5, OUT[3] + 1, ZS - 1, USB_Z + USB_H / 2))
    for x in [8.0, 10.4, 12.8, 15.2, 40.0, 42.4, 44.8, 47.2]:            # arka havalandırma
        cuts.append(box(x, x + 1.2, IN[3] - 0.5, OUT[3] + 1, ZS + 1.5, CAP_IN - 1.5))
    for side, yc in FINGERS:
        if side < 0:
            cuts.append(finger_window(side, yc, IN[0] - FIT, OUT[0], ZS))
        else:
            cuts.append(finger_window(side, yc, IN[1] + FIT, OUT[1], ZS))
    for x, y in STATUS_LEDS:
        cuts.append(cylz(x, y, 3.2, CAP_IN - 1, H + 1))
        cuts.append(cone_z(x, y, 3.2, 4.6, H - 0.7, H + 0.01))
    cuts.append(cylz(TOUCH[0], TOUCH[1], 14.0, H - 0.8, H + 1))         # dokunma çukuru
    cuts.append(box(1.5, 9.5, 33.5, 39.8, CAP_IN - 0.1, CAP_IN + 1.0))    # pil fişi cebi
    cap = cut(cap, *cuts)

    # ---------------------------------------------------------- ara plaka
    car = rbox(0.6, 54.0, -0.2, 42.0, CAR0, CAR1, 1.0)
    rails = [box(0.6, 53.8, 0.2, 1.4, CAR1 - 0.1, PCB_BOT), box(0.6, 53.8, 40.6, 41.8, CAR1 - 0.1, PCB_BOT),
             box(0.6, 1.4, 0.2, 41.8, CAR1 - 0.1, PCB_BOT), box(52.6, 53.8, 0.2, 41.8, CAR1 - 0.1, PCB_BOT)]
    car = fuse(car, *rails)

    # ---------------------------------------------------------- temsili elemanlar
    dummy = {
        "kart": rbox(0, 54, 0, 42, PCB_BOT, PCB_TOP, 2.0),
        "xiao": fuse(box(20.1, 37.9, 19.7, 40.7, PCB_TOP + 2.5, PCB_TOP + 3.5),
                     box(21.0, 37.0, 20.5, 39.5, PCB_TOP + 3.5, PCB_TOP + 6.0),
                     box(USB_X - 4.5, USB_X + 4.5, 34.7, 42.0, PCB_TOP + 3.5, PCB_TOP + 6.7),
                     box(20.4, 22.4, 21.3, 39.1, PCB_TOP, PCB_TOP + 2.5),
                     box(35.6, 37.6, 21.3, 39.1, PCB_TOP, PCB_TOP + 2.5)),
        "usb_fis": box(USB_X - 5.5, USB_X + 5.5, 42.6, 70, USB_Z - 3.25, USB_Z + 3.25),
        "ir_led": fuse(*[fuse(cyly(x, LED_Z, 5.0, -8.6, 0), cyly(x, LED_Z, 5.8, -1.0, 0)) for x in LED_X]),
        "ir_alici": box(RX_X - 3.0, RX_X + 3.0, 1.5, 6.0, PCB_TOP + 1.0, PCB_TOP + 8.0),
        "q1": box(16.8, 22.3, 10.0, 14.0, PCB_TOP + 1.0, PCB_TOP + 6.0),
        "c2": cylz(7.75, 13.0, 6.3, PCB_TOP, PCB_TOP + 7.5),
        "pil_soketi": box(2.0, 9.0, 34.3, 39.8, PCB_TOP, CAP_IN + 0.9),
        "durum_led": fuse(*[fuse(cylz(x, y, 3.0, PCB_TOP + 5.0, H - 0.2), cylz(x, y, 3.8, PCB_TOP + 5.0, PCB_TOP + 6.0))
                            for x, y in STATUS_LEDS]),
        "ttp223": box(TOUCH[0] - 6.0, TOUCH[0] + 6.0, TOUCH[1] - 8.0, TOUCH[1] + 8.0, CAP_IN - 2.6, CAP_IN),
        "sht31": box(CH_X0 + 2.3, CH_X0 + 16.3, -4.6, -2.0, 3.0, 14.0),
        "pil": rbox(bx0 + 0.3, bx1 - 0.3, by0 + 0.3, by1 - 0.3, FLOOR, CAR0 - 0.6, 1.0),
        "vida": fuse(cylz(SCREW[0], SCREW[1], 1.6, ZS - 4.0, ZS + 6.0), cylz(SCREW[0], SCREW[1], 3.8, ZS - 6.0, ZS - 4.0)),
    }
    P.update(alt_govde=base, kapak=cap, ara_plaka=car)
    info = dict(size=(OUT[1] - OUT[0], OUT[3] - OUT[2], H), zs=ZS, explode={"kapak": 22, "ara_plaka": 9},
                on_lid=("ttp223",))
    return P, dummy, info


# ================================================================== SENSÖR
def build_sensor():
    """Sensör: kart 25 x 44 mm, duvarda dikey (y=0 üst kenar, USB-C altta).
    Arka gövde duvara çift taraflı bantla yapışır. Pil kartın arkasında.
    Üstte SHT31 odacığı: kapaktaki perde kartın yarığından geçer, ısıyı ayırır.
    Ön kapak 4 tırnakla oturur; arkadan 1 adet M2x10 vida kartı ve kapağı sıkıştırır."""
    P = {}
    T = 1.6
    IN = (-2.8, 27.8, -0.4, 45.0)
    OUT = (IN[0] - T, IN[1] + T, IN[2] - T, IN[3] + T)
    R_OUT, R_IN = 3.0, 1.4
    BACK = 1.6
    LEDGE0, CAR0, CAR1 = 7.0, 8.2, 9.2
    PCB_BOT, PCB_TOP = 11.2, 12.8
    ZS = 14.6
    CAP_IN, TOP = 22.4, 1.6
    H = CAP_IN + TOP
    HOLE = (3.6, 3.0)
    USB_X, USB_Z = 12.0, PCB_TOP + 2.5 + 1.0 + 1.6
    POSTS = [(24.0, 1.0), (1.0, 43.0), (24.0, 43.0)]
    FINGERS = [(-1, 24.0), (-1, 38.0), (1, 24.0), (1, 38.0)]
    SLOT = (8.0, 12.4, 16.0, 14.6)

    # ---------------------------------------------------------- arka gövde
    base = cut(rbox(OUT[0], OUT[1], OUT[2], OUT[3], 0, ZS, R_OUT),
               rbox(IN[0], IN[1], IN[2], IN[3], BACK, ZS + 1, R_IN))
    adds = []
    # Ara plakayı taşıyan pahlı rafler (pil kablosu için sol üstte boşluk)
    for side in (-1, 1):
        xw = IN[0] if side < 0 else IN[1]
        segs = [(IN[2], 4.0), (20.0, IN[3])] if side < 0 else [(IN[2], IN[3])]
        for y0, y1 in segs:
            adds.append(prism_xz([(xw + side * 0.1, LEDGE0), (xw - side * 1.2, CAR0), (xw + side * 0.1, CAR0)], y0, y1))
    # Vida sütunu
    adds.append(cylz(HOLE[0], HOLE[1], 5.6, 0.5, PCB_BOT))
    adds.append(box(HOLE[0] - 1.0, HOLE[0] + 1.0, IN[2] - 0.1, HOLE[1], 0.5, CAR0 - 0.2))
    for side, yc in FINGERS:
        adds.append(finger(side, yc, IN[0] if side < 0 else IN[1], ZS))
    base = fuse(base, *adds)
    cuts = [cylz(HOLE[0], HOLE[1], 2.3, -1, PCB_BOT + 1),
            cylz(HOLE[0], HOLE[1], 4.0, -1, PCB_BOT - 3.0),
            box(USB_X - USB_W / 2, USB_X + USB_W / 2, IN[3] - 0.5, OUT[3] + 1, USB_Z - USB_H / 2, ZS + 1)]
    for x in [7.0 + 2.4 * i for i in range(6)]:  # üst duvar: odacık hava girişi (kart üstü)
        cuts.append(box(x, x + 1.2, OUT[2] - 1, IN[2] + 0.5, PCB_TOP + 0.4, ZS + 1))
    base = cut(base, *cuts)

    # ---------------------------------------------------------- ön kapak
    cap = cut(rbox(OUT[0], OUT[1], OUT[2], OUT[3], ZS, H, R_OUT),
              rbox(IN[0] - FIT, IN[1] + FIT, IN[2] - FIT, IN[3] + FIT, ZS - 1, CAP_IN, R_IN))
    adds = [cylz(x, y, 2.0, PCB_TOP + 0.05, CAP_IN + 0.1) for x, y in POSTS]
    adds.append(cylz(HOLE[0], HOLE[1], 5.0, PCB_TOP, CAP_IN + 0.1))
    # Odacık perdesi + yarıktan geçen dil
    adds.append(box(IN[0] - FIT - 0.1, IN[1] + FIT + 0.1, 12.7, 14.3, ZS, CAP_IN + 0.1))
    adds.append(box(IN[0] + 0.2, IN[1] - 0.2, 12.7, 14.3, PCB_TOP + 0.2, ZS + 0.1))
    adds.append(box(SLOT[0] + 0.8, SLOT[2] - 0.8, SLOT[1] + 0.4, SLOT[3] - 0.4, CAR1 + 0.3, PCB_TOP + 0.3))
    # SHT31 modülü rayları (çip havalandırmaya bakar, raylar 1.5 mm boşluk bırakır)
    adds.append(box(7.0, 23.0, -0.3, 0.5, CAP_IN - 1.5, CAP_IN + 0.1))
    adds.append(box(7.0, 23.0, 11.9, 12.7, CAP_IN - 1.5, CAP_IN + 0.1))
    cap = fuse(cap, *adds)
    cuts = [cylz(HOLE[0], HOLE[1], 1.7, PCB_TOP - 1, CAP_IN - 1.5),
            box(USB_X - USB_W / 2, USB_X + USB_W / 2, IN[3] - 0.5, OUT[3] + 1, ZS - 1, USB_Z + USB_H / 2),
            box(4.0, 12.5, 15.4, 21.9, CAP_IN - 0.1, CAP_IN + 0.8)]               # pil fişi cebi
    for yv in (2.0, 4.4, 6.8, 9.2):                                            # ön ızgara
        cuts.append(box(7.5, 22.5, yv, yv + 1.2, CAP_IN - 1, H + 1))
    for x in [7.0 + 2.4 * i for i in range(6)]:                                # üst ızgara
        cuts.append(box(x, x + 1.2, OUT[2] - 1, IN[2] + 0.5, ZS - 1, CAP_IN - 1.0))
    for yv in (2.0, 5.0, 8.0):                                                 # yan ızgaralar
        cuts.append(box(OUT[0] - 1, IN[0] + 0.5, yv, yv + 1.2, ZS + 1.5, CAP_IN - 1.5))
        cuts.append(box(IN[1] - 0.5, OUT[1] + 1, yv, yv + 1.2, ZS + 1.5, CAP_IN - 1.5))
    for side, yc in FINGERS:
        if side < 0:
            cuts.append(finger_window(side, yc, IN[0] - FIT, OUT[0], ZS))
        else:
            cuts.append(finger_window(side, yc, IN[1] + FIT, OUT[1], ZS))
    cap = cut(cap, *cuts)

    # ---------------------------------------------------------- ara plaka
    car = cut(rbox(IN[0] + 0.3, IN[1] - 0.3, -0.1, 44.6, CAR0, CAR1, 1.0),
              cylz(HOLE[0], HOLE[1], 6.2, CAR0 - 1, CAR1 + 1),
              box(IN[0], -1.0, 4.0, 20.0, CAR0 - 1, CAR1 + 1))                # pil kablosu geçişi
    rails = [box(0.2, 1.4, 6.6, 43.8, CAR1 - 0.1, PCB_BOT), box(23.6, 24.8, 0.2, 43.8, CAR1 - 0.1, PCB_BOT),
             box(6.8, 24.8, 0.2, 1.4, CAR1 - 0.1, PCB_BOT), box(0.2, 24.8, 42.6, 43.8, CAR1 - 0.1, PCB_BOT),
             box(-1.0, -0.3, 6.6, 43.8, CAR1 - 0.1, PCB_BOT + 0.8), box(25.3, 26.0, 0.2, 43.8, CAR1 - 0.1, PCB_BOT + 0.8)]
    car = fuse(car, *rails)

    # ---------------------------------------------------------- temsili elemanlar
    sx0, sy0, sx1, sy1 = SLOT
    slot = fuse(box(sx0 + 1.1, sx1 - 1.1, sy0, sy1, PCB_BOT - 1, PCB_TOP + 1),
                cylz(sx0 + 1.1, (sy0 + sy1) / 2, sy1 - sy0, PCB_BOT - 1, PCB_TOP + 1),
                cylz(sx1 - 1.1, (sy0 + sy1) / 2, sy1 - sy0, PCB_BOT - 1, PCB_TOP + 1))
    dummy = {
        "kart": cut(rbox(0, 25, 0, 44, PCB_BOT, PCB_TOP, 2.0), slot, cylz(HOLE[0], HOLE[1], 2.2, PCB_BOT - 1, PCB_TOP + 1)),
        "xiao": fuse(box(3.1, 20.9, 22.5, 43.5, PCB_TOP + 2.5, PCB_TOP + 3.5),
                     box(4.0, 20.0, 23.5, 42.5, PCB_TOP + 3.5, PCB_TOP + 5.5),
                     box(USB_X - 4.5, USB_X + 4.5, 37.5, 44.8, PCB_TOP + 3.5, PCB_TOP + 6.7),
                     box(3.4, 5.4, 24.0, 42.0, PCB_TOP, PCB_TOP + 2.5),
                     box(18.6, 20.6, 24.0, 42.0, PCB_TOP, PCB_TOP + 2.5)),
        "usb_fis": box(USB_X - 5.5, USB_X + 5.5, 45.4, 70, USB_Z - 3.25, USB_Z + 3.25),
        "pil": rbox(-2.4, 27.4, 6.0, 44.9, BACK, LEDGE0, 1.0),
        "sht31": box(7.5, 21.5, 0.6, 11.6, CAP_IN - 1.5 - 2.5, CAP_IN - 1.5),
        "pil_soketi": box(4.8, 11.8, 16.0, 21.5, PCB_TOP, CAP_IN + 0.7),
        "j2_kablo": box(13.0, 17.6, 18.0, 20.0, PCB_TOP, PCB_TOP + 3.0),
        "vida": fuse(cylz(HOLE[0], HOLE[1], 1.6, PCB_BOT - 3.0, PCB_BOT + 7.0),
                     cylz(HOLE[0], HOLE[1], 3.8, PCB_BOT - 4.6, PCB_BOT - 3.0)),
    }
    P.update(arka_govde=base, on_kapak=cap, ara_plaka=car)
    info = dict(size=(OUT[1] - OUT[0], OUT[3] - OUT[2], H), zs=ZS, explode={"on_kapak": 22, "ara_plaka": 9},
                on_lid=("sht31",))
    return P, dummy, info


# ============================================================== denetim ve çıktı
def check(name, parts, dummy):
    """Kutu parçaları birbirine ve temsili elemanlara değmemeli (vida hariç: diş açar)."""
    problems = []
    names = list(parts)
    for i in range(len(names)):
        for j in range(i + 1, len(names)):
            v = parts[names[i]].intersect(parts[names[j]]).Volume()
            if v > 0.01:
                problems.append(f"{names[i]} <-> {names[j]}: {v:.2f} mm3")
    for dn, d in dummy.items():
        for pn, p in parts.items():
            v = d.intersect(p).Volume()
            if v > 0.01:
                problems.append(f"{dn} <-> {pn}: {v:.2f} mm3")
    dn = list(dummy)
    for i in range(len(dn)):
        for j in range(i + 1, len(dn)):
            if {dn[i], dn[j]} & {"vida"} and {dn[i], dn[j]} & {"kart"}:
                continue
            v = dummy[dn[i]].intersect(dummy[dn[j]]).Volume()
            if v > 0.01:
                problems.append(f"(eleman) {dn[i]} <-> {dn[j]}: {v:.2f} mm3")
    print(f"[{name}] çakışma denetimi: {'TEMİZ' if not problems else str(len(problems)) + ' sorun'}")
    for p in problems:
        print("    ÇAKIŞMA:", p)
    return problems


COLORS = {
    "alt_govde": (0.86, 0.87, 0.89), "arka_govde": (0.86, 0.87, 0.89),
    "kapak": (0.62, 0.74, 0.90), "on_kapak": (0.62, 0.74, 0.90),
    "ara_plaka": (0.95, 0.78, 0.40),
    "kart": (0.20, 0.55, 0.30), "xiao": (0.25, 0.25, 0.30), "usb_fis": (0.15, 0.15, 0.15),
    "ir_led": (0.55, 0.45, 0.75), "ir_alici": (0.10, 0.10, 0.10), "q1": (0.10, 0.10, 0.10),
    "c2": (0.15, 0.25, 0.55), "pil_soketi": (0.95, 0.95, 0.90), "durum_led": (0.90, 0.30, 0.30),
    "ttp223": (0.85, 0.20, 0.25), "sht31": (0.45, 0.25, 0.60), "pil": (0.70, 0.72, 0.75),
    "vida": (0.60, 0.60, 0.60), "j2_kablo": (0.85, 0.10, 0.10),
}


def export(name, parts, dummy, info):
    outdir = os.path.join(HERE, name)
    os.makedirs(outdir, exist_ok=True)
    for pn, shape in parts.items():
        base = os.path.join(outdir, f"{name}_{pn}")
        cq.exporters.export(cq.Workplane().add(shape), base + ".stl", tolerance=0.02, angularTolerance=0.15)
        cq.exporters.export(cq.Workplane().add(shape), base + ".step")
    asm = cq.Assembly(name=f"{name}_montaj")
    for pn, shape in parts.items():
        asm.add(shape, name=pn, color=cq.Color(*COLORS.get(pn, (0.8, 0.8, 0.8))))
    for dn, shape in dummy.items():
        asm.add(shape, name=f"temsili_{dn}", color=cq.Color(*COLORS.get(dn, (0.5, 0.5, 0.5))))
    asm.save(os.path.join(outdir, f"{name}_montaj.step"))
    sx, sy, sz = info["size"]
    print(f"[{name}] dış ölçü {sx:.1f} x {sy:.1f} x {sz:.1f} mm; dosyalar: {outdir}")


# ------------------------------------------------------------------ görseller
def _mesh(shape, tol=0.08):
    import numpy as np

    verts, tris = shape.tessellate(tol, 0.3)
    v = np.array([(p.x, p.y, p.z) for p in verts])
    return v, np.array(tris)


LABELS = {
    "alt_govde": "Alt govde", "arka_govde": "Arka govde", "kapak": "Kapak", "on_kapak": "On kapak",
    "ara_plaka": "Ara plaka", "kart": "Kart (PCB)", "xiao": "XIAO", "usb_fis": "USB-C fisi", "ir_led": "IR LED",
    "ir_alici": "IR alici", "q1": "Q1", "c2": "C2", "pil_soketi": "Pil soketi", "durum_led": "Durum LED",
    "ttp223": "TTP223", "sht31": "SHT31", "pil": "Pil 503035", "vida": "M2 vida", "j2_kablo": "XBAT kablosu",
}

VIEWER = r"""<!doctype html>
<html lang="tr"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>__TITLE__</title>
<style>
:root{--bg:#f4f5f7;--fg:#1f2937;--panel:#ffffffee;--line:#d1d5db}
@media (prefers-color-scheme:dark){:root{--bg:#111318;--fg:#e5e7eb;--panel:#1f2329ee;--line:#374151}}
html,body{margin:0;height:100%;background:var(--bg);color:var(--fg);font:14px system-ui,sans-serif}
#ui{position:fixed;left:12px;top:12px;background:var(--panel);border:1px solid var(--line);border-radius:10px;
padding:10px 12px;max-width:260px;max-height:calc(100% - 48px);overflow:auto}
#ui h1{font-size:15px;margin:0 0 6px}#ui p{margin:4px 0 8px;opacity:.8;font-size:12px}
button{font:inherit;margin:2px 2px 2px 0;padding:4px 8px;border-radius:6px;border:1px solid var(--line);
background:transparent;color:inherit;cursor:pointer}label{display:block;font-size:13px;margin:2px 0}
.sw{display:inline-block;width:10px;height:10px;border-radius:2px;margin-right:6px;vertical-align:middle}
body.shot #ui{display:none}
</style></head><body>
<div id="ui"><h1>__TITLE__</h1><p>__SIZE__ &middot; surukle: dondur, tekerlek: yakinlas</p>
<div><button data-v="on">On</button><button data-v="arka">Arka</button><button data-v="ust">Ust</button>
<button data-v="alt">Alt</button></div>
<div><button id="ex">Patlat / Topla</button><button id="sec">Kesit</button></div><div id="parts"></div></div>
<script src="https://cdnjs.cloudflare.com/ajax/libs/three.js/r128/three.min.js"></script>
<script src="https://cdn.jsdelivr.net/npm/three@0.128.0/examples/js/controls/OrbitControls.js"></script>
<script>
const DATA = __DATA__;
const VIEWS = __VIEWS__;
const q = new URLSearchParams(location.search);
if (q.get("shot")) document.body.classList.add("shot");
const renderer = new THREE.WebGLRenderer({antialias: true, preserveDrawingBuffer: true});
renderer.setPixelRatio(Math.min(2, window.devicePixelRatio || 1));
renderer.setSize(innerWidth, innerHeight);
renderer.localClippingEnabled = true;
document.body.appendChild(renderer.domElement);
const scene = new THREE.Scene();
const dark = matchMedia("(prefers-color-scheme: dark)").matches && !q.get("shot");
scene.background = new THREE.Color(dark ? 0x111318 : 0xf4f5f7);
const cam = new THREE.PerspectiveCamera(30, innerWidth / innerHeight, 1, 5000);
cam.up.set(0, 0, 1);
scene.add(new THREE.HemisphereLight(0xffffff, 0x6b7280, 0.5));
const sun = new THREE.DirectionalLight(0xffffff, 0.55); sun.position.set(80, 120, 200); scene.add(sun);
const fill = new THREE.DirectionalLight(0xffffff, 0.2); fill.position.set(-120, -60, 40); scene.add(fill);
const clip = new THREE.Plane(new THREE.Vector3(-1, 0, 0), 1e6);
const objs = [];
function b64(s) { const b = atob(s), a = new Uint8Array(b.length); for (let i = 0; i < b.length; i++) a[i] = b.charCodeAt(i); return a.buffer; }
const box = new THREE.Box3();
for (const d of DATA) {
  const raw = new Int16Array(b64(d.pos)), pos = new Float32Array(raw.length);
  for (let i = 0; i < raw.length; i++) pos[i] = raw[i] * d.scale;
  const g = new THREE.BufferGeometry(); g.setAttribute("position", new THREE.BufferAttribute(pos, 3));
  g.computeVertexNormals();
  const mat = new THREE.MeshStandardMaterial({color: new THREE.Color(...d.color), roughness: .75, metalness: 0,
    side: THREE.DoubleSide, clippingPlanes: [clip], polygonOffset: true, polygonOffsetFactor: 1, polygonOffsetUnits: 1});
  const m = new THREE.Mesh(g, mat);
  const e = new THREE.LineSegments(new THREE.EdgesGeometry(g, 35),
    new THREE.LineBasicMaterial({color: 0x30343b, transparent: true, opacity: .35, clippingPlanes: [clip]}));
  const grp = new THREE.Group(); grp.add(m); grp.add(e); scene.add(grp);
  objs.push({d, grp}); if (d.group === "kutu") box.expandByObject(grp);
}
const ctr = box.getCenter(new THREE.Vector3()), rad = box.getSize(new THREE.Vector3()).length() / 2;
const ctl = new THREE.OrbitControls(cam, renderer.domElement); ctl.target.copy(ctr);
let exploded = q.get("explode") === "1";
function applyExplode() { for (const o of objs) o.grp.position.z = exploded ? o.d.dz : 0; }
function view(name) {
  const v = VIEWS[name] || VIEWS.on; const el = v[0] * Math.PI / 180, az = v[1] * Math.PI / 180;
  const dist = rad * (exploded ? 3.8 : 3.5) * (v[2] || 1);
  cam.position.set(ctr.x + dist * Math.cos(el) * Math.cos(az), ctr.y + dist * Math.cos(el) * Math.sin(az),
                   ctr.z + (exploded ? 10 : 0) + dist * Math.sin(el));
  ctl.target.set(ctr.x, ctr.y, ctr.z + (exploded ? 10 : 0)); ctl.update();
}
const list = document.getElementById("parts");
for (const o of objs) {
  const l = document.createElement("label"); const c = document.createElement("input"); c.type = "checkbox";
  c.checked = !o.d.hidden; o.grp.visible = c.checked; c.onchange = () => { o.grp.visible = c.checked; };
  const sw = document.createElement("span"); sw.className = "sw";
  sw.style.background = "rgb(" + o.d.color.map(x => Math.round(x * 255)).join(",") + ")";
  l.append(c, sw, o.d.label); list.appendChild(l);
}
const hide = (q.get("hide") || "").split(",");
for (const o of objs) if (hide.includes(o.d.name)) o.grp.visible = false;
if (q.get("section")) { clip.constant = parseFloat(q.get("section")); }
document.querySelectorAll("[data-v]").forEach(b => b.onclick = () => view(b.dataset.v));
document.getElementById("ex").onclick = () => { exploded = !exploded; applyExplode(); view("on"); };
document.getElementById("sec").onclick = () => { clip.constant = clip.constant > 1e5 ? ctr.x : 1e6; };
applyExplode(); view(q.get("view") || "on");
addEventListener("resize", () => { cam.aspect = innerWidth / innerHeight; cam.updateProjectionMatrix();
  renderer.setSize(innerWidth, innerHeight); });
(function loop() { renderer.render(scene, cam); requestAnimationFrame(loop); })();
window.__ready = true;
</script></body></html>
"""


def viewer(name, parts, dummy, info, out_html):
    """Tarayıcıda döndürülebilen 3D görüntüleyici (tek dosya, three.js)."""
    import base64
    import json

    import numpy as np

    ex = info["explode"]
    lid = ex.get("kapak", ex.get("on_kapak", 0))
    on_pcb = ("kart", "xiao", "ir_led", "ir_alici", "q1", "c2", "pil_soketi", "durum_led", "j2_kablo")
    entries = []
    for group, items in (("kutu", parts), ("eleman", dummy)):
        for k, shape in items.items():
            v, t = _mesh(shape, 0.06)
            tri = v[t].reshape(-1, 3)
            q = np.round(tri / 0.01).astype("<i2")
            if group == "kutu":
                dz = ex.get(k, 0)
            else:
                dz = ex.get("ara_plaka", 0) if k in on_pcb else (lid if k in info.get("on_lid", ()) else 0)
            entries.append(dict(name=k, label=LABELS.get(k, k), color=COLORS.get(k, (0.6, 0.6, 0.6)), dz=dz, group=group,
                                scale=0.01, hidden=False, pos=base64.b64encode(q.tobytes()).decode()))
    sx, sy, sz = info["size"]
    views = {"on": [24, 62], "arka": [24, -118], "ust": [80, -90], "alt": [-40, -60]}
    views.update(info.get("viewer_views", {}))
    html = (VIEWER.replace("__TITLE__", f"KlimaHub {name} kutusu")
            .replace("__SIZE__", f"{sx:.1f} x {sy:.1f} x {sz:.1f} mm")
            .replace("__DATA__", json.dumps(entries, separators=(",", ":")))
            .replace("__VIEWS__", json.dumps(views)))
    with open(out_html, "w", encoding="utf-8") as fh:
        fh.write(html)


def snapshots(name, html, prev):
    """Görüntüleyiciden PNG önizlemeler (Playwright + Chromium; yoksa atlanır)."""
    import subprocess

    script = os.path.join(HERE, "tools", "snapshot.py")
    shots = [("kapali_on", "view=on"), ("kapali_arka", "view=arka"),
             ("patlatilmis", "view=on&explode=1&hide=usb_fis")]
    args = []
    for tag, qs in shots:
        args += [f"{html}?shot=1&{qs}", os.path.join(prev, f"{name}_{tag}.png")]
    for py in (sys.executable, "python3"):
        try:
            r = subprocess.run([py, script, *args], capture_output=True, text=True, timeout=300)
        except (OSError, subprocess.TimeoutExpired):
            continue
        if r.returncode == 0:
            print(f"[{name}] önizleme görselleri: {len(shots)} adet")
            return True
    print(f"[{name}] UYARI: Playwright bulunamadı, PNG önizlemeler güncellenmedi "
          f"(3D görüntüleyici yine de hazır: {os.path.relpath(html, HERE)})")
    return False


def sections(name, parts, dummy, cuts_def, out_png):
    """Kesit görünüşleri (trimesh ile): her parça ayrı renkte."""
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    import trimesh

    meshes = {}
    for k, s in list(parts.items()) + list(dummy.items()):
        v, t = _mesh(s, 0.05)
        meshes[k] = trimesh.Trimesh(vertices=v, faces=t, process=True)
    fig, axs = plt.subplots(1, len(cuts_def), figsize=(7 * len(cuts_def), 6))
    if len(cuts_def) == 1:
        axs = [axs]
    for ax, (title, origin, normal, axes_idx) in zip(axs, cuts_def):
        for k, m in meshes.items():
            sec = m.section(plane_origin=origin, plane_normal=normal)
            if sec is None:
                continue
            for ent in sec.entities:
                pts = sec.vertices[ent.points]
                ax.fill(pts[:, axes_idx[0]] * axes_idx[2], pts[:, axes_idx[1]], color=COLORS.get(k, (0.5, 0.5, 0.5)),
                        alpha=0.85, lw=0.4, ec="black")
        ax.set_aspect("equal")
        ax.set_title(title, fontsize=10)
        ax.grid(True, lw=0.3, alpha=0.5)
    fig.suptitle(f"KlimaHub {name} kutusu - kesitler (mm)", fontsize=13)
    fig.tight_layout()
    fig.savefig(out_png, dpi=110)
    plt.close(fig)


def main():
    which = sys.argv[1:] or ["hub", "sensor"]
    prev = os.path.join(HERE, "onizleme")
    os.makedirs(prev, exist_ok=True)
    bad = 0
    for name in which:
        if name == "hub":
            parts, dummy, info = build_hub()
            cuts_def = [("Yan kesit, IR LED2 ekseninden (x=17.7)", (17.73, 0, 0), (1, 0, 0), (1, 2, -1)),
                        ("Yan kesit, vida ve odacik (x=46)", (46.0, 0, 0), (1, 0, 0), (1, 2, -1)),
                        ("On kesit (y=-30, XIAO ve USB)", (0, -30, 0), (0, 1, 0), (0, 2, 1))]
        else:
            parts, dummy, info = build_sensor()
            cuts_def = [("Dikey kesit (x=12, USB ve odacik)", (12.0, 0, 0), (1, 0, 0), (1, 2, -1)),
                        ("Dikey kesit (x=3.6, vida)", (3.6, 0, 0), (1, 0, 0), (1, 2, -1)),
                        ("Yatay kesit (y=-33)", (0, -33, 0), (0, 1, 0), (0, 2, 1))]
        bad += len(check(name, parts, dummy))
        export(name, parts, dummy, info)
        html = os.path.join(prev, f"{name}_3d.html")
        viewer(name, parts, dummy, info, html)
        snapshots(name, html, prev)
        sections(name, parts, dummy, cuts_def, os.path.join(prev, f"{name}_kesit.png"))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
