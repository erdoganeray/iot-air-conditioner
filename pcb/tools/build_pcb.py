#!/usr/bin/env python3
"""Hub ve sensör PCB'lerini KiCad'de oluşturur, Freerouting ile tek yüzlü yönlendirir,
GND dolgusu ekler, DRC çalıştırır ve üretim dosyalarını (Gerber, Excellon, SVG) üretir.

Gereksinimler: KiCad 7+ (pcbnew Python modülü ve kicad-cli), Java 21, Freerouting 2.x
    python3 pcb/tools/build_pcb.py [hub|sensor|all] [--no-route]

Freerouting jar dosyası yoksa ~/.cache/klima/ altına indirilir.
"""
import os
import re
import shutil
import subprocess
import sys
import urllib.request

import pcbnew

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from boards import BOARDS, FP  # noqa: E402

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
PCB_DIR = os.path.join(ROOT, "pcb")
LOCAL_LIB = os.path.join(PCB_DIR, "lib", "klima.pretty")
FR_VERSION = "2.0.1"
FR_URL = f"https://github.com/freerouting/freerouting/releases/download/v{FR_VERSION}/freerouting-{FR_VERSION}.jar"

# CNC'ye uygun tasarım kuralları (mm)
TRACK = 0.6          # sinyal izi
TRACK_POWER = 0.8    # güç izleri
CLEARANCE = 0.45     # bakır-bakır boşluk (0.2 mm V-uç ≈ 0.25-0.3 mm keser + pay)
EDGE_CLEARANCE = 0.6
ZONE_CLEARANCE = 0.45
ZONE_MIN_WIDTH = 0.5
THERMAL_GAP = 0.5
THERMAL_SPOKE = 0.7

mm = pcbnew.FromMM


def V(x, y):
    return pcbnew.VECTOR2I(mm(x), mm(y))


def freerouting_jar():
    jar = os.environ.get("FREEROUTING_JAR")
    if jar and os.path.exists(jar):
        return jar
    cache = os.path.join(os.path.expanduser("~"), ".cache", "klima")
    jar = os.path.join(cache, f"freerouting-{FR_VERSION}.jar")
    if not os.path.exists(jar):
        os.makedirs(cache, exist_ok=True)
        print("Freerouting indiriliyor:", FR_URL)
        urllib.request.urlretrieve(FR_URL, jar)
    return jar


# ------------------------------------------------------------------ yardımcılar
def load_fp(lib, name):
    path = LOCAL_LIB if lib == "klima" else os.path.join(FP, lib + ".pretty")
    fp = pcbnew.FootprintLoad(path, name)
    if fp is None:
        raise RuntimeError(f"Footprint bulunamadı: {lib}:{name}")
    fp.SetFPID(pcbnew.LIB_ID(lib, name))
    return fp


def cnc_pads(fp, lib, name):
    """THT pedlerini elle yapılan (kaplamasız) kart için büyüt, delikleri standartlaştır."""
    for pad in fp.Pads():
        if pad.GetAttribute() != pcbnew.PAD_ATTRIB_PTH:
            continue
        if name.startswith("PinHeader") or name.startswith("XIAO"):
            pad.SetDrillSize(V(1.0, 1.0))
            pad.SetSize(V(1.9, 1.9))
        elif name.startswith("JST_PH"):
            pad.SetDrillSize(V(1.0, 1.0))
            pad.SetSize(V(1.4, 2.0))
        else:  # LED, TO-92, elektrolitik
            pad.SetDrillSize(V(0.8, 0.8))
            pad.SetSize(V(1.8, 1.8))


def add_outline(board, w, h, r, slots):
    def seg(x1, y1, x2, y2):
        s = pcbnew.PCB_SHAPE(board)
        s.SetShape(pcbnew.SHAPE_T_SEGMENT)
        s.SetStart(V(x1, y1))
        s.SetEnd(V(x2, y2))
        s.SetLayer(pcbnew.Edge_Cuts)
        s.SetWidth(mm(0.1))
        board.Add(s)

    def arc(cx, cy, sx, sy, angle):
        a = pcbnew.PCB_SHAPE(board)
        a.SetShape(pcbnew.SHAPE_T_ARC)
        a.SetCenter(V(cx, cy))
        a.SetStart(V(sx, sy))
        a.SetArcAngleAndEnd(pcbnew.EDA_ANGLE(angle, pcbnew.DEGREES_T), True)
        a.SetLayer(pcbnew.Edge_Cuts)
        a.SetWidth(mm(0.1))
        board.Add(a)

    seg(r, 0, w - r, 0)
    seg(w, r, w, h - r)
    seg(w - r, h, r, h)
    seg(0, h - r, 0, r)
    arc(w - r, r, w - r, 0, 90)
    arc(w - r, h - r, w, h - r, 90)
    arc(r, h - r, r, h, 90)
    arc(r, r, 0, r, 90)
    for (x1, y1, x2, y2) in slots:
        rr = (y2 - y1) / 2.0
        seg(x1 + rr, y1, x2 - rr, y1)
        seg(x2 - rr, y2, x1 + rr, y2)
        arc(x2 - rr, (y1 + y2) / 2, x2 - rr, y1, 180)
        arc(x1 + rr, (y1 + y2) / 2, x1 + rr, y2, 180)


def outline_poly(board):
    """Kart dış hattını (dolgu bölgesi için) nokta listesi olarak döndürür."""
    ps = pcbnew.SHAPE_POLY_SET()
    board.GetBoardPolygonOutlines(ps)
    o = ps.Outline(0)
    return [(o.CPoint(i).x, o.CPoint(i).y) for i in range(o.PointCount())]


def add_gnd_ring(board, gnd, w, h, inset, width=0.8):
    """Kart kenarı boyunca kilitli GND halkası: tek yüzde dolgunun bölünmesini önler."""
    c = inset + 1.5
    pts = [(c, inset), (w - c, inset), (w - inset, c), (w - inset, h - c), (w - c, h - inset),
           (c, h - inset), (inset, h - c), (inset, c), (c, inset)]
    for (x1, y1), (x2, y2) in zip(pts, pts[1:]):
        t = pcbnew.PCB_TRACK(board)
        t.SetStart(V(x1, y1))
        t.SetEnd(V(x2, y2))
        t.SetWidth(mm(width))
        t.SetLayer(pcbnew.B_Cu)
        t.SetNet(gnd)
        t.SetLocked(True)
        board.Add(t)


def add_rule_area_fcu(board, w, h):
    """F.Cu'ya iz/via yasağı: Freerouting yalnızca alt yüzü (B.Cu) kullanır."""
    z = pcbnew.ZONE(board)
    z.SetIsRuleArea(True)
    z.SetDoNotAllowTracks(True)
    z.SetDoNotAllowVias(True)
    z.SetDoNotAllowPads(False)
    z.SetDoNotAllowCopperPour(True)
    z.SetDoNotAllowFootprints(False)
    ls = pcbnew.LSET()
    ls.AddLayer(pcbnew.F_Cu)
    z.SetLayerSet(ls)
    z.SetZoneName("TEK_YUZ")
    o = z.Outline()
    o.NewOutline()
    for x, y in [(-2, -2), (w + 2, -2), (w + 2, h + 2), (-2, h + 2)]:
        o.Append(mm(x), mm(y))
    board.Add(z)
    return z


def add_gnd_zone(board, gnd, w, h):
    z = pcbnew.ZONE(board)
    z.SetLayer(pcbnew.B_Cu)
    z.SetNetCode(gnd.GetNetCode())
    z.SetZoneName("GND_DOLGU")
    z.SetLocalClearance(mm(ZONE_CLEARANCE))
    z.SetMinThickness(mm(ZONE_MIN_WIDTH))
    z.SetThermalReliefGap(mm(THERMAL_GAP))
    z.SetThermalReliefSpokeWidth(mm(THERMAL_SPOKE))
    z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
    z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
    o = z.Outline()
    o.NewOutline()
    for x, y in [(0, 0), (w, 0), (w, h), (0, h)]:
        o.Append(mm(x), mm(y))
    board.Add(z)
    return z


def set_rules(board):
    ds = board.GetDesignSettings()
    ds.m_MinClearance = mm(CLEARANCE)
    ds.m_TrackMinWidth = mm(0.5)
    ds.m_CopperEdgeClearance = mm(EDGE_CLEARANCE)
    ds.m_HoleClearance = mm(0.3)
    ds.m_MinThroughDrill = mm(0.6)
    ds.m_HoleToHoleMin = mm(0.5)
    nc = ds.m_NetSettings.m_DefaultNetClass
    nc.SetTrackWidth(mm(TRACK))
    nc.SetClearance(mm(CLEARANCE))
    nc.SetViaDiameter(mm(1.6))
    nc.SetViaDrill(mm(0.8))


# ------------------------------------------------------------------- oluşturma
def build_board(spec):
    board = pcbnew.BOARD()
    set_rules(board)
    nets = {}
    for name in spec["nets"]:
        n = pcbnew.NETINFO_ITEM(board, name)
        board.Add(n)
        nets[name] = n

    fps = {}
    for c in spec["components"]:
        lib, name = c["fp"]
        fp = load_fp(lib, name)
        fp.SetReference(c["ref"])
        fp.SetValue(c["value"])
        board.Add(fp)
        fp.SetPosition(V(c["x"], c["y"]))
        fp.SetOrientationDegrees(c["rot"])
        if c["side"] == "bottom":
            fp.Flip(fp.GetPosition(), False)
        cnc_pads(fp, lib, name)
        fps[c["ref"]] = fp

    for net, pins in spec["nets"].items():
        for p in pins:
            ref, num = p.split(".")
            pad = fps[ref].FindPadByNumber(num)
            if pad is None:
                raise RuntimeError(f"{ref} elemanında {num} numaralı ped yok")
            pad.SetNet(nets[net])

    add_outline(board, spec["width"], spec["height"], spec["corner"], spec["slots"])
    if spec.get("gnd_ring"):
        add_gnd_ring(board, nets["GND"], spec["width"], spec["height"], spec["gnd_ring"])
    add_rule_area_fcu(board, spec["width"], spec["height"])
    title = board.GetTitleBlock()
    title.SetTitle(spec["title"])
    title.SetCompany("github.com/erdoganeray/iot-air-conditioner")
    title.SetRevision("1.0")
    return board, nets


def patch_dsn(path, spec):
    """Güç ağları için daha geniş iz sınıfı ekler; dolguyla bağlanacak ağları çıkarır."""
    s = open(path).read()
    for n in spec.get("pour_only", []):
        s = re.sub(r"\(net %s\s*\(pins[^)]*\)\s*\)" % re.escape(n), "", s)
        s = re.sub(r'(\(class kicad_default ""[^(]*?)\s%s(?=\s)' % re.escape(n), r"\1", s)
    power = [n for n in spec["power_nets"] if n in spec["nets"] and n not in spec.get("pour_only", [])]
    m = re.search(r"\(class kicad_default \"\"(.*?)\(circuit", s, re.S)
    if not m or not power:
        open(path, "w").write(s)
        return
    names = m.group(1).split()
    rest = [n for n in names if n.strip('"') not in power]
    w_sig, w_pow, clr = int(TRACK * 1000), int(TRACK_POWER * 1000), int(CLEARANCE * 1000)
    new_cls = (
        f'(class kicad_default "" {" ".join(rest)}\n'
        f'      (circuit (use_via "Via[0-1]_1600:800_um"))\n'
        f"      (rule (width {w_sig}) (clearance {clr}))\n    )\n"
        f'    (class power "" {" ".join(power)}\n'
        f'      (circuit (use_via "Via[0-1]_1600:800_um"))\n'
        f"      (rule (width {w_pow}) (clearance {clr}))\n    )"
    )
    s = re.sub(r"\(class kicad_default \"\".*?\n    \)", new_cls, s, count=1, flags=re.S)
    open(path, "w").write(s)


def route(board, spec, workdir, stage="1"):
    dsn = os.path.join(workdir, f"{spec['name']}_{stage}.dsn")
    ses = os.path.join(workdir, f"{spec['name']}_{stage}.ses")
    if os.path.exists(ses):
        os.remove(ses)
    if not pcbnew.ExportSpecctraDSN(board, dsn):
        raise RuntimeError("DSN dışa aktarılamadı")
    patch_dsn(dsn, spec)
    cmd = ["java", "-jar", freerouting_jar(), "-de", dsn, "-do", ses, "-mp", "100", "--gui.enabled=false"]
    print("Freerouting çalışıyor…")
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=1800)
    log = r.stdout + r.stderr
    for line in log.splitlines():
        if "unrouted" in line.lower() or "ERROR" in line:
            print("  ", line.split("] ")[-1][:160])
    if not os.path.exists(ses):
        print(log[-3000:])
        raise RuntimeError("Freerouting SES üretmedi")
    import_ses(board, ses)


def parse_sexpr(text):
    """Basit s-ifadesi ayrıştırıcı (SES dosyası için)."""
    tokens = re.findall(r'\(|\)|"[^"]*"|[^\s()]+', text)
    stack, cur = [], []
    for t in tokens:
        if t == "(":
            stack.append(cur)
            cur = []
        elif t == ")":
            done = cur
            cur = stack.pop()
            cur.append(done)
        else:
            cur.append(t.strip('"'))
    return cur[0]


def find_all(node, key):
    if isinstance(node, list):
        if node and node[0] == key:
            yield node
        for c in node:
            yield from find_all(c, key)


def import_ses(board, ses_path):
    """Freerouting SES dosyasındaki izleri karta ekler (KiCad 7 betik API'sinde
    ImportSpecctraSES yalnızca açık editörde çalıştığı için)."""
    tree = parse_sexpr(open(ses_path).read())
    res = 10.0  # (resolution um 10)
    for r in find_all(tree, "resolution"):
        res = float(r[2])
    to_mm = lambda v: float(v) / res / 1000.0  # noqa: E731
    routes = next(find_all(tree, "network_out"), [])
    n_seg = 0
    for net in find_all(routes, "net"):
        name = net[1]
        ni = board.FindNet(name)
        for wire in find_all(net, "wire"):
            for path in find_all(wire, "path"):
                layer = pcbnew.B_Cu if path[1] == "B.Cu" else pcbnew.F_Cu
                width = to_mm(path[2])
                pts = [(to_mm(path[i]), -to_mm(path[i + 1])) for i in range(3, len(path) - 1, 2)]
                for (x1, y1), (x2, y2) in zip(pts, pts[1:]):
                    t = pcbnew.PCB_TRACK(board)
                    t.SetStart(V(x1, y1))
                    t.SetEnd(V(x2, y2))
                    t.SetWidth(mm(width))
                    t.SetLayer(layer)
                    t.SetNet(ni)
                    board.Add(t)
                    n_seg += 1
        for via in find_all(net, "via"):
            v = pcbnew.PCB_VIA(board)
            v.SetViaType(pcbnew.VIATYPE_THROUGH)
            v.SetPosition(V(to_mm(via[2]), -to_mm(via[3])))
            v.SetWidth(mm(1.8))
            v.SetDrill(mm(0.8))
            v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
            v.SetNet(ni)
            board.Add(v)
    print(f"  {n_seg} iz parçası eklendi")


def unconnected(board):
    board.BuildConnectivity()
    return board.GetConnectivity().GetUnconnectedCount(False)


def write_project(path):
    # KiCad proje dosyası (SaveBoard da oluşturur; kütüphane tablosunu ekle)
    d = os.path.dirname(path)
    with open(os.path.join(d, "fp-lib-table"), "w") as f:
        f.write('(fp_lib_table\n  (version 7)\n'
                '  (lib (name "klima")(type "KiCad")(uri "${KIPRJMOD}/../lib/klima.pretty")(options "")'
                '(descr "Klima projesi footprintleri"))\n)\n')


def export_outputs(pcb_path, spec, outdir):
    os.makedirs(outdir, exist_ok=True)
    gerb = os.path.join(outdir, "gerber")
    if os.path.exists(gerb):
        shutil.rmtree(gerb)
    os.makedirs(gerb)
    run = lambda *a: subprocess.run(list(a), check=True, capture_output=True, text=True)  # noqa: E731
    run("kicad-cli", "pcb", "export", "gerbers", "--output", gerb + "/",
        "--layers", "B.Cu,Edge.Cuts,F.SilkS,F.Fab,B.Fab", "--no-protel-ext", pcb_path)
    run("kicad-cli", "pcb", "export", "drill", "--output", gerb + "/", "--format", "excellon",
        "--drill-origin", "absolute", "--excellon-units", "mm", "--excellon-separate-th",
        "--generate-map", "--map-format", "pdf", pcb_path)
    # Görseller: üst görünüm (yerleşim) ve alt bakır (aynalı, CNC'de görüneceği gibi)
    svg_top = os.path.join(outdir, "yerlesim_ust.svg")
    svg_bot = os.path.join(outdir, "bakir_alt_aynali.svg")
    run("kicad-cli", "pcb", "export", "svg", "--output", svg_top, "--layers",
        "Edge.Cuts,F.Fab,F.SilkS,B.Cu", "--page-size-mode", "2", "--exclude-drawing-sheet", pcb_path)
    run("kicad-cli", "pcb", "export", "svg", "--output", svg_bot, "--layers", "Edge.Cuts,B.Cu,B.Fab",
        "--mirror", "--page-size-mode", "2", "--exclude-drawing-sheet", pcb_path)
    try:
        import cairosvg

        for svg in (svg_top, svg_bot):
            cairosvg.svg2png(url=svg, write_to=svg[:-4] + ".png", output_width=1400, background_color="white")
    except Exception as e:  # noqa: BLE001
        print("PNG dönüştürülemedi:", e)


def placement_check(board, spec):
    """Yönlendirmeden önce: farklı ağlara ait pedler arası boşluk ve avlu (courtyard) çakışmaları."""
    from shapely.geometry import Polygon

    items = []
    for fp in board.GetFootprints():
        for pad in fp.Pads():
            ps = pad.GetEffectivePolygon()
            o = ps.Outline(0)
            pts = [(pcbnew.ToMM(o.CPoint(i).x), pcbnew.ToMM(o.CPoint(i).y)) for i in range(o.PointCount())]
            layers = "B" if pad.IsOnLayer(pcbnew.B_Cu) else "F"
            items.append((fp.GetReference() + "." + pad.GetNumber(), pad.GetNetname(), layers, Polygon(pts)))
    bad = []
    for i in range(len(items)):
        for j in range(i + 1, len(items)):
            a, b = items[i], items[j]
            if a[2] != b[2] or "B" not in a[2]:
                continue
            if a[1] and a[1] == b[1]:
                continue
            d = a[3].distance(b[3])
            if d < CLEARANCE - 1e-3:
                bad.append(f"{a[0]}({a[1]}) <-> {b[0]}({b[1]}): {d:.2f} mm")
    # Kart kenarına yakınlık
    w, h = spec["width"], spec["height"]
    edge = Polygon([(0, 0), (w, 0), (w, h), (0, h)]).exterior
    for name, net, lay, poly in items:
        if "B" in lay and poly.distance(edge) < EDGE_CLEARANCE - 1e-3:
            bad.append(f"{name}({net}) kenara {poly.distance(edge):.2f} mm")
    return bad


def build(key, do_route=True):
    spec = BOARDS[key]
    bdir = os.path.join(PCB_DIR, key)
    os.makedirs(bdir, exist_ok=True)
    work = os.path.join(bdir, "_work")
    os.makedirs(work, exist_ok=True)
    board, nets = build_board(spec)
    pcb_path = os.path.join(bdir, spec["name"] + ".kicad_pcb")
    bad = placement_check(board, spec)
    if bad:
        print(f"[{key}] YERLEŞİM SORUNLARI:")
        for b in bad:
            print("   ", b)
    if do_route:
        route(board, spec, work, "1")
        n1 = unconnected(board)
        print(f"[{key}] 1. aşama (yalnız alt yüz) sonrası eksik bağlantı: {n1}")
        if n1:
            # 2. aşama: kalan bağlantılar için üst yüzde köprü teli (F.Cu iz = tel)
            for t in board.GetTracks():
                t.SetLocked(True)
            for z in list(board.Zones()):
                if z.GetIsRuleArea() and z.GetZoneName() == "TEK_YUZ":
                    board.Remove(z)
            route(board, spec, work, "2")
            print(f"[{key}] 2. aşama (köprü telleri) sonrası eksik bağlantı: {unconnected(board)}")
    # Bellekte oluşturulan kartta dolgu motoru çöker: kaydet, yeniden yükle, sonra doldur
    pcbnew.SaveBoard(pcb_path, board)
    write_project(pcb_path)
    board = pcbnew.LoadBoard(pcb_path)
    gnd = board.FindNet("GND")
    if gnd and gnd.GetNetCode() > 0:
        add_gnd_zone(board, gnd, spec["width"], spec["height"])
        board.BuildConnectivity()
        pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    n_unc = unconnected(board)
    pcbnew.SaveBoard(pcb_path, board)
    drc = os.path.join(bdir, "drc_raporu.txt")
    pcbnew.WriteDRCReport(board, drc, pcbnew.EDA_UNITS_MILLIMETRES, True)
    print(f"[{key}] bağlanmamış bağlantı: {n_unc}")
    print(f"[{key}] DRC özeti:")
    for line in open(drc):
        if line.startswith("**") or line.startswith("["):
            print("   ", line.rstrip()[:150])
    export_outputs(pcb_path, spec, os.path.join(PCB_DIR, "cnc", key))
    return n_unc


if __name__ == "__main__":
    which = sys.argv[1] if len(sys.argv) > 1 and not sys.argv[1].startswith("--") else "all"
    do_route = "--no-route" not in sys.argv
    keys = list(BOARDS) if which == "all" else [which]
    bad = 0
    for k in keys:
        bad += build(k, do_route)
    sys.exit(1 if bad else 0)
