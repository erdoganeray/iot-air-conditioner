#!/usr/bin/env python3
"""Web uygulaması duman testi: sahte hub'ı başlatır, uygulamayı başsız Chromium'da açar,
her sekmeyi gezer, temel etkileşimleri dener ve ekran görüntüsü alır.

    pip install aiohttp playwright
    python3 app/tools/ui_smoke_test.py [cikti_klasoru]

Konsolda JavaScript hatası olursa çıkış kodu 1 olur.
"""
import os
import subprocess
import sys
import time

from playwright.sync_api import sync_playwright

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "..", "docs", "images", "app")
PORT = 8765
CHROME = os.environ.get("CHROME_PATH", "/opt/pw-browsers/chromium")


def main():
    os.makedirs(OUT, exist_ok=True)
    server = subprocess.Popen([sys.executable, os.path.join(HERE, "mock_hub.py"), str(PORT)])
    errors = []
    try:
        time.sleep(1.5)
        with sync_playwright() as p:
            kw = {"executable_path": CHROME} if os.path.exists(CHROME) else {}
            browser = p.chromium.launch(**kw)
            page = browser.new_page(viewport={"width": 400, "height": 860}, device_scale_factor=2,
                                    color_scheme="light")
            page.on("console", lambda m: m.type == "error" and errors.append(m.text))
            page.on("pageerror", lambda e: errors.append(str(e)))
            page.goto(f"http://localhost:{PORT}/")

            # PIN diyaloğu
            page.wait_for_selector("#dlg[open]")
            page.fill("#f-pin", "1234")
            page.click("#dlg-ok")
            page.wait_for_selector("#tab-home:not(.hidden)")
            page.wait_for_timeout(800)
            page.screenshot(path=os.path.join(OUT, "01_ana.png"), full_page=True)

            # Klimayı aç, dereceyi arttır
            page.click("#ac-power")
            page.wait_for_timeout(500)
            page.click("[data-temp='1']")
            page.wait_for_timeout(1200)
            assert "25" in page.inner_text("#ac-temp"), page.inner_text("#ac-temp")
            page.click("#ac-modes [data-mode='dry']")
            page.wait_for_timeout(600)
            page.screenshot(path=os.path.join(OUT, "02_ana_acik.png"), full_page=True)

            # Grafik
            page.click("#tabbar [data-tab='chart']")
            page.wait_for_timeout(1500)
            page.screenshot(path=os.path.join(OUT, "03_grafik.png"), full_page=True)
            page.click("#chart-metric [data-metric='hum']")
            page.click("#chart-range [data-hours='168']")
            page.wait_for_timeout(1500)
            page.screenshot(path=os.path.join(OUT, "04_grafik_nem_7gun.png"), full_page=True)

            # Otomasyon: zamanlayıcı ekle ve kaydet
            page.click("#tabbar [data-tab='auto']")
            page.wait_for_timeout(800)
            page.click("#sch-add")
            page.click("#auto-save")
            page.wait_for_timeout(800)
            page.screenshot(path=os.path.join(OUT, "05_otomasyon.png"), full_page=True)

            # Kumanda: öğrenme akışı
            page.click("#tabbar [data-tab='ir']")
            page.wait_for_timeout(600)
            page.click("#ir-learn")
            page.wait_for_selector("#dlg[open]")
            page.screenshot(path=os.path.join(OUT, "06_ogrenme.png"))
            page.wait_for_selector("#dlg[open] >> text=Klima tanındı", timeout=6000)
            page.screenshot(path=os.path.join(OUT, "07_ogrenme_sonuc.png"))
            page.click("#dlg-ok")
            page.click("#ir-proto-btn")
            page.wait_for_timeout(800)
            page.screenshot(path=os.path.join(OUT, "08_kumanda.png"), full_page=True)

            # Ayarlar: WiFi taraması
            page.click("#tabbar [data-tab='settings']")
            page.wait_for_timeout(800)
            page.click("#wifi-scan")
            page.wait_for_selector("#wifi-scan-list >> text=Komsu_WiFi", timeout=5000)
            page.screenshot(path=os.path.join(OUT, "09_ayarlar.png"), full_page=True)

            # Karanlık tema
            dark = browser.new_page(viewport={"width": 400, "height": 860}, device_scale_factor=2,
                                    color_scheme="dark")
            dark.on("pageerror", lambda e: errors.append(str(e)))
            dark.goto(f"http://localhost:{PORT}/")
            dark.wait_for_selector("#dlg[open]")
            dark.fill("#f-pin", "1234")
            dark.click("#dlg-ok")
            dark.wait_for_selector("#tab-home:not(.hidden)")
            dark.wait_for_timeout(800)
            dark.screenshot(path=os.path.join(OUT, "10_ana_karanlik.png"), full_page=True)
            browser.close()
    finally:
        server.terminate()
    if errors:
        print("JavaScript hataları:")
        for e in errors:
            print("  ", e)
        sys.exit(1)
    print("Tüm adımlar başarılı. Ekran görüntüleri:", os.path.abspath(OUT))


if __name__ == "__main__":
    main()
