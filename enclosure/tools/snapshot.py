#!/usr/bin/env python3
"""3D görüntüleyiciden (enclosure/onizleme/*_3d.html) PNG çeker.

Kullanım: python3 snapshot.py <html?parametreler> <cikti.png> [<html?...> <cikti.png> ...]
Playwright ve Chromium gerekir (pip install playwright && playwright install chromium).
"""
import os
import sys
import time

from playwright.sync_api import sync_playwright

# Bazı ortamlarda Playwright'ın kendi tarayıcısı yerine sistemdeki Chromium kullanılır
CHROME = os.environ.get("CHROME_PATH", "/opt/pw-browsers/chromium")


def main(argv):
    pairs = list(zip(argv[0::2], argv[1::2]))
    with sync_playwright() as p:
        kw = {"executable_path": CHROME} if os.path.exists(CHROME) else {}
        b = p.chromium.launch(args=["--use-angle=swiftshader", "--enable-unsafe-swiftshader", "--ignore-gpu-blocklist"], **kw)
        page = b.new_page(viewport={"width": 1100, "height": 760}, device_scale_factor=1.5)
        for url, out in pairs:
            path, _, query = url.partition("?")
            page.goto("file://" + path + ("?" + query if query else ""))
            page.wait_for_function("window.__ready === true", timeout=60000)
            time.sleep(0.6)
            page.screenshot(path=out)
        b.close()


if __name__ == "__main__":
    main(sys.argv[1:])
