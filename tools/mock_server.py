#!/usr/bin/env python3
"""Mock of the WaterFill device API with a small live simulation, for developing the UI off-device.

    python3 tools/mock_server.py [port]          # then open http://localhost:8080/
    MOCK_SCENARIO=supply python3 tools/mock_server.py   # also: fault, manual, ozone, quiet

It serves web/index.html and mimics /api/state, /api/config, /api/cmd, /api/history, /api/log,
/stream (server-sent events). The simulation is deliberately simple; the real controller logic is
in WaterFill_v2/control.cpp and is tested by tests/run.sh.
"""
import json, os, random, sys, threading, time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCENARIO = os.environ.get("MOCK_SCENARIO", "normal")
T0 = time.time()
lock = threading.Lock()
rnd = random.Random(7)

cfg = {
    "tanks": [{"en": True, "inv": False, "max": 600} for _ in range(3)],
    "supply": {"en": True, "inv": False, "trip": 2, "recover": 10},
    "pump": {"on": 10, "off": 10, "lead": 1000, "lag": 1000},
    "debounce": 2000, "ozone": {"en": True, "run": 60}, "manualMax": 300,
    "sched": {"en": False, "start": 480, "end": 1260}, "tz": "MST7", "pinSet": False, "mode": "auto",
}
S = dict(mode="auto", st="idle", tg=-1, man=False, pump=False, v1=False, v2=False, oz=False, cool=0, stint=0,
         level=[100.0, 100.0, 100.0], need=[False] * 3, fault=[False] * 3, supply=80.0, empty=False,
         fillEl=0, fillPump=0, manRem=0, ozRem=0, phase_t=0.0, last=[0, 0, 0], today_pump=0, today_f=[0, 0, 0], btn=False)
events = []   # [t, type, arg, a, b, pad]
days = {}     # day -> [pump, f0, f1, f2, ozRuns, ozSec, faults]
log = []


def now(): return int(time.time())
def day_of(t): return int(t // 86400)


def add_event(t, ty, arg=0, a=0, b=0, pad=0):
    events.append([t, ty, arg, a, b, pad])
    del events[:-300]


def seed_history():
    t = now()
    for back in range(60, 0, -1):
        d = day_of(t) - back
        pump = 0; f = [0, 0, 0]
        for tank, per_day in enumerate([1.0, 0.6, 0.35]):
            if rnd.random() < per_day:
                n = 1 + (rnd.random() < 0.25)
                for _ in range(n):
                    dur = int(rnd.uniform(25, 80)); pumped = int(dur * 0.55)
                    at = d * 86400 + int(rnd.uniform(9, 20) * 3600)
                    f[tank] += 1; pump += pumped
                    if back <= 14: add_event(at + 60 * back * 0, 2, tank, dur, pumped, 0)
        oz = 1 if rnd.random() < 0.5 else 0
        days[d] = [pump, f[0], f[1], f[2], oz, oz * 60, 0]
        if oz and back <= 14: add_event(d * 86400 + 22 * 3600, 5, 0, 60)
    # a supply-empty episode and one fault to show in the charts
    add_event(t - 5 * 86400 + 4000, 4, 1); add_event(t - 5 * 86400 + 4000 + 5400, 4, 0)
    add_event(t - 9 * 86400 + 30000, 3, 2, 600); add_event(t - 9 * 86400 + 30700, 8, 2)
    events.sort(key=lambda e: e[0])
    days.setdefault(day_of(t), [0, 0, 0, 0, 0, 0, 0])
    add_event(t - 300, 1, 1)


def say(msg):
    log.append("[%ds] %s" % (time.time() - T0, msg)); del log[:-40]


def day_row():
    return days.setdefault(day_of(now()), [0, 0, 0, 0, 0, 0, 0])


def finish_fill(reason=0):
    t = now(); tg = S["tg"]
    if tg >= 0 and S["fillPump"] > 0:
        add_event(t, 2, tg, S["fillEl"], S["fillPump"], reason | (256 if S["man"] else 0))
        day_row()[1 + tg] += 1; S["last"][tg] = t; say("%d fill ended" % tg)
    S.update(st="idle", tg=-1, man=False, pump=False, v1=False, v2=False, cool=0, stint=0, fillEl=0, fillPump=0, manRem=0)


def sim_step():
    """1 Hz tick."""
    with lock:
        c = cfg
        for i in range(3):   # tanks drain; need water below 35%, full = 100%
            if S["tg"] != i or not S["pump"]:
                S["level"][i] = max(0.0, S["level"][i] - [0.22, 0.14, 0.08][i] * (1.0 if SCENARIO != "quiet" else 0.0))
            if not S["need"][i] and S["level"][i] < 35: S["need"][i] = True
        S["empty"] = SCENARIO == "supply" or S["supply"] < 12
        if S["st"] == "idle" and S["mode"] == "auto" and not S["empty"] and S["oz"] is False:
            for i in range(3):
                if c["tanks"][i]["en"] and S["need"][i] and not S["fault"][i]:
                    S.update(st="opening", tg=i, man=False, v1=(i == 0), v2=(i == 1), phase_t=0, fillEl=0, fillPump=0); break
        elif S["st"] == "opening":
            S["phase_t"] += 1
            if S["phase_t"] >= 1: S.update(st="filling", pump=True, phase_t=0)
        elif S["st"] == "filling":
            S["fillEl"] += 1; i = S["tg"]
            if S["pump"] and not S["empty"]:
                S["fillPump"] += 1; S["stint"] += 1; S["supply"] = max(0, S["supply"] - 0.1)
                S["level"][i] = min(100, S["level"][i] + 3.0); day_row()[0] += 1
                if S["stint"] >= c["pump"]["on"]: S.update(pump=False, cool=c["pump"]["off"], stint=0)
            elif not S["pump"]:
                S["cool"] -= 1
                if S["cool"] <= 0 and not S["empty"]: S.update(pump=True, cool=0)
            if S["empty"]: S["pump"] = False
            if S["man"]:
                S["manRem"] -= 1
                if S["manRem"] <= 0: finish_fill(6)
            elif S["level"][i] >= 100:
                S["need"][i] = False; finish_fill(0)
            elif SCENARIO == "fault" and S["fillPump"] >= 25:
                S["fault"][i] = True; add_event(now(), 3, i, S["fillPump"]); finish_fill(5)
        elif S["st"] == "ozone":
            S["ozRem"] -= 1
            if S["ozRem"] <= 0:
                add_event(now(), 5, 0, c["ozone"]["run"]); day_row()[4] += 1; day_row()[5] += c["ozone"]["run"]
                S.update(st="idle", oz=False, ozRem=0)
        if not S["empty"] and S["supply"] < 60 and SCENARIO != "supply": S["supply"] += 0.6   # the supply refills slowly


NET = {"ssid": "Thukpa", "mqtt": {"en": True, "host": "192.168.1.5", "port": 1883, "user": ""}}


def net_json():
    return {"ssid": NET["ssid"], "hasPass": True, "apSsid": "WaterFill-008C", "apPassSet": True, "ap": SCENARIO == "ap", "connected": SCENARIO != "ap",
            "rssi": -48, "ip": "192.168.1.152", "mqtt": dict(NET["mqtt"], hasPass=False, ok=SCENARIO != "ap", node="waterfill_fd008c")}


def state_json():
    with lock:
        c = cfg; r = day_row(); di = 0xFF
        for i in range(3):
            active = S["need"][i] != c["tanks"][i]["inv"]
            if active: di &= ~(0x08 << i)
        if S["empty"]: di &= ~0x04
        do = 0xFF
        if S["pump"]: do &= ~1
        if S["v1"]: do &= ~2
        if S["v2"]: do &= ~4
        if S["oz"]: do &= ~0x10
        return {
            "t": now(), "up": int(time.time() - T0) + 86400 * 3 + 5000, "tv": True, "mode": S["mode"], "st": S["st"], "tg": S["tg"], "man": S["man"],
            "out": {"pump": S["pump"], "v1": S["v1"], "v2": S["v2"], "oz": S["oz"]},
            "cool": max(0, S["cool"]), "stint": S["stint"],
            "tanks": [{"n": S["need"][i], "e": c["tanks"][i]["en"], "f": S["fault"][i], "r": bool(not (di & (0x08 << i))), "l": S["last"][i] or (now() - 3600 * (2 + 5 * i))} for i in range(3)],
            "sup": {"empty": S["empty"], "en": c["supply"]["en"], "r": S["empty"]},
            "btn": False, "fillEl": S["fillEl"], "fillPump": S["fillPump"], "fillLim": 600, "manRem": max(0, S["manRem"]), "ozRem": max(0, S["ozRem"]),
            "ozp": False, "wait": 1 if (S["st"] == "filling" and S["empty"]) else 0, "io": False, "di": di, "do": do,
            "today": {"pump": r[0], "oz": r[5], "fills": r[1:4]},
            "net": {"ap": SCENARIO == "ap", "apSsid": "WaterFill-008C"}, "mqtt": {"en": True, "ok": SCENARIO != "ap"},
            "life": {"pump": 41 * 3600 + 1260, "oz": 5400, "cycles": 18432, "faults": 1, "fills": [212, 131, 88]},
            "sys": {"rssi": -48, "heap": 172000, "ip": "192.168.1.152", "fw": "2.0.0-dev (mock)", "rst": "software", "mac": "AC:15:18:FD:00:8C"},
        }


def do_cmd(d):
    with lock:
        c = d.get("c"); t = d.get("t", -1)
        if c == "mode": S["mode"] = "auto" if d.get("v") == "auto" else "manual"; cfg["mode"] = S["mode"]; finish_fill(4); say("mode " + S["mode"])
        elif c == "stop": finish_fill(3); S["mode"] = "manual"; S.update(oz=False, ozRem=0); cfg["mode"] = "manual"
        elif c == "run" and S["mode"] == "manual" and 0 <= t < 3:
            finish_fill(3); S.update(st="opening", tg=t, man=True, v1=(t == 0), v2=(t == 1), phase_t=0, fillEl=0, fillPump=0, manRem=cfg["manualMax"], stint=0)
        elif c == "ozone": finish_fill(3); S.update(st="ozone", oz=True, ozRem=cfg["ozone"]["run"])
        elif c == "ozstop": S.update(st="idle", oz=False, ozRem=0)
        elif c == "clear" and 0 <= t < 3: S["fault"][t] = False; add_event(now(), 8, t)


def merge(dst, src):
    for k, v in src.items():
        if k == "pin":
            dst["pinSet"] = bool(v)
        elif isinstance(v, dict) and isinstance(dst.get(k), dict): merge(dst[k], v)
        elif isinstance(v, list) and isinstance(dst.get(k), list):
            for i, x in enumerate(v):
                if isinstance(x, dict) and i < len(dst[k]): merge(dst[k][i], x)
        else: dst[k] = v


class H(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    def log_message(self, *a): pass

    def send_json(self, obj, code=200):
        b = json.dumps(obj).encode()
        self.send_response(code); self.send_header("Content-Type", "application/json"); self.send_header("Content-Length", str(len(b))); self.end_headers(); self.wfile.write(b)

    def do_GET(self):
        p = self.path.split("?")[0]
        if p == "/":
            b = open(os.path.join(ROOT, "web", "index.html"), "rb").read()
            self.send_response(200); self.send_header("Content-Type", "text/html"); self.send_header("Content-Length", str(len(b))); self.end_headers(); self.wfile.write(b)
        elif p == "/api/state": self.send_json(state_json())
        elif p == "/api/config":
            with lock: self.send_json(cfg)
        elif p == "/api/history":
            with lock:
                dl = sorted(days.items())
                self.send_json({"now": now(), "day": day_of(now()), "ev": events, "days": [[d] + v for d, v in dl]})
        elif p == "/api/net": self.send_json(net_json())
        elif p == "/api/net/scan": self.send_json({"done": True, "nets": [{"s": "Thukpa", "r": -44, "e": 1}, {"s": "Thukpa-guest", "r": -61, "e": 1}, {"s": "NeighborWiFi", "r": -82, "e": 1}]})
        elif p == "/api/log":
            b = "\n".join(log).encode(); self.send_response(200); self.send_header("Content-Type", "text/plain"); self.send_header("Content-Length", str(len(b))); self.end_headers(); self.wfile.write(b)
        elif p == "/stream":
            self.send_response(200); self.send_header("Content-Type", "text/event-stream"); self.send_header("Cache-Control", "no-cache"); self.end_headers()
            try:
                while True:
                    self.wfile.write(("event: state\ndata: %s\n\n" % json.dumps(state_json())).encode()); self.wfile.flush(); time.sleep(1)
            except Exception: pass
        else: self.send_response(404); self.send_header("Content-Length", "0"); self.end_headers()

    def do_POST(self):
        n = int(self.headers.get("Content-Length", 0)); body = self.rfile.read(n) if n else b""
        p = self.path.split("?")[0]
        if p == "/api/cmd":
            try: do_cmd(json.loads(body))
            except Exception: return self.send_json({"err": "json"}, 400)
            self.send_json({"ok": True})
        elif p == "/api/config":
            with lock:
                merge(cfg, json.loads(body))
                for i in range(3): S["need"][i] = S["need"][i]
                self.send_json(cfg)
        elif p == "/api/net":
            d = json.loads(body or b"{}")
            if "ssid" in d: NET["ssid"] = d["ssid"]
            if "mqtt" in d: NET["mqtt"].update({k: v for k, v in d["mqtt"].items() if k != "pass"})
            self.send_json(net_json())
        elif p == "/api/update": self.send_response(200); self.send_header("Content-Length", "13"); self.end_headers(); self.wfile.write(b"OK, rebooting")
        else: self.send_response(404); self.send_header("Content-Length", "0"); self.end_headers()


def main():
    seed_history()
    if SCENARIO == "fault": S["level"] = [100, 100, 20]; S["need"][2] = True
    if SCENARIO == "manual": S["mode"] = "manual"; cfg["mode"] = "manual"
    if SCENARIO == "ozone": S.update(st="ozone", oz=True, ozRem=42)
    if SCENARIO == "normal": S["level"] = [40, 100, 100]
    def loop():
        while True: sim_step(); time.sleep(1)
    threading.Thread(target=loop, daemon=True).start()
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
    print("mock on http://localhost:%d/  scenario=%s" % (port, SCENARIO), flush=True)
    ThreadingHTTPServer(("0.0.0.0", port), H).serve_forever()


if __name__ == "__main__":
    main()
