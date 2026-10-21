#!/usr/bin/env python3
"""KiCad kartından CNC (GRBL / Candle) için G-code üretir.

Tek yüzlü kart: bakır ALT yüzdedir. Tüm işlemler (izolasyon, delme, kesim) plaka bakır yüzü
YUKARI bakacak şekilde tek bağlamada yapılır. Bu yüzden çizim X ekseninde aynalanır
(üstten görünüşün alttan görünüşü).

Makine koordinatları: X sağa, Y ileri (operatörden uzağa), Z yukarı, mm, mutlak.
Sıfır noktası: plakanın SOL-ÖN köşesi. Kart, köşeden MARGIN (4) mm içeriden başlar.
Plakanın başka bir yerine kesmek için Candle'da X0 Y0'ı o noktaya almanız yeterlidir.
Z sıfırı: bakır yüzey (Candle'da Z-probe ile). Candle'ın "heightmap" özelliği önerilir.

Kullanım:
    python3 pcb/tools/cnc.py [hub|sensor|all]  [--angle 30] [--depth 0.1] [--feed 150] ...

Çıktılar: pcb/cnc/<kart>/*.nc ve önizleme PNG'leri.
"""
import argparse
import math
import os
import sys

import pcbnew
from shapely.geometry import LineString, MultiPolygon, Point, Polygon, box
from shapely.ops import unary_union

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
BOARDS = {"hub": "pcb/hub/klima-hub.kicad_pcb", "sensor": "pcb/sensor/klima-sensor.kicad_pcb"}

MARGIN = 4.0  # plaka köşesinden karta mesafe (3.175 mm kesim frezesi plakada kalsın)


# ----------------------------------------------------------------- geometri okuma
def to_mm(v):
    return pcbnew.ToMM(v)


def polyset_to_shapely(ps):
    polys = []
    for i in range(ps.OutlineCount()):
        o = ps.Outline(i)
        ext = [(to_mm(o.CPoint(k).x), to_mm(o.CPoint(k).y)) for k in range(o.PointCount())]
        holes = []
        for h in range(ps.HoleCount(i)):
            hh = ps.Hole(i, h)
            holes.append([(to_mm(hh.CPoint(k).x), to_mm(hh.CPoint(k).y)) for k in range(hh.PointCount())])
        if len(ext) >= 3:
            polys.append(Polygon(ext, holes).buffer(0))
    return unary_union(polys)


def read_board(path):
    board = pcbnew.LoadBoard(path)
    items = []  # (net, geom)
    pads = []   # (ref, num, net, x, y)
    holes = []  # (x, y, d)
    jumpers = []
    npth = []   # (x, y, d) kaplamasız vida delikleri: kesim frezesiyle oyulur
    for fp in board.GetFootprints():
        for pad in fp.Pads():
            if pad.GetAttribute() == pcbnew.PAD_ATTRIB_NPTH:
                p = pad.GetPosition()
                d = round(to_mm(pad.GetDrillSize().x), 2)
                holes.append((to_mm(p.x), to_mm(p.y), d))
                npth.append((to_mm(p.x), to_mm(p.y), d))
                continue
            if not pad.IsOnLayer(pcbnew.B_Cu):
                continue
            g = polyset_to_shapely(pad.GetEffectivePolygon())
            items.append((pad.GetNetname(), g))
            p = pad.GetPosition()
            pads.append((fp.GetReference(), pad.GetNumber(), pad.GetNetname(), to_mm(p.x), to_mm(p.y)))
            if pad.GetDrillSize().x > 0:
                holes.append((to_mm(p.x), to_mm(p.y), round(to_mm(pad.GetDrillSize().x), 2)))
    for t in board.GetTracks():
        if t.GetClass() == "PCB_VIA":
            p = t.GetPosition()
            holes.append((to_mm(p.x), to_mm(p.y), round(to_mm(t.GetDrillValue()), 2)))
            items.append((t.GetNetname(), Point(to_mm(p.x), to_mm(p.y)).buffer(to_mm(t.GetWidth()) / 2)))
            continue
        if t.GetLayer() != pcbnew.B_Cu:
            jumpers.append((t.GetNetname(), to_mm(t.GetStart().x), to_mm(t.GetStart().y),
                            to_mm(t.GetEnd().x), to_mm(t.GetEnd().y)))
            continue
        a, b = t.GetStart(), t.GetEnd()
        g = LineString([(to_mm(a.x), to_mm(a.y)), (to_mm(b.x), to_mm(b.y))]).buffer(to_mm(t.GetWidth()) / 2)
        items.append((t.GetNetname(), g))
    for z in board.Zones():
        if z.GetIsRuleArea() or not z.IsOnLayer(pcbnew.B_Cu):
            continue
        g = polyset_to_shapely(z.GetFilledPolysList(pcbnew.B_Cu))
        if not g.is_empty:
            items.append((z.GetNetname(), g))
    ops = pcbnew.SHAPE_POLY_SET()
    board.GetBoardPolygonOutlines(ops)
    outline = polyset_to_shapely(ops)
    return dict(items=items, pads=pads, holes=holes, npth=npth, outline=outline, jumpers=jumpers)


# ------------------------------------------------------------- koordinat dönüşümü
class Frame:
    """KiCad üst görünüm -> makine (alttan görünüş, X aynalı)."""

    def __init__(self, outline):
        minx, miny, maxx, maxy = outline.bounds
        self.minx, self.miny, self.maxx, self.maxy = minx, miny, maxx, maxy

    def pt(self, x, y):
        return (self.maxx - x + MARGIN, self.maxy - y + MARGIN)

    def geom(self, g):
        from shapely.affinity import affine_transform

        # x' = -x + (maxx + M), y' = -y + (maxy + M)
        return affine_transform(g, [-1, 0, 0, -1, self.maxx + MARGIN, self.maxy + MARGIN])


# ------------------------------------------------------------------ takım yolları
def boundaries(geom):
    """Poligon(lar)ın tüm sınır halkalarını koordinat listeleri olarak döndürür."""
    out = []
    polys = [geom] if isinstance(geom, Polygon) else list(getattr(geom, "geoms", []))
    for p in polys:
        if p.is_empty:
            continue
        out.append(list(p.exterior.coords))
        for r in p.interiors:
            out.append(list(r.coords))
    return out


def order_paths(paths, start=(0.0, 0.0)):
    """En yakın komşu sıralaması (boşta hareketi azaltır). Kapalı halkalar en yakın noktadan başlatılır."""
    rest = [list(p) for p in paths if len(p) >= 2]
    ordered = []
    cur = start
    while rest:
        best_i, best_k, best_d = 0, 0, float("inf")
        for i, p in enumerate(rest):
            closed = p[0] == p[-1]
            cand = range(len(p) - 1) if closed else (0, len(p) - 1)
            for k in cand:
                d = (p[k][0] - cur[0]) ** 2 + (p[k][1] - cur[1]) ** 2
                if d < best_d:
                    best_i, best_k, best_d = i, k, d
        p = rest.pop(best_i)
        if p[0] == p[-1]:
            ring = p[:-1]
            ring = ring[best_k:] + ring[:best_k]
            p = ring + [ring[0]]
        elif best_k != 0:
            p = p[::-1]
        ordered.append(p)
        cur = p[-1]
    return ordered


def simplify_path(p, tol=0.005):
    ls = LineString(p).simplify(tol, preserve_topology=False)
    return list(ls.coords)


# --------------------------------------------------------------------- G-code
class GCode:
    """GRBL uyumlu G-code yazıcı. Yorumlar ASCII'ye çevrilir ve iç içe parantez içermez."""

    def __init__(self, title, args, tool_note):
        self.lines = []
        self.args = args
        self.raised = True  # başlangıçta Z güvenli yükseklikte (z_clear)
        self.comment(title)
        self.comment(f"Takim: {tool_note}")
        self.comment(f"Birim mm, mutlak koordinat. X/Y sifiri: plakanin sol-on kosesi, kart {MARGIN:g} mm iceriden baslar")
        self.comment("Z sifiri: bakir yuzey - Candle ile Z-probe yapin; izolasyonda heightmap onerilir")
        self.comment("Uretici: pcb/tools/cnc.py - github.com/erdoganeray/iot-air-conditioner")
        for c in ("G21", "G90", "G94", "G17"):
            self.lines.append(c)
        self.lines.append(f"G0 Z{args.z_clear:.3f}")
        self.lines.append(f"M3 S{args.rpm}")
        self.lines.append("G4 P3")

    @staticmethod
    def clean(t):
        tr = str.maketrans("çğıöşüÇĞİÖŞÜ()", "cgiosuCGIOSU[]")
        return "".join(ch for ch in t.translate(tr) if 32 <= ord(ch) < 127)

    def f(self, v):
        return f"{v:.4f}".rstrip("0").rstrip(".") if abs(v) >= 1e-9 else "0"

    def _move(self, word, x=None, y=None, z=None, feed=None):
        s = word
        if x is not None:
            s += f" X{self.f(x)}"
        if y is not None:
            s += f" Y{self.f(y)}"
        if z is not None:
            s += f" Z{self.f(z)}"
        if feed is not None:
            s += f" F{feed:.0f}"
        self.lines.append(s)

    def rapid(self, x=None, y=None, z=None):
        self._move("G0", x, y, z)

    def cut(self, x=None, y=None, z=None, feed=None):
        self._move("G1", x, y, z, feed)

    def goto(self, x, y):
        """Bir sonraki işlem noktasına güvenli geçiş. İlk geçiş z_clear yüksekliğinde yapılır
        (plaka kıskaçlarına çarpmamak için), sonrakiler z_safe yüksekliğinde."""
        if self.raised:
            self.rapid(x=x, y=y)
            self.rapid(z=self.args.z_safe)
            self.raised = False
        else:
            self.rapid(z=self.args.z_safe)
            self.rapid(x=x, y=y)

    def comment(self, t):
        self.lines.append(f"({self.clean(t)})")

    def end(self):
        self.lines.append(f"G0 Z{self.args.z_clear:.3f}")
        self.lines.append("M5")
        self.lines.append("G0 X0 Y0")
        self.lines.append("M2")

    def save(self, path):
        with open(path, "w", newline="\n") as fh:
            fh.write("\n".join(self.lines) + "\n")


# ------------------------------------------------------------------- işlemler
def vbit_width(args):
    return args.tip + 2.0 * args.depth * math.tan(math.radians(args.angle / 2.0))


def isolation(copper_m, board_m, args):
    w = vbit_width(args)
    step = w * (1.0 - args.overlap)
    region = board_m.buffer(1.0)
    paths = []
    for k in range(args.passes):
        r = w / 2.0 + k * step
        g = copper_m.buffer(r, resolution=16).intersection(region.buffer(r + 0.5))
        for ring in boundaries(g):
            ring = simplify_path(ring)
            line = LineString(ring)
            clipped = line.intersection(region)
            parts = [clipped] if isinstance(clipped, LineString) else list(getattr(clipped, "geoms", []))
            for part in parts:
                if isinstance(part, LineString) and part.length > 0.05:
                    paths.append(list(part.coords))
    return paths, w


def write_isolation(paths, w, args, title, out):
    g = GCode(title + " - izolasyon frezeleme",
              args, f"V-uc {args.tip} mm uc, {args.angle} derece, derinlik {args.depth} mm -> kesim genisligi {w:.3f} mm")
    g.comment(f"{args.passes} gecis, adim {w * (1 - args.overlap):.3f} mm, ilerleme {args.feed} mm/dk")
    total = 0.0
    for p in order_paths(paths):
        g.goto(p[0][0], p[0][1])
        g.cut(z=-args.depth, feed=args.plunge)
        first = True
        for (x, y) in p[1:]:
            g.cut(x=x, y=y, feed=args.feed if first else None)
            first = False
        total += LineString(p).length
    g.end()
    g.save(out)
    return total


def write_drills(holes, args, title, outdir, prefix):
    files = []
    by_d = {}
    for x, y, d in holes:
        by_d.setdefault(d, []).append((x, y))
    for d, pts in sorted(by_d.items()):
        name = f"{prefix}_02_delik_{d:.1f}mm.nc"
        g = GCode(f"{title} - delik {d:.1f} mm ({len(pts)} adet)", args, f"{d:.1f} mm matkap ucu")
        g.comment(f"Delme derinligi {args.drill_depth} mm: 1.6 mm plaka + pay, altta feda plakasi olsun")
        for p in order_paths([[pt, pt] for pt in pts]):
            g.goto(p[0][0], p[0][1])
            g.cut(z=-args.drill_depth, feed=args.drill_feed)
        g.end()
        g.save(os.path.join(outdir, name))
        files.append((name, d, len(pts)))
    # Tek uçla delmek isteyenler için: tüm delikler 1.0 mm
    name = f"{prefix}_02_delik_HEPSI_1.0mm.nc"
    g = GCode(f"{title} - TUM delikler tek uc 1.0 mm ({len(holes)} adet)", args, "1.0 mm matkap ucu")
    for p in order_paths([[(x, y), (x, y)] for x, y, _ in holes]):
        g.goto(p[0][0], p[0][1])
        g.cut(z=-args.drill_depth, feed=args.drill_feed)
    g.end()
    g.save(os.path.join(outdir, name))
    files.append((name, 1.0, len(holes)))
    return files


def tab_intervals(length, n_tabs, tab_w):
    if n_tabs <= 0:
        return []
    return [((i + 0.5) * length / n_tabs - tab_w / 2, (i + 0.5) * length / n_tabs + tab_w / 2) for i in range(n_tabs)]


def write_cutout(board_m, npth_m, args, title, out, tool_d):
    g = GCode(f"{title} - kart kesimi (freze {tool_d} mm)", args,
              f"{tool_d} mm parmak freze, toplam derinlik {args.cut_depth} mm, paso {args.cut_step} mm")
    poly = board_m if isinstance(board_m, Polygon) else max(board_m.geoms, key=lambda p: p.area)
    outer = Polygon(poly.exterior.coords).buffer(tool_d / 2.0, resolution=24)
    rings = [("dis", list(outer.exterior.coords), True)]
    skipped = []
    # Vida delikleri: freze delikten küçükse çember üzerinde oyulur, eşitse dalınır
    for x, y, d in npth_m:
        if tool_d > d + 0.05:
            g.comment(f"UYARI: {d} mm vida deligi bu frezeden kucuk, ATLANDI. Matkapla acin")
            skipped.append(None)
            continue
        r = (d - tool_d) / 2.0
        if r < 0.05:
            rings.append(("vida", [(x, y), (x, y)], False))
        else:
            n = 24
            ring = [(x + r * math.cos(2 * math.pi * i / n), y + r * math.sin(2 * math.pi * i / n)) for i in range(n)]
            rings.append(("vida", ring + [ring[0]], False))
    for interior in poly.interiors:
        inner = Polygon(interior.coords).buffer(-tool_d / 2.0, resolution=24)
        if inner.is_empty or inner.area < 1e-3:
            # Yarık freze çapından dar: kesilirse yarık genişler ve yandaki izlere değebilir
            skipped.append(Polygon(interior.coords).bounds)
            continue
        for r in boundaries(inner):
            rings.append(("yarik", r, False))
    # İç kesimler önce (kart sabitken), dış hat sonra (sekmelerle)
    rings.sort(key=lambda r: r[0] == "dis")
    for b in skipped:
        if b is None:
            continue
        g.comment(f"UYARI: {b[2] - b[0]:.1f} x {b[3] - b[1]:.1f} mm yarik bu frezeye dar, ATLANDI. "
                  "Yarigi 1.0 mm freze dosyasiyla kesin veya elle acin")
    depths = []
    z = 0.0
    while z < args.cut_depth - 1e-6:
        z = min(z + args.cut_step, args.cut_depth)
        depths.append(z)
    tab_top = args.cut_depth - args.tab_height
    for kind, ring, tabs in rings:
        g.comment({"dis": "Dis hat, sekmeli", "yarik": "Ic kesim / yarik", "vida": "Vida deligi"}[kind])
        line = LineString(ring)
        L = line.length
        tabs_iv = tab_intervals(L, args.tabs, args.tab_width) if tabs else []
        g.goto(ring[0][0], ring[0][1])
        for d in depths:
            use_tabs = tabs and d > tab_top + 1e-6
            if not use_tabs:
                g.cut(z=-d, feed=args.cut_plunge)
                for (x, y) in ring[1:]:
                    g.cut(x=x, y=y, feed=args.cut_feed)
                if ring[0] != ring[-1]:  # açık yol (yarık): geri dön
                    for (x, y) in ring[-2::-1]:
                        g.cut(x=x, y=y, feed=args.cut_feed)
                continue
            # Sekmeli paso: yolu 0.2 mm adımlarla örnekle, sekme aralığında Z'yi yükselt
            n = max(2, int(L / 0.2))
            g.cut(z=-d, feed=args.cut_plunge)
            cur_z = -d
            for i in range(1, n + 1):
                s = L * i / n
                p = line.interpolate(s)
                in_tab = any(a <= s <= b for a, b in tabs_iv)
                tz = -tab_top if in_tab else -d
                if tz != cur_z:
                    g.cut(z=tz, feed=args.cut_plunge)
                    cur_z = tz
                g.cut(x=p.x, y=p.y, feed=args.cut_feed)
    g.end()
    g.save(out)
    return len(skipped)


def write_score(board_m, args, title, out):
    """Dış hattı V-uçla 0.3 mm kazır: freze yoksa kart elle (maket bıçağı/testere) kesilir."""
    g = GCode(f"{title} - dis hat cizgisi (V-uc ile)", args, "V-uc, derinlik 0.3 mm (elle kesim kilavuzu)")
    poly = board_m if isinstance(board_m, Polygon) else max(board_m.geoms, key=lambda p: p.area)
    for ring in boundaries(poly):
        g.goto(ring[0][0], ring[0][1])
        g.cut(z=-0.3, feed=args.plunge)
        for (x, y) in ring[1:]:
            g.cut(x=x, y=y, feed=args.feed)
    g.end()
    g.save(out)


# --------------------------------------------------------------- doğrulama
def verify(data, frame, paths, w):
    """Frezeleme sonrası kalan bakırı hesaplar; kısa devre ve kopukluk arar."""
    board_m = frame.geom(data["outline"])
    cuts = unary_union([LineString(p).buffer(w / 2.0, resolution=8) for p in paths])
    remaining = board_m.difference(cuts)
    comps = [remaining] if isinstance(remaining, Polygon) else list(remaining.geoms)
    nets_in = [set() for _ in comps]
    pad_comp = {}
    for ref, num, net, x, y in data["pads"]:
        pt = Point(*frame.pt(x, y))
        for i, c in enumerate(comps):
            if c.distance(pt) < 1e-6 or c.contains(pt):
                if net:
                    nets_in[i].add(net)
                pad_comp[(ref, num)] = (i, net)
                break
    shorts = [sorted(s) for s in nets_in if len(s) > 1]
    net_comps = {}
    for (ref, num), (i, net) in pad_comp.items():
        if net:
            net_comps.setdefault(net, set()).add(i)
    jumper_nets = {j[0] for j in data["jumpers"]}
    opens = {n: len(c) for n, c in net_comps.items() if len(c) > 1 and n not in jumper_nets}
    return shorts, opens


# --------------------------------------------------------------- önizleme
def preview(data, frame, iso_paths, holes_m, cut_tool, out, title):
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.patches import Circle

    copper = frame.geom(unary_union([g for _, g in data["items"]]))
    board_m = frame.geom(data["outline"])
    fig, ax = plt.subplots(figsize=(10, 10 * (board_m.bounds[3] + 6) / (board_m.bounds[2] + 6)))
    for p in [copper] if isinstance(copper, Polygon) else copper.geoms:
        xs, ys = p.exterior.xy
        ax.fill(xs, ys, color="#e8a35c", zorder=1)
        for r in p.interiors:
            xs, ys = r.xy
            ax.fill(xs, ys, color="white", zorder=1)
    for p in iso_paths:
        xs, ys = zip(*p)
        ax.plot(xs, ys, color="#1d4ed8", lw=0.5, zorder=2)
    for x, y, d in holes_m:
        ax.add_patch(Circle((x, y), d / 2, color="black", zorder=3))
    for ring in boundaries(board_m):
        xs, ys = zip(*ring)
        ax.plot(xs, ys, color="#dc2626", lw=1.2, zorder=4)
    ax.plot([0], [0], "k+", ms=14)
    ax.annotate("X0 Y0", (0, 0), (1, -2.2), fontsize=9)
    ax.set_aspect("equal")
    ax.set_xlim(-1, board_m.bounds[2] + MARGIN)
    ax.set_ylim(-3, board_m.bounds[3] + MARGIN)
    ax.set_title(title + "\nbakir yuz YUKARI (alttan gorunus, aynali) - mavi: izolasyon, siyah: delik, kirmizi: kesim",
                 fontsize=9)
    ax.grid(True, lw=0.3, alpha=0.5)
    fig.savefig(out, dpi=170, bbox_inches="tight")
    plt.close(fig)


# -------------------------------------------------------------------- ana akış
def run(key, args):
    path = os.path.join(ROOT, BOARDS[key])
    data = read_board(path)
    frame = Frame(data["outline"])
    outdir = os.path.join(ROOT, "pcb", "cnc", key)
    os.makedirs(outdir, exist_ok=True)
    prefix = key
    title = f"KlimaHub {key} karti"

    copper_m = frame.geom(unary_union([g for _, g in data["items"]]))
    board_m = frame.geom(data["outline"])
    iso_paths, w = isolation(copper_m, board_m, args)
    length = write_isolation(iso_paths, w, args, title, os.path.join(outdir, f"{prefix}_01_izolasyon.nc"))

    holes_m = [(*frame.pt(x, y), d) for x, y, d in data["holes"]]
    drills = write_drills(holes_m, args, title, outdir, prefix)
    npth_m = [(*frame.pt(x, y), d) for x, y, d in data["npth"]]
    for d in (1.0, 2.0, 3.175):
        if write_cutout(board_m, npth_m, args, title, os.path.join(outdir, f"{prefix}_03_kesim_freze_{d}mm.nc"), d):
            print(f"[{key}] UYARI: {d} mm freze yarık/vida deliğine sığmıyor, o dosyada atlandı (dosyadaki nota bakın)")
    write_score(board_m, args, title, os.path.join(outdir, f"{prefix}_03_kesim_ALTERNATIF_vuc_cizgi.nc"))
    preview(data, frame, iso_paths, holes_m, 2.0, os.path.join(outdir, f"{prefix}_onizleme_cnc.png"), title)

    shorts, opens = verify(data, frame, iso_paths, w)
    minx, miny, maxx, maxy = board_m.bounds
    print(f"[{key}] kart {maxx - minx + 0:.1f} x {maxy - miny:.1f} mm, plaka en az "
          f"{maxx + MARGIN:.0f} x {maxy + MARGIN:.0f} mm")
    print(f"[{key}] izolasyon: {len(iso_paths)} yol, {length / 1000:.2f} m, kesim genişliği {w:.3f} mm, "
          f"süre ≈ {length / args.feed:.0f} dk")
    for name, d, n in drills:
        print(f"[{key}] {name}: {n} delik")
    print(f"[{key}] köprü teli: {len(data['jumpers'])} parça")
    print(f"[{key}] doğrulama: kısa devre {len(shorts)}, kopukluk {len(opens)}")
    for s in shorts:
        print("    KISA DEVRE:", s)
    for n, c in opens.items():
        print("    KOPUK:", n, c)
    return len(shorts) + len(opens)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("board", nargs="?", default="all", choices=["hub", "sensor", "all"])
    ap.add_argument("--tip", type=float, default=0.2, help="V-uç uç çapı (mm)")
    ap.add_argument("--angle", type=float, default=30.0, help="V-uç açısı (derece)")
    ap.add_argument("--depth", type=float, default=0.10, help="izolasyon derinliği (mm)")
    ap.add_argument("--passes", type=int, default=3, help="izolasyon geçiş sayısı")
    ap.add_argument("--overlap", type=float, default=0.4, help="geçişler arası bindirme oranı")
    ap.add_argument("--feed", type=float, default=150.0, help="izolasyon ilerleme (mm/dk)")
    ap.add_argument("--plunge", type=float, default=50.0, help="dalış hızı (mm/dk)")
    ap.add_argument("--rpm", type=int, default=12000, help="iş mili devri (S)")
    ap.add_argument("--z-safe", dest="z_safe", type=float, default=1.5, help="hareket yüksekliği (mm)")
    ap.add_argument("--z-clear", dest="z_clear", type=float, default=8.0, help="başlangıç/bitiş yüksekliği")
    ap.add_argument("--drill-depth", dest="drill_depth", type=float, default=1.9)
    ap.add_argument("--drill-feed", dest="drill_feed", type=float, default=60.0)
    ap.add_argument("--cut-depth", dest="cut_depth", type=float, default=1.8)
    ap.add_argument("--cut-step", dest="cut_step", type=float, default=0.4)
    ap.add_argument("--cut-feed", dest="cut_feed", type=float, default=120.0)
    ap.add_argument("--cut-plunge", dest="cut_plunge", type=float, default=40.0)
    ap.add_argument("--tabs", type=int, default=4, help="dış hatta tutma sekmesi sayısı")
    ap.add_argument("--tab-width", dest="tab_width", type=float, default=2.5)
    ap.add_argument("--tab-height", dest="tab_height", type=float, default=0.5)
    args = ap.parse_args()
    keys = ["hub", "sensor"] if args.board == "all" else [args.board]
    bad = sum(run(k, args) for k in keys)
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
