#!/usr/bin/env python3
"""Lehimleme için okunaklı montaj haritaları üretir.

    python3 pcb/tools/assembly_maps.py [hub|sensor|all]

Çıktılar (pcb/cnc/<kart>/):
    montaj_ust.png  Üst yüz: bacaklı elemanlar, yön işaretleri (K, +, OUT, E, pin adları)
    montaj_alt.png  Alt yüz (aynalı, elinizdeki kartı çevirdiğinizde gördüğünüz gibi):
                    SMD direnç ve kondansatörler, değerleriyle
"""
import os
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import pcbnew  # noqa: E402
from matplotlib.patches import FancyBboxPatch, Polygon as MplPolygon  # noqa: E402

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from boards import BOARDS  # noqa: E402
from cnc import polyset_to_shapely  # noqa: E402

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
PCB = {"hub": "pcb/hub/klima-hub.kicad_pcb", "sensor": "pcb/sensor/klima-sensor.kicad_pcb"}

XIAO_PINS = {str(i + 1): f"D{i}" for i in range(7)}
XIAO_PINS.update({"8": "D7", "9": "D8", "10": "D9", "11": "D10", "12": "3V3", "13": "GND", "14": "5V"})

# Pad 1'in anlamı (yön işareti)
PIN1 = {"LED": "K", "C2": "+", "J1": "+", "J2": "+", "Q1": "E", "U2": "OUT"}


def mm(v):
    return pcbnew.ToMM(v)


def draw(key, side):
    spec = BOARDS[key]
    board = pcbnew.LoadBoard(os.path.join(ROOT, PCB[key]))
    W, H = spec["width"], spec["height"]
    mirror = side == "alt"

    def X(x):
        return W - x if mirror else x

    scale = 0.42 if key == "hub" else 0.55
    fig, ax = plt.subplots(figsize=(W * scale + 3.5, H * scale + 1.6))
    # Kart ve bakır (soluk)
    ops = pcbnew.SHAPE_POLY_SET()
    board.GetBoardPolygonOutlines(ops)
    outline = polyset_to_shapely(ops)
    for poly in getattr(outline, "geoms", [outline]):
        ax.add_patch(MplPolygon([(X(x), y) for x, y in poly.exterior.coords], closed=True, fc="#f3efe2", ec="#555",
                                lw=1.2, zorder=0))
        for r in poly.interiors:
            ax.add_patch(MplPolygon([(X(x), y) for x, y in r.coords], closed=True, fc="white", ec="#555", lw=1.0,
                                    zorder=1))
    for t in board.GetTracks():
        if t.GetLayer() != pcbnew.B_Cu or t.GetClass() == "PCB_VIA":
            continue
        a, b = t.GetStart(), t.GetEnd()
        ax.plot([X(mm(a.x)), X(mm(b.x))], [mm(a.y), mm(b.y)], color="#d9b38c" if mirror else "#ece0cf",
                lw=mm(t.GetWidth()) * 72 / 25.4 / scale * 0.9, solid_capstyle="round", zorder=1)

    labels = []
    for fp in board.GetFootprints():
        ref, val = fp.GetReference(), fp.GetValue()
        is_smd = fp.GetLayer() == pcbnew.B_Cu
        if ref.startswith("H") or (is_smd and not mirror):
            continue  # üst yüzde SMD yok; vida deliği eleman değil
        for pad in fp.Pads():
            g = polyset_to_shapely(pad.GetEffectivePolygon())
            col = "#b87333" if (is_smd or not mirror) else "#cfa77f"
            for poly in getattr(g, "geoms", [g]):
                ax.add_patch(MplPolygon([(X(x), y) for x, y in poly.exterior.coords], closed=True, fc=col, ec="none",
                                        zorder=2))
            if pad.GetDrillSize().x > 0:
                p = pad.GetPosition()
                ax.add_patch(plt.Circle((X(mm(p.x)), mm(p.y)), mm(pad.GetDrillSize().x) / 2, color="white", zorder=3))
        # Yön ve pin işaretleri: üstte tüm bacaklı elemanlar, altta yalnız kablo lehimlenen konnektörler
        if not is_smd and (not mirror or ref in ("J1", "J2", "J3")):
            for pad in fp.Pads():
                p = pad.GetPosition()
                px, py = X(mm(p.x)), mm(p.y)
                mark = None
                if ref == "U1":
                    mark = XIAO_PINS.get(pad.GetNumber())
                elif ref == "J3":
                    mark = pad.GetNetname().lstrip("+")
                elif pad.GetNumber() == "1":
                    mark = PIN1.get("LED" if ref.startswith("LED") else ref)
                if mark:
                    ax.text(px, py, mark, fontsize=6.5 if ref in ("U1", "J3") else 8, color="#b91c1c",
                            ha="center", va="center", weight="bold", zorder=6,
                            bbox=dict(boxstyle="round,pad=0.12", fc="white", ec="#b91c1c", lw=0.6, alpha=0.9))
        if mirror and not is_smd:
            continue  # altta bacaklı elemanların çerçevesi çizilmez
        bb = fp.GetBoundingBox(False, False)
        x0, x1 = sorted((X(mm(bb.GetLeft())), X(mm(bb.GetRight()))))
        y0, y1 = mm(bb.GetTop()), mm(bb.GetBottom())
        ec = "#1d4ed8" if is_smd else "#0f766e"
        ax.add_patch(FancyBboxPatch((x0, y0), x1 - x0, y1 - y0, boxstyle="round,pad=0,rounding_size=0.4", fc="none",
                                    ec=ec, lw=1.3, zorder=4))
        text = f"U1 {val}\nUSB-C kart kenarına" if ref == "U1" else f"{ref}\n{val}"
        labels.append(((x0 + x1) / 2, (y0 + y1) / 2, text, ec, is_smd))
    for cx, cy, text, ec, is_smd in labels:
        ax.text(cx, cy, text, fontsize=9 if is_smd else 8.5, color=ec, ha="center", va="center", weight="bold",
                zorder=7, bbox=dict(boxstyle="round,pad=0.2", fc="white", ec=ec, lw=0.8, alpha=0.85))

    ax.set_xlim(-2, W + 2)
    ax.set_ylim(H + 2, -10 if (key == "hub" and not mirror) else -2)
    ax.set_aspect("equal")
    ax.axis("off")
    if mirror:
        title = (f"{spec['title']}\nALT YÜZ (bakır) - kartı çevirdiğinizde gördüğünüz gibi. "
                 "Mavi: SMD (1206) elemanlar, alt yüze lehimlenir")
    else:
        title = (f"{spec['title']}\nÜST YÜZ - bacaklı elemanlar buradan takılır, alttan lehimlenir. "
                 "Kırmızı: yön işaretleri (K = kısa bacak, + = artı)")
    ax.set_title(title, fontsize=9.5)
    if key == "hub" and not mirror:
        ax.annotate("ÖN (klimaya bakar)", xy=(W * 0.8, -0.3), xytext=(W * 0.8, -5.0), ha="center", fontsize=9,
                    arrowprops=dict(arrowstyle="<-", color="#555"))
    out = os.path.join(ROOT, "pcb", "cnc", key, f"montaj_{side}.png")
    fig.tight_layout()
    fig.savefig(out, dpi=120)
    plt.close(fig)
    return out


def main():
    which = sys.argv[1] if len(sys.argv) > 1 else "all"
    for key in (["hub", "sensor"] if which == "all" else [which]):
        for side in ("ust", "alt"):
            print(draw(key, side))


if __name__ == "__main__":
    main()
