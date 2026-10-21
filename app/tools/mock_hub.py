#!/usr/bin/env python3
"""Donanım olmadan web uygulamasını denemek için sahte (mock) hub.

Gerçek hub'ın WebSocket JSON API'sini taklit eder (docs/API.md).

Kullanım:
    pip install aiohttp
    python3 app/tools/mock_hub.py          # http://localhost:8080  (PIN: 1234)
"""
import asyncio
import json
import math
import os
import random
import sys
import time

from aiohttp import web

APP_DIR = os.path.normpath(os.path.join(os.path.dirname(__file__), ".."))
PIN = "1234"
START = time.time()

state = {
    "ac": {"power": False, "mode": "cool", "temp": 24.0, "fan": "auto", "swing": False},
    "ac_src": "app",
    "ac_changed": int(time.time()) - 3600,
    "ir": {"mode": "protocol", "protocol": "FUJITSU_AC", "model": 1, "model_name": "ARRAH2E",
           "commands": 2, "ready": True, "learning": False},
    "cmds": [{"id": 1, "name": "Kapat", "role": "off", "len": 115},
             {"id": 2, "name": "Turbo", "role": "", "len": 263}],
    "sensors": [
        {"id": 0, "kind": "hub", "name": "Hub", "enabled": True, "online": True, "temp": 24.6, "hum": 55.1,
         "age_s": 12, "t_off": -0.8, "h_off": 0},
        {"id": 1, "kind": "sensor", "name": "Salon", "mac": "34:85:18:AA:BB:01", "online": True, "temp": 25.3,
         "hum": 61.0, "age_s": 140, "seen_s": 140, "batt_mv": 3950, "batt_pct": 72, "usb": False,
         "low_batt": False, "sensor_error": False, "rssi": -67, "fw": "1.0", "boots": 512, "reports": 480,
         "interval_now": 600, "interval_min": 10, "t_off": 0, "h_off": 0},
        {"id": 2, "kind": "sensor", "name": "Yatak odası", "mac": "34:85:18:AA:BB:02", "online": False,
         "seen_s": 5400, "batt_mv": 3560, "batt_pct": 6, "usb": False, "low_batt": True,
         "sensor_error": False, "rssi": -81, "fw": "1.0", "boots": 300, "reports": 290,
         "interval_now": 600, "interval_min": 10, "t_off": 0.3, "h_off": -2},
    ],
    "auto": {
        "thermo": {"enabled": True, "source": 1, "mode": "cool", "target": 25.0, "hyst": 0.5,
                   "ac_temp": 24.0, "fan": "auto"},
        "dry": {"enabled": False, "source": 0, "on": 65, "off": 55, "ac_temp": 24.0},
        "timing": {"min_on_min": 5, "min_off_min": 5, "override_min": 60},
        "schedules": [{"enabled": True, "days": 31, "time": "07:00", "action": "on", "mode": "cool", "temp": 24},
                      {"enabled": True, "days": 127, "time": "23:30", "action": "off", "mode": "cool", "temp": 24}],
    },
    "settings": {"name": "KlimaHub", "tz": "<+03>-3", "ble_always": True, "auto_accept": True,
                 "led": {"brightness": 40, "night": True, "night_start": "23:00", "night_end": "07:00"},
                 "ac_default": {"mode": "cool", "temp": 24, "fan": "auto", "swing": False},
                 "hub_sensor": {"enabled": True, "name": "Hub", "t_off": -0.8, "h_off": 0}},
    "telegram": {"enabled": True, "has_token": True, "chats": [123456789],
                 "notify": {"battery": True, "offline": True, "power": True, "wifi": True, "auto": False},
                 "insecure": False},
    "wifi": [{"ssid": "EvAgi_2.4G", "channel": 6}],
}
clients = set()
authed = set()


def status():
    a = state["auto"]
    names = {s["id"]: s["name"] for s in state["sensors"]}
    names[255] = "Ortalama"
    return {
        "hub": {
            "name": state["settings"]["name"], "fw": "1.0.0", "uptime": int(time.time() - START) + 86000,
            "time": int(time.time()), "time_valid": True, "time_src": "ntp",
            "wifi": {"state": "connected", "ssid": "EvAgi_2.4G", "configured": 1, "rssi": -58,
                     "ip": "192.168.1.50", "ch": 6, "outage_s": 0, "reconnects": 3, "retry_in": 0,
                     "internet": True},
            "ap": {"on": False, "ssid": "KlimaHub-A1B2"},
            "ble": {"on": True, "clients": 0, "always": True},
            "power": {"usb": True, "battery": True, "batt_mv": 4150, "batt_pct": 95, "low": False},
            "telegram": {"enabled": True, "ok": True, "error": "", "chats": 1, "queued": 0},
        },
        "sensors": state["sensors"],
        "ac": {"state": state["ac"], "src": state["ac_src"], "changed": state["ac_changed"], "ir": state["ir"]},
        "auto": {
            "thermo": {**{k: a["thermo"][k] for k in ("enabled", "mode", "target", "hyst", "source")},
                       "source_name": names.get(a["thermo"]["source"], "?"), "want": "keep", "value": 25.3},
            "dry": {**{k: a["dry"][k] for k in ("enabled", "on", "off", "source")},
                    "source_name": names.get(a["dry"]["source"], "?"), "want": "keep", "value": 55.1},
            "owner": "none", "deferred": False, "override_s": 0, "schedules": len(a["schedules"]),
        },
    }


def history(src, hours, step_min):
    now = int(time.time())
    step = max(300, step_min * 60)
    t0 = (now - hours * 3600) // step * step
    n = hours * 3600 // step
    temps, hums = [], []
    for i in range(n):
        t = t0 + i * step
        day = math.sin((t % 86400) / 86400 * 2 * math.pi - 2)
        if src == 2 and i > n * 0.9:
            temps.append(None)
            hums.append(None)
            continue
        temps.append(round((24 + src * 0.7 + 2.2 * day + random.uniform(-0.2, 0.2)) * 10))
        hums.append(round((55 + src * 3 - 6 * day + random.uniform(-1, 1)) * 10))
    return {"src": src, "t0": t0, "step": step, "temp": temps, "hum": hums}


async def broadcast(evt, data):
    msg = json.dumps({"evt": evt, "data": data})
    for ws in list(clients):
        if ws in authed:
            await ws.send_str(msg)


async def learn_later(params):
    await asyncio.sleep(2.5)
    state["ir"]["learning"] = False
    if params.get("raw"):
        nid = max([c["id"] for c in state["cmds"]] + [0]) + 1
        c = {"id": nid, "name": params.get("name") or f"Komut {nid}", "role": params.get("role", ""), "len": 199}
        state["cmds"].append(c)
        state["ir"]["commands"] = len(state["cmds"])
        await broadcast("learn", {"ok": True, "kind": "raw", **c, "detected": "UNKNOWN"})
    else:
        await broadcast("learn", {"ok": True, "kind": "protocol", "protocol": "FUJITSU_AC", "model": 1,
                                  "model_name": "ARRAH2E",
                                  "desc": "Model: 1 (ARRAH2E), Power: On, Mode: 1 (Cool), Temp: 24C, Fan: 0 (Auto)"})
    await broadcast("status", status())


async def scan_later():
    await asyncio.sleep(1.5)
    await broadcast("wifi.scan", {"networks": [
        {"ssid": "EvAgi_2.4G", "rssi": -55, "ch": 6, "open": False},
        {"ssid": "Komsu_WiFi", "rssi": -78, "ch": 11, "open": False},
        {"ssid": "Misafir", "rssi": -70, "ch": 1, "open": True}]})


async def handle(ws, req):
    cmd = req.get("cmd")
    if cmd == "hello":
        return {"name": state["settings"]["name"], "fw": "1.0.0", "api": 1, "authed": ws in authed,
                "time_valid": True, "default_pin": True}
    if cmd == "auth":
        if req.get("pin") == PIN:
            authed.add(ws)
            return {"name": state["settings"]["name"]}
        raise ValueError("PIN hatali")
    if ws not in authed:
        raise ValueError("auth")
    if cmd == "status":
        return status()
    if cmd in ("ac.set", "ac.toggle"):
        ac = state["ac"]
        if cmd == "ac.toggle":
            ac["power"] = not ac["power"]
        for k in ("power", "mode", "temp", "fan", "swing"):
            if k in req:
                ac[k] = req[k]
        state["ac_src"] = "app"
        state["ac_changed"] = int(time.time())
        asyncio.get_event_loop().call_later(0.2, lambda: asyncio.ensure_future(broadcast("status", status())))
        return {"state": ac}
    if cmd == "ir.learn":
        state["ir"]["learning"] = True
        state["ir"]["learn_remaining"] = 30
        asyncio.ensure_future(learn_later(req))
        return {"timeout_s": 30}
    if cmd == "ir.learn.cancel":
        state["ir"]["learning"] = False
        return {}
    if cmd == "ir.cmds":
        return {"cmds": state["cmds"], "ir": state["ir"]}
    if cmd == "ir.cmd.send":
        return {}
    if cmd == "ir.cmd.update":
        for c in state["cmds"]:
            if c["id"] == req.get("id"):
                c["name"] = req.get("name", c["name"])
                c["role"] = req.get("role", c["role"])
        return {}
    if cmd == "ir.cmd.delete":
        state["cmds"] = [c for c in state["cmds"] if c["id"] != req.get("id")]
        state["ir"]["commands"] = len(state["cmds"])
        return {}
    if cmd == "ir.protocols":
        return {"protocols": ["COOLIX", "DAIKIN", "FUJITSU_AC", "GREE", "LG", "MIDEA", "MITSUBISHI_AC",
                              "SAMSUNG_AC", "TOSHIBA_AC", "VESTEL_AC"]}
    if cmd == "ir.models":
        if req.get("protocol") == "FUJITSU_AC":
            return {"models": [{"id": i + 1, "name": n} for i, n in
                               enumerate(["ARRAH2E", "ARDB1", "ARREB1E", "ARJW2", "ARRY4", "ARREW4E"])]}
        return {"models": []}
    if cmd == "ir.protocol.set":
        state["ir"]["protocol"] = req.get("protocol")
        state["ir"]["model"] = req.get("model")
        state["ir"]["mode"] = "protocol"
        return {"ir": state["ir"]}
    if cmd == "ir.mode.set":
        state["ir"]["mode"] = "raw" if req.get("raw") else "protocol"
        return {"ir": state["ir"]}
    if cmd == "sensors":
        return {"sensors": state["sensors"], "auto_accept": True}
    if cmd == "sensor.set":
        for s in state["sensors"]:
            if s["id"] == req.get("id"):
                for k in ("name", "interval_min", "t_off", "h_off"):
                    if k in req:
                        s[k] = req[k]
        return {"sensors": state["sensors"]}
    if cmd == "sensor.remove":
        state["sensors"] = [s for s in state["sensors"] if s["id"] != req.get("id")]
        return {}
    if cmd == "history":
        return history(int(req.get("src", 0)), int(req.get("hours", 24)), int(req.get("step_min", 15)))
    if cmd == "events":
        now = int(time.time())
        return {"events": [{"t": now - i * 5400, "power": i % 2 == 0, "mode": "cool", "temp": 24,
                            "src": ["thermostat", "app", "remote", "schedule"][i % 4]} for i in range(8)]}
    if cmd == "auto.get":
        return {**state["auto"], "status": status()["auto"]}
    if cmd == "auto.set":
        for k in ("thermo", "dry", "timing"):
            if k in req:
                state["auto"][k].update(req[k])
        if "schedules" in req:
            state["auto"]["schedules"] = req["schedules"]
        return {**state["auto"], "status": status()["auto"]}
    if cmd == "wifi.list":
        return {"networks": state["wifi"], "state": "connected", "ssid": "EvAgi_2.4G", "ip": "192.168.1.50",
                "ap": False, "ap_ssid": "KlimaHub-A1B2"}
    if cmd == "wifi.add":
        state["wifi"] = [{"ssid": req["ssid"], "channel": 0}] + [w for w in state["wifi"] if w["ssid"] != req["ssid"]]
        return {"networks": state["wifi"]}
    if cmd == "wifi.remove":
        state["wifi"] = [w for w in state["wifi"] if w["ssid"] != req.get("ssid")]
        return {"networks": state["wifi"]}
    if cmd == "wifi.scan":
        asyncio.ensure_future(scan_later())
        return {}
    if cmd == "wifi.reconnect":
        return {}
    if cmd == "telegram.get":
        return {**state["telegram"], "status": status()["hub"]["telegram"]}
    if cmd == "telegram.set":
        t = state["telegram"]
        for k in ("enabled", "notify", "insecure", "chats"):
            if k in req:
                t[k] = req[k]
        if req.get("token"):
            t["has_token"] = True
        return {**t, "status": status()["hub"]["telegram"]}
    if cmd == "telegram.test":
        return {}
    if cmd == "settings.get":
        return state["settings"]
    if cmd == "settings.set":
        for k, v in req.items():
            if k in ("id", "cmd", "pin"):
                continue
            if isinstance(v, dict) and isinstance(state["settings"].get(k), dict):
                state["settings"][k].update(v)
            else:
                state["settings"][k] = v
        return state["settings"]
    if cmd == "time.set":
        return {"time": req.get("epoch")}
    if cmd == "system.info":
        return {"fw": "1.0.0", "idf": "v5.5.1", "uptime": int(time.time() - START) + 86000, "heap": 182000,
                "heap_min": 141000, "psram": 8100000, "reset_reason": 1, "mac": "34:85:18:00:A1:B2",
                "fs_used": 145000, "fs_total": 1507328,
                "espnow": {"rx": 960, "invalid": 0, "unknown": 0, "acks": 958, "ack_fail": 2, "queue_full": 0},
                "ble_clients": 0, "ws_clients": len(clients)}
    if cmd in ("system.reboot", "system.factory_reset", "system.ap", "ble.on"):
        return {}
    raise ValueError("bilinmeyen komut: %s" % cmd)


async def ws_handler(request):
    ws = web.WebSocketResponse()
    await ws.prepare(request)
    clients.add(ws)
    try:
        async for msg in ws:
            if msg.type != web.WSMsgType.TEXT:
                continue
            try:
                req = json.loads(msg.data)
            except json.JSONDecodeError as e:
                await ws.send_str(json.dumps({"ok": False, "error": str(e)}))
                continue
            try:
                data = await handle(ws, req)
                await ws.send_str(json.dumps({"id": req.get("id"), "ok": True, "data": data}))
            except Exception as e:  # noqa: BLE001
                await ws.send_str(json.dumps({"id": req.get("id"), "ok": False, "error": str(e)}))
    finally:
        clients.discard(ws)
        authed.discard(ws)
    return ws


async def ticker():
    while True:
        await asyncio.sleep(10)
        for s in state["sensors"]:
            if s.get("online") and "temp" in s:
                s["temp"] = round(s["temp"] + random.uniform(-0.1, 0.1), 1)
        await broadcast("status", status())


async def main(port):
    app = web.Application()
    app.router.add_get("/ws", ws_handler)
    app.router.add_get("/", lambda r: web.FileResponse(os.path.join(APP_DIR, "index.html")))
    app.router.add_static("/", APP_DIR)
    runner = web.AppRunner(app)
    await runner.setup()
    await web.TCPSite(runner, "0.0.0.0", port).start()
    print(f"Sahte hub: http://localhost:{port}  (PIN {PIN})")
    await ticker()


if __name__ == "__main__":
    asyncio.run(main(int(sys.argv[1]) if len(sys.argv) > 1 else 8080))
