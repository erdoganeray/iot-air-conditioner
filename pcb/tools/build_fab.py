#!/usr/bin/env python3
"""Fabrikada ürettirilecek (JLCPCB, PCBWay vb.) hub ve sensör kartlarını oluşturur.

CNC sürümüyle (build_pcb.py) aynı kart ölçüsü, aynı eleman yerleşimi ve aynı pinler
kullanılır; böylece kutular ve yazılım değişmeden çalışır. Farklar:
  - 2 katman (üst + alt bakır), metal kaplı delikler, vialar
  - ince izler ve küçük boşluklar (fabrika standart kuralları)
  - iki yüzde GND dolgusu ve dikiş vialar
  - lehim maskesi ve yön işaretli serigrafi (+, pin adları, kart adı)

    python3 pcb/tools/build_fab.py [hub|sensor|all]

Çıktılar: pcb/fab/<kart>/ (KiCad dosyaları, DRC raporu) ve
          pcb/fab/uretim/<kart>/ (Gerber + delik dosyaları, sipariş için zip, görseller)
"""
import os
import re
import shutil
import subprocess
import sys
import zipfile

import pcbnew

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build_pcb as B  # noqa: E402
from boards import BOARDS  # noqa: E402

FAB_DIR = os.path.join(B.PCB_DIR, "fab")

# Fabrika kuralları (mm). JLCPCB standart 2 katman sınırlarının rahatça üstünde.
TRACK = 0.3
TRACK_POWER = 0.6
CLEARANCE = 0.2
EDGE_CLEARANCE = 0.4
VIA_D, VIA_DRILL = 0.7, 0.35
ZONE_CLEARANCE = 0.3
ZONE_MIN_WIDTH = 0.25
THERMAL_GAP = 0.3
THERMAL_SPOKE = 0.5
STITCH_PITCH = 3.0

mm = pcbnew.FromMM
V = B.V

# Serigrafi yazıları: (metin, x, y, boy, yüz, hizalama)  — yüz: "F" üst, "B" alt
SILK = {
    "hub": [
        ("KlimaHub HUB v1.0", 31.0, 14.5, 1.2, "F", "C"),
        ("ON: KLIMAYA BAKAR", 31.0, 16.5, 0.8, "F", "C"),
        ("+", 3.0, 34.6, 1.2, "F", "C"),        # J1 pil +
        ("BAT", 5.5, 41.0, 0.9, "F", "C"),
        ("+", 10.5, 17.2, 1.2, "F", "C"),       # J2 XBAT +
        ("OUT", 33.08, 6.3, 0.8, "F", "C"),     # VS1838B çıkış
        ("TOUCH", 47.6, 23.0, 0.8, "F", "R"),
        ("SDA", 47.6, 25.54, 0.8, "F", "R"),
        ("SCL", 47.6, 28.08, 0.8, "F", "R"),
        ("GND", 47.6, 30.62, 0.8, "F", "R"),
        ("GND", 47.6, 33.16, 0.8, "F", "R"),
        ("3V3", 47.6, 35.7, 0.8, "F", "R"),
        ("3V3", 47.6, 38.24, 0.8, "F", "R"),
        ("KlimaHub HUB v1.0", 29.0, 26.0, 1.0, "B", "C"),
    ],
    "sensor": [
        ("KlimaHub SENSOR v1.0", 12.5, 9.2, 1.0, "F", "C"),
        ("GND", 8.19, 7.4, 0.8, "F", "C"),
        ("VIN", 10.73, 7.4, 0.8, "F", "C"),
        ("SDA", 13.27, 7.4, 0.8, "F", "C"),
        ("SCL", 15.81, 7.4, 0.8, "F", "C"),
        ("+", 5.6, 18.2, 1.2, "F", "C"),        # J1 pil +
        ("+", 14.0, 16.9, 1.2, "F", "C"),       # J2 XBAT +
        ("SENSOR v1.0", 12.5, 30.0, 1.0, "B", "C"),
    ],
}

XIAO_LABELS = {str(i + 1): f"D{i}" for i in range(7)}
XIAO_LABELS.update({"8": "D7", "9": "D8", "10": "D9", "11": "D10", "12": "3V3", "13": "GND", "14": "5V"})


def set_rules(board):
    ds = board.GetDesignSettings()
    ds.m_MinClearance = mm(CLEARANCE)
    ds.m_TrackMinWidth = mm(0.2)
    ds.m_CopperEdgeClearance = mm(EDGE_CLEARANCE)
    ds.m_HoleClearance = mm(0.25)
    ds.m_MinThroughDrill = mm(0.3)
    ds.m_HoleToHoleMin = mm(0.3)
    ds.m_ViasMinSize = mm(0.6)
    ds.m_ViasMinAnnularWidth = mm(0.15)
    ds.m_SilkClearance = mm(0.1)
    nc = ds.m_NetSettings.m_DefaultNetClass
    nc.SetTrackWidth(mm(TRACK))
    nc.SetClearance(mm(CLEARANCE))
    nc.SetViaDiameter(mm(VIA_D))
    nc.SetViaDrill(mm(VIA_DRILL))


def silk_text(board, text, x, y, h, side, align):
    t = pcbnew.PCB_TEXT(board)
    t.SetText(text)
    t.SetPosition(V(x, y))
    t.SetTextSize(pcbnew.VECTOR2I(mm(h), mm(h)))
    t.SetTextThickness(mm(max(0.15, h * 0.15)))
    t.SetLayer(pcbnew.F_SilkS if side == "F" else pcbnew.B_SilkS)
    if side == "B":
        t.SetMirrored(True)
    t.SetHorizJustify({"L": pcbnew.GR_TEXT_H_ALIGN_LEFT, "C": pcbnew.GR_TEXT_H_ALIGN_CENTER,
                       "R": pcbnew.GR_TEXT_H_ALIGN_RIGHT}[align])
    board.Add(t)


def build_board(key):
    spec = BOARDS[key]
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
        fp = B.load_fp(lib, name)
        fp.SetReference(c["ref"])
        fp.SetValue(c["value"])
        board.Add(fp)
        fp.SetPosition(V(c["x"], c["y"]))
        fp.SetOrientationDegrees(c["rot"])
        if c["side"] == "bottom":
            fp.Flip(fp.GetPosition(), False)
        ref = fp.Reference()
        ref.SetTextSize(pcbnew.VECTOR2I(mm(0.9), mm(0.9)))
        ref.SetTextThickness(mm(0.15))
        if c["ref"] in ("U1", "H1"):  # kart dışına düşüyor; XIAO pin adları zaten yazılı
            ref.SetVisible(False)
        fps[c["ref"]] = fp
    for net, pins in spec["nets"].items():
        for p in pins:
            r, num = p.split(".")
            fps[r].FindPadByNumber(num).SetNet(nets[net])
    B.add_outline(board, spec["width"], spec["height"], spec["corner"], spec["slots"])
    for (x1, y1, x2, y2) in spec["slots"]:
        B.add_slot_keepout(board, x1, y1, x2, y2, 0.3)
    # Serigrafi: kart yazıları, konnektör işaretleri, XIAO pin adları
    for args in SILK[key]:
        silk_text(board, *args)
    u1 = fps["U1"]
    ux = pcbnew.ToMM(u1.GetPosition().x)
    for pad in u1.Pads():
        px, py = pcbnew.ToMM(pad.GetPosition().x), pcbnew.ToMM(pad.GetPosition().y)
        outside = -1 if px < ux else 1
        silk_text(board, XIAO_LABELS[pad.GetNumber()], px + outside * 1.35, py, 0.8, "F", "L" if outside > 0 else "R")
    title = board.GetTitleBlock()
    title.SetTitle(spec["title"] + " - fabrika uretimi")
    title.SetCompany("github.com/erdoganeray/iot-air-conditioner")
    title.SetRevision("1.0")
    return board, nets, spec


def patch_dsn(path, spec):
    """Sinyal ve güç iz sınıfları; via adı DSN'deki padstack'ten alınır."""
    s = open(path).read()
    via = re.search(r'\(padstack "(Via\[0-1\]_[^"]+)"', s)
    via_name = via.group(1) if via else "Via[0-1]_700:350_um"
    m = re.search(r"\(class kicad_default \"\"(.*?)\(circuit", s, re.S)
    names = m.group(1).split()
    power = [n for n in spec["power_nets"] if n in spec["nets"]]
    rest = [n for n in names if n.strip('"') not in power]
    w_sig, w_pow, clr = int(TRACK * 1000), int(TRACK_POWER * 1000), int(CLEARANCE * 1000)
    new_cls = (
        f'(class kicad_default "" {" ".join(rest)}\n'
        f'      (circuit (use_via "{via_name}"))\n'
        f"      (rule (width {w_sig}) (clearance {clr}))\n    )\n"
        f'    (class power "" {" ".join(power)}\n'
        f'      (circuit (use_via "{via_name}"))\n'
        f"      (rule (width {w_pow}) (clearance {clr}))\n    )"
    )
    s = re.sub(r"\(class kicad_default \"\".*?\n    \)", new_cls, s, count=1, flags=re.S)
    open(path, "w").write(s)


def route(board, spec, workdir):
    dsn = os.path.join(workdir, f"{spec['name']}_fab.dsn")
    ses = os.path.join(workdir, f"{spec['name']}_fab.ses")
    if os.path.exists(ses):
        os.remove(ses)
    if not pcbnew.ExportSpecctraDSN(board, dsn):
        raise RuntimeError("DSN dışa aktarılamadı")
    patch_dsn(dsn, spec)
    cmd = ["java", "-jar", B.freerouting_jar(), "-de", dsn, "-do", ses, "-mp", "100", "--gui.enabled=false"]
    print("Freerouting çalışıyor…")
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=1800)
    if not os.path.exists(ses) or os.path.getsize(ses) == 0:
        print((r.stdout + r.stderr)[-3000:])
        raise RuntimeError("Freerouting SES üretmedi")
    B.import_ses(board, ses, VIA_D, VIA_DRILL)


def add_zone(board, gnd, layer, w, h):
    z = pcbnew.ZONE(board)
    z.SetLayer(layer)
    z.SetNetCode(gnd.GetNetCode())
    z.SetZoneName("GND_" + ("UST" if layer == pcbnew.F_Cu else "ALT"))
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


def filled(board, layer):
    from cnc import polyset_to_shapely
    from shapely.ops import unary_union

    polys = [polyset_to_shapely(z.GetFilledPolysList(layer)) for z in board.Zones()
             if not z.GetIsRuleArea() and z.IsOnLayer(layer)]
    return unary_union(polys)


def add_stitching(board, gnd, w, h):
    """Üst ve alt GND dolgusunun ikisinin de bulunduğu yerlere dikiş viası koyar."""
    from shapely.geometry import Point

    both = filled(board, pcbnew.F_Cu).intersection(filled(board, pcbnew.B_Cu))
    safe = both.buffer(-(VIA_D / 2 + 0.05))
    n = 0
    y = STITCH_PITCH / 2
    while y < h:
        x = STITCH_PITCH / 2
        while x < w:
            if safe.contains(Point(x, y)):
                v = pcbnew.PCB_VIA(board)
                v.SetViaType(pcbnew.VIATYPE_THROUGH)
                v.SetPosition(V(x, y))
                v.SetWidth(mm(VIA_D))
                v.SetDrill(mm(VIA_DRILL))
                v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
                v.SetNet(gnd)
                board.Add(v)
                n += 1
            x += STITCH_PITCH
        y += STITCH_PITCH
    return n


def export(pcb_path, spec, key):
    out = os.path.join(FAB_DIR, "uretim", key)
    gerb = os.path.join(out, "gerber")
    if os.path.exists(out):
        shutil.rmtree(out)
    os.makedirs(gerb)
    run = lambda *a: subprocess.run(list(a), check=True, capture_output=True, text=True)  # noqa: E731
    run("kicad-cli", "pcb", "export", "gerbers", "--output", gerb + "/", "--layers",
        "F.Cu,B.Cu,F.Paste,B.Paste,F.SilkS,B.SilkS,F.Mask,B.Mask,Edge.Cuts", "--subtract-soldermask", pcb_path)
    run("kicad-cli", "pcb", "export", "drill", "--output", gerb + "/", "--format", "excellon",
        "--drill-origin", "absolute", "--excellon-units", "mm", "--excellon-separate-th", pcb_path)
    zpath = os.path.join(out, f"{spec['name']}-gerber.zip")
    with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED) as zf:
        for f in sorted(os.listdir(gerb)):
            zf.write(os.path.join(gerb, f), f)
    # Görseller
    svgs = []
    for name, layers, mirror in (("ust", "Edge.Cuts,F.Cu,F.Mask,F.SilkS", False),
                                 ("alt", "Edge.Cuts,B.Cu,B.Mask,B.SilkS", True)):
        svg = os.path.join(out, f"{key}_{name}.svg")
        args = ["kicad-cli", "pcb", "export", "svg", "--output", svg, "--layers", layers,
                "--page-size-mode", "2", "--exclude-drawing-sheet"]
        if mirror:
            args.append("--mirror")
        run(*args, pcb_path)
        svgs.append(svg)
    try:
        import cairosvg

        for svg in svgs:
            cairosvg.svg2png(url=svg, write_to=svg[:-4] + ".png", output_width=1400, background_color="white")
            os.remove(svg)
    except Exception as e:  # noqa: BLE001
        print("PNG dönüştürülemedi:", e)
    return zpath


def drc_summary(board, path):
    pcbnew.WriteDRCReport(board, path, pcbnew.EDA_UNITS_MILLIMETRES, True)
    cats = {}
    unconnected = 0
    for line in open(path):
        m = re.match(r"\[(\w+)\]", line)
        if m:
            cats[m.group(1)] = cats.get(m.group(1), 0) + 1
        m = re.search(r"Found (\d+) unconnected", line)
        if m:
            unconnected = int(m.group(1))
    return cats, unconnected


# Üretimi engellemeyen uyarılar (serigrafi kırpılır, kütüphane tablosu projeye özgü)
HARMLESS = {"silk_overlap", "silk_over_copper", "silk_edge_clearance", "lib_footprint_issues",
            "lib_footprint_mismatch", "text_height", "text_thickness"}


def build(key):
    board, nets, spec = build_board(key)
    bdir = os.path.join(FAB_DIR, key)
    work = os.path.join(bdir, "_work")
    os.makedirs(work, exist_ok=True)
    route(board, spec, work)
    pcb_path = os.path.join(bdir, spec["name"] + "-fab.kicad_pcb")
    pcbnew.SaveBoard(pcb_path, board)
    with open(os.path.join(bdir, "fp-lib-table"), "w") as f:
        f.write('(fp_lib_table\n  (version 7)\n'
                '  (lib (name "klima")(type "KiCad")(uri "${KIPRJMOD}/../../lib/klima.pretty")(options "")'
                '(descr "Klima projesi footprintleri"))\n)\n')
    # Dolgu motoru bellekte oluşturulan kartta çöktüğü için: kaydet, yükle, doldur
    board = pcbnew.LoadBoard(pcb_path)
    gnd = board.FindNet("GND")
    for layer in (pcbnew.F_Cu, pcbnew.B_Cu):
        add_zone(board, gnd, layer, spec["width"], spec["height"])
    board.BuildConnectivity()
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    n_via = add_stitching(board, gnd, spec["width"], spec["height"])
    board.BuildConnectivity()
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    board.BuildConnectivity()
    pcbnew.SaveBoard(pcb_path, board)
    cats, unc = drc_summary(board, os.path.join(bdir, "drc_raporu.txt"))
    errors = {k: v for k, v in cats.items() if k not in HARMLESS}
    print(f"[{key}] dikiş viası: {n_via}, bağlanmamış: {unc}, DRC: {cats}")
    if errors or unc:
        print(f"[{key}] HATA: {errors} bağlanmamış={unc}")
    z = export(pcb_path, spec, key)
    print(f"[{key}] sipariş dosyası: {os.path.relpath(z, B.ROOT)}")
    return len(errors) + unc


if __name__ == "__main__":
    which = sys.argv[1] if len(sys.argv) > 1 else "all"
    keys = list(BOARDS) if which == "all" else [which]
    sys.exit(1 if sum(build(k) for k in keys) else 0)
