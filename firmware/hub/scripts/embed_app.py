# PlatformIO ön-derleme betiği: web uygulamasını (app/index.html) gzip ile
# sıkıştırıp hub yazılımına C dizisi olarak gömer.
# Çıktı: src/generated/app_html.h  (git'e eklenmez, her derlemede güncellenir)
import gzip
import os

Import("env")  # noqa: F821  (PlatformIO tarafından sağlanır)

project_dir = env.subst("$PROJECT_DIR")  # noqa: F821
app_path = os.path.normpath(os.path.join(project_dir, "..", "..", "app", "index.html"))
out_dir = os.path.join(project_dir, "src", "generated")
out_path = os.path.join(out_dir, "app_html.h")

PLACEHOLDER = b"""<!doctype html><meta charset=utf-8><title>KlimaHub</title>
<p>Web uygulamasi bulunamadi (app/index.html). Depoyu eksiksiz indirip yeniden derleyin.</p>"""


def needs_update():
    if not os.path.exists(out_path):
        return True
    if not os.path.exists(app_path):
        return False
    return os.path.getmtime(app_path) > os.path.getmtime(out_path)


def generate():
    if os.path.exists(app_path):
        with open(app_path, "rb") as f:
            raw = f.read()
    else:
        print("UYARI: %s yok, yer tutucu sayfa gömülüyor" % app_path)
        raw = PLACEHOLDER
    # mtime=0: aynı girdi her zaman aynı çıktıyı üretir
    data = gzip.compress(raw, compresslevel=9, mtime=0)
    os.makedirs(out_dir, exist_ok=True)
    lines = []
    for i in range(0, len(data), 20):
        lines.append("  " + ", ".join("0x%02x" % b for b in data[i : i + 20]) + ",")
    with open(out_path, "w") as f:
        f.write("// OTOMATIK URETILDI (scripts/embed_app.py) - elle duzenlemeyin\n")
        f.write("#pragma once\n#include <stddef.h>\n#include <stdint.h>\n\n")
        f.write("static const uint8_t kAppHtmlGz[] = {\n%s\n};\n" % "\n".join(lines))
        f.write("static const size_t kAppHtmlGzLen = %d;\n" % len(data))
        f.write("static const size_t kAppHtmlRawLen = %d;\n" % len(raw))
    print("Web uygulamasi gomuldu: %d bayt -> %d bayt (gzip)" % (len(raw), len(data)))


if needs_update():
    generate()
