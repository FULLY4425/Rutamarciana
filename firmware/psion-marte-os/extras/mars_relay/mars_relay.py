"""Mars relay: your PC opens the Ojo de Marte web (Ruta Marciana) in Edge and passes its data to Mars OS on the Psion.

The Psion still works out every route itself; the relay only carries what the web has and the Psion hasn't:
  * live plan sync: the stops you set on the web show up on the Psion (and the Psion's on the web), no code needed
  * the weather the web is using (dust, storms: zones you can't enter + the walking speed), so routes stay the same
  * pictures of the web's 3D view of your route, sized for the Psion's screen
  * the web's own server (mission check, 4-letter codes), passed through
The web page needs the small "Psion hook" (window.ojoDeMarte in web/psion.js, see docs/RELOJ_MARS_OS.md).

Start the window (double-click start_mars_relay.bat), or run headless:  python mars_relay.py
Settings: mars_relay_config.json next to this file (the window edits it). Needs: pip install playwright pillow
(Microsoft Edge is used; it's already on Windows).

Endpoints (plain HTTP on your home network, port 8790):
  GET  /mr/hello                    -> {"relay": "mars", "version": 1, "web": url, "webOk": bool, "hook": bool, "plan": N, "window": bool}
  GET  /mr/plan?since=N             -> the web's plan: {"version": N, "stops": [{"name", "x", "y", "lat", "lon", "custom"}],
                                       "maxSlopeDegrees": 20, "weatherKey": "clear", "routed": bool, "status": text}
                                       (nothing new since N: {"version": N, "same": true})
  POST /mr/plan                     <- the Psion's route (the JSON it sends the server: stops, maxSlopeDegrees, weather)
                                       -> the web takes it and plans it; answer as GET /mr/plan
  GET  /mr/weather                  -> the web's weather: planner.py's environment + "scenarios": [{"key", "label"}]
  POST /mr/weather?key=storm        -> the web switches to that weather (and plans again); answer as GET /mr/weather
  GET  /mr/view3d?w=320&h=200&mode=terrain&yaw=0&pitch=1.32&zoom=1&focus=route&p=0
                                    -> the web's 3D view: w x h pixels, RGB565 little-endian (what the TFT takes)
                                       mode: terrain | follow | surface | globe; p: where on the route (0..1)
  GET / POST /api/...               -> passed on to the web's server (POST /api/psion/missions, GET .../<code>, /api/health)
Errors come back as {"error": "..."} with status 502 / 503.
"""
import base64
import io
import json
import math
import os
import queue
import socket
import sys
import threading
import time
import traceback
import urllib.error
import urllib.request
from concurrent.futures import Future
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

VERSION = 1
HERE = os.path.dirname(os.path.abspath(__file__))
CONFIG_FILE = os.path.join(HERE, "mars_relay_config.json")
LOG_FILE = os.path.join(HERE, "mars_relay.log")
DEFAULTS = {"web": "https://rutamarciana.onrender.com/?region=glen-torridon", "port": 8790, "window": True}
CFG = dict(DEFAULTS)
HOOK_TIMEOUT = 40.0                               # seconds to wait for the page's Psion hook
ENV_KEYS = ("weather", "label", "visibility", "notice", "sourceType", "model", "speedKmH", "restrictions", "scenarios")


def load_config():
    CFG.clear(); CFG.update(DEFAULTS)
    try:
        with open(CONFIG_FILE, encoding="utf-8") as f:
            got = json.load(f)
        CFG.update({k: got[k] for k in DEFAULTS if k in got})
    except (OSError, ValueError):
        pass
    CFG["web"] = normalize_web(CFG["web"])
    return CFG


def save_config(cfg):
    with open(CONFIG_FILE, "w", encoding="utf-8") as f:
        json.dump({k: cfg[k] for k in DEFAULTS}, f, indent=2, ensure_ascii=False)


def normalize_web(url):
    url = (url or "").strip()
    if url and "://" not in url:
        url = ("http://" if url.startswith(("localhost", "127.", "192.168.", "10.")) else "https://") + url
    if url:
        from urllib.parse import urlsplit, urlunsplit
        parts=urlsplit(url)
        url=urlunsplit((parts.scheme,parts.netloc,parts.path or "/",parts.query,""))
    return url


def lan_ip():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("10.255.255.255", 1))          # (no packet is sent: it only picks the home network's address)
        return s.getsockname()[0]
    except OSError:
        return "127.0.0.1"
    finally:
        s.close()


_log_lock = threading.Lock()


def log(msg):
    line = time.strftime("%H:%M:%S ") + msg
    with _log_lock:
        print(line, flush=True)
        try:
            with open(LOG_FILE, "a", encoding="utf-8") as f:
                f.write(line + "\n")
        except OSError:
            pass


class RelayError(Exception):
    pass


def rgb565(img, w, h):
    """A picture -> w x h RGB565 little-endian bytes, filling the frame (cropped to the Psion's shape, not squashed)."""
    from PIL import Image
    sw, sh = img.size
    scale = max(w / sw, h / sh)
    nw, nh = max(w, round(sw * scale)), max(h, round(sh * scale))
    img = img.convert("RGB").resize((nw, nh), Image.LANCZOS)
    left, top = (nw - w) // 2, (nh - h) // 2
    img = img.crop((left, top, left + w, top + h))
    r, g, b = img.split()
    r = r.point(lambda v: v >> 3); g = g.point(lambda v: v >> 2); b = b.point(lambda v: v >> 3)
    rb, gb, bb = r.tobytes(), g.tobytes(), b.tobytes()
    out = bytearray(w * h * 2)
    for i in range(w * h):
        v = (rb[i] << 11) | (gb[i] << 5) | bb[i]
        out[2 * i] = v & 0xFF
        out[2 * i + 1] = v >> 8
    return bytes(out)


class Web:
    """Edge, driven by Playwright, in one thread of its own (Playwright's rule). Two pages of the same web:
    "sync": the one you see and plan on (a normal window); "render": a hidden one for the 3D pictures, so taking a
    picture never moves the camera under your mouse. Without the window, one hidden page does both."""

    def __init__(self):
        self.jobs = queue.Queue()
        self.thread = threading.Thread(target=self._run, daemon=True, name="edge")
        self.pw = self.shown = self.hidden = self.sync = self.render = None
        self.state_lock = threading.Lock()
        self.version = 0
        self.plan = None                          # the web's plan as the hook gives it (stops with lat/lon)
        self.plan_key = None                      # what counts as "the same plan" (stops, limit, weather)
        self.render_key = None                    # the plan the hidden page has
        self.terrain = None
        self.ok = False
        self.err = "Abriendo la web..."
        self.window = bool(CFG["window"])
        self.thread.start()

    # ---------- the Edge thread ----------
    def call(self, fn, timeout=90):
        f = Future()
        self.jobs.put((fn, f))
        return f.result(timeout=timeout)

    def _run(self):
        last_poll = 0.0
        while True:
            try:
                fn, f = self.jobs.get(timeout=0.3)
            except queue.Empty:
                fn = f = None
            if fn:
                try:
                    f.set_result(fn())
                except Exception as e:               # noqa: BLE001 - the error goes back to the HTTP request
                    f.set_exception(e)
            now = time.time()
            if now - last_poll >= 0.7:
                last_poll = now
                try:
                    self._poll()
                except Exception as e:               # noqa: BLE001
                    self._fail(f"{e}")

    def _fail(self, msg):
        if self.ok or self.err != msg:
            log("web: " + msg)
        self.ok, self.err = False, msg

    def _launch(self, headless):
        from playwright.sync_api import sync_playwright
        if not self.pw:
            self.pw = sync_playwright().start()
        args = ["--disable-blink-features=AutomationControlled"]
        if headless:                                  # (WebGL without a screen: Chrome's software renderer)
            args += ["--enable-unsafe-swiftshader", "--use-angle=swiftshader", "--ignore-gpu-blocklist"]
        if os.environ.get("RUTA_CHROMIUM"):
            return self.pw.chromium.launch(headless=headless,executable_path=os.environ["RUTA_CHROMIUM"],args=args)
        err = None
        for kw in ({"channel": "msedge"}, {"channel": "chrome"}, {}):
            try:
                return self.pw.chromium.launch(headless=headless, ignore_default_args=["--enable-automation"], args=args, **kw)
            except Exception as e:                    # noqa: BLE001
                err = e
        raise RelayError(f"No se pudo abrir Edge ({err})")

    def _open(self, browser, size):
        ctx = browser.new_context(viewport=size, device_scale_factor=1, locale="es-MX")
        page = ctx.new_page()
        page.goto(CFG["web"], wait_until="domcontentloaded", timeout=30000)
        return page

    def _ready(self, page):
        t0 = time.time()
        while time.time() - t0 < HOOK_TIMEOUT:
            try:
                st = page.evaluate("() => typeof window.ojoDeMarte === 'object' ? (window.ojoDeMarte.ready() ? 2 : 1) : 0")
            except Exception:                         # noqa: BLE001 - (the page is still loading)
                st = -1
            if st == 2:
                return True
            if st == 0 and time.time() - t0 > 12:
                raise RelayError("Esta web no tiene el gancho del Psion (window.ojoDeMarte): actualiza Ruta Marciana.")
            page.wait_for_timeout(300)
        raise RelayError("La web no terminó de cargar.")

    def _ensure(self):
        """Opens the pages if needed; True when the hook answers."""
        if self.sync and not self.sync.is_closed() and self.ok:
            return True
        try:
            if self.window and (not self.shown or not self.shown.is_connected()):
                self.shown = self._launch(False)
                self.sync = self._open(self.shown, {"width": 1366, "height": 860})
                log(f"web window: {CFG['web']}")
            if not self.hidden or not self.hidden.is_connected():
                self.hidden = self._launch(True)
                self.render = self._open(self.hidden, {"width": 1366, "height": 860})
                self.render_key = None
            if not self.window:
                self.sync = self.render
            if self.sync.is_closed():                 # (you closed the window: open it again)
                self.sync = self._open(self.shown, {"width": 1366, "height": 860})
            for page in {id(self.sync): self.sync, id(self.render): self.render}.values():
                self._ready(page)
                page.evaluate("() => ojoDeMarte.mars && ojoDeMarte.mars()")   # (the web opens on its KSP space: Mars, please)
            self.terrain = self.sync.evaluate("() => ojoDeMarte.terrain()")
            if not self.ok:
                log("web ready: Psion hook v%s" % self.sync.evaluate("() => ojoDeMarte.version"))
            self.ok, self.err = True, ""
            return True
        except Exception as e:                        # noqa: BLE001
            self._fail(str(e).split("\n")[0][:200])
            return False

    def _read_plan(self):
        p = self.sync.evaluate("() => ojoDeMarte.plan()")
        key = json.dumps([p.get("stops"), p.get("maxSlopeDegrees"), p.get("weather"), p.get("environment")], sort_keys=True)
        with self.state_lock:
            if key != self.plan_key:
                self.version += 1
                self.plan_key = key
                if self.plan is not None:
                    log(f"web plan changed: {len(p.get('stops') or [])} stops, {p.get('maxSlopeDegrees')} deg, {p.get('weather')} (v{self.version})")
            self.plan = p
        return p

    def _poll(self):
        if not self._ensure():
            return
        try:
            self._read_plan()
        except Exception as e:                        # noqa: BLE001 - (page reloaded or closed: open / wait again)
            self.ok = False
            raise RelayError(str(e).split("\n")[0][:200])

    # ---------- what the HTTP side asks (run in the Edge thread) ----------
    def need(self):
        if not self._ensure():
            raise RelayError(self.err or "La web no responde.")

    def get_plan(self):
        self.need()
        return self._read_plan()

    def set_plan(self, payload):
        self.need()
        self.sync.evaluate("p => ojoDeMarte.setPlan(p)", payload)
        p = self._read_plan()
        log(f"Psion plan -> web: {len(payload.get('stops') or [])} stops, {payload.get('maxSlopeDegrees')} deg (v{self.version})")
        return p

    def weather(self):
        self.need()
        env = self.sync.evaluate("() => ojoDeMarte.weather()")
        return {k: env[k] for k in ENV_KEYS if k in env}

    def set_weather(self, key):
        self.need()
        self.sync.evaluate("k => ojoDeMarte.setWeather(k)", key)
        self._read_plan()
        log(f"Psion weather -> web: {key}")
        return self.weather()

    def view3d(self, cam, w, h, plan=None):
        """plan: the Psion's route (the 3D view shows yours, even when live sync is off); None: the web's plan."""
        self.need()
        page = self.render
        if plan is None and self.plan is not None:
            pl = self.plan
            plan = {"stops": [s if s.get("custom") else s["name"] for s in pl["stops"]], "maxSlopeDegrees": pl["maxSlopeDegrees"], "weather": pl.get("weather"), "environment": pl.get("environment")}
        if plan is not None and isinstance(plan.get("stops"), list):
            key = json.dumps([plan.get("stops"), plan.get("maxSlopeDegrees"), plan.get("weather"), plan.get("environment")], sort_keys=True)
            if key != self.render_key:                # (the hidden page plans it once; then only the camera moves)
                page.evaluate("p => ojoDeMarte.setPlan(p)", plan)
                self.render_key = key
                if page is self.sync:
                    self._read_plan()
        from PIL import Image
        state = page.evaluate("o => ojoDeMarte.view3d(Object.assign({snapshot: true}, o))", cam)
        img = state.pop("image", None) if isinstance(state, dict) else None
        if img and img.startswith("data:image/png;base64,") and len(img) > 2000:   # just the 3D picture, no buttons on it
            return rgb565(Image.open(io.BytesIO(base64.b64decode(img.split(",", 1)[1]))), w, h), state
        page.evaluate("() => new Promise(r => requestAnimationFrame(() => requestAnimationFrame(r)))")
        box = page.locator("#map3d").bounding_box()  # (an older hook: a screenshot of that part of the page)
        if not box or box["width"] < 50:
            raise RelayError("La vista 3D no está en la página.")
        return rgb565(Image.open(io.BytesIO(page.screenshot(clip=box))), w, h), state

    def stop(self):
        for b in (self.shown, self.hidden):
            try:
                if b:
                    b.close()
            except Exception:                         # noqa: BLE001
                pass
        try:
            if self.pw:
                self.pw.stop()
        except Exception:                             # noqa: BLE001
            pass


WEB = None


def cells(plan, terrain):
    """The web's stops -> the Psion's cells (planner.py's grid_point: floor(x + 0.5))."""
    out = []
    for s in plan.get("stops") or []:
        x = math.floor((s["lon"] - terrain["west"]) / terrain["degreeStep"] + .5)
        y = math.floor((terrain["north"] - s["lat"]) / terrain["degreeStep"] + .5)
        out.append({"name": s["name"], "x": x, "y": y, "lat": s["lat"], "lon": s["lon"], "custom": bool(s.get("custom"))})
    return out


def plan_reply(p):
    return {"version": WEB.version, "stops": cells(p, WEB.terrain), "maxSlopeDegrees": p.get("maxSlopeDegrees"),
            "weatherKey": p.get("weather") or "clear", "routed": bool(p.get("routed")), "status": (p.get("status") or "")[:160]}


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"
    server_version = "MarsRelay/1"

    def log_message(self, fmt, *a):
        pass

    def send(self, status, body, ctype="application/json; charset=utf-8"):
        if isinstance(body, (dict, list)):
            body = json.dumps(body, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def body(self):
        n = int(self.headers.get("Content-Length") or 0)
        if not 0 <= n <= 64 * 1024:
            raise RelayError("Mensaje demasiado grande.")
        return self.rfile.read(n) if n else b""

    def do_GET(self):
        self.route("GET")

    def do_POST(self):
        self.route("POST")

    def route(self, method):
        u = urlparse(self.path)
        q = {k: v[0] for k, v in parse_qs(u.query).items()}
        try:
            if u.path.startswith("/api/"):
                return self.proxy(method, u)
            if u.path == "/mr/hello":
                return self.send(200, {"relay": "mars", "version": VERSION, "web": CFG["web"], "webOk": WEB.ok, "hook": WEB.ok,
                                       "error": WEB.err, "plan": WEB.version, "window": WEB.window})
            if u.path == "/mr/plan" and method == "GET":
                since = int(q.get("since", "-1") or -1)
                p = WEB.call(WEB.get_plan, 30)
                if since == WEB.version:
                    return self.send(200, {"version": WEB.version, "same": True})
                return self.send(200, plan_reply(p))
            if u.path == "/mr/plan" and method == "POST":
                payload = json.loads(self.body().decode("utf-8"))
                if not isinstance(payload, dict) or not isinstance(payload.get("stops"), list):
                    raise RelayError("Plan no válido.")
                p = WEB.call(lambda: WEB.set_plan(payload), 60)
                return self.send(200, plan_reply(p))
            if u.path == "/mr/weather":
                if method == "POST":
                    key = q.get("key", "")
                    return self.send(200, WEB.call(lambda: WEB.set_weather(key), 60))
                return self.send(200, WEB.call(WEB.weather, 30))
            if u.path == "/mr/view3d":
                w, h = int(q.get("w", 320)), int(q.get("h", 200))
                if not (16 <= w <= 320 and 16 <= h <= 240):
                    raise RelayError("Tamaño no válido.")
                cam = {"mode": q.get("mode", "terrain"), "focus": q.get("focus", "route")}
                for k, name in (("yaw", "yaw"), ("pitch", "pitch"), ("zoom", "zoom"), ("p", "progress")):
                    if k in q:
                        cam[name] = float(q[k])
                plan = None
                if method == "POST":                  # (the Psion sends its route along: the picture shows that one)
                    raw = self.body()
                    plan = json.loads(raw.decode("utf-8")) if raw.strip() else None
                    if not isinstance(plan, dict) or not plan.get("stops"):
                        plan = None
                t0 = time.time()
                pix, state = WEB.call(lambda: WEB.view3d(cam, w, h, plan), 90)
                log(f"3D view {w}x{h} {cam.get('mode')} for the Psion ({time.time() - t0:.1f} s)")
                return self.send(200, pix, "application/octet-stream")
            return self.send(404, {"error": "No existe: " + u.path})
        except RelayError as e:
            return self.send(503, {"error": str(e)})
        except Exception as e:                        # noqa: BLE001
            log("error: " + "".join(traceback.format_exception_only(type(e), e)).strip()[:300])
            msg = str(e).split("\n")[0][:160] or type(e).__name__
            return self.send(502, {"error": "El relay falló: " + msg})

    def proxy(self, method, u):
        """The web's own server: the Psion's mission check and codes go through here."""
        from urllib.parse import urlsplit, urlunsplit
        base = urlsplit(CFG["web"])
        url = urlunsplit((base.scheme,base.netloc,u.path,u.query,""))
        data = self.body() if method == "POST" else None
        req = urllib.request.Request(url, data=data, method=method, headers={"Content-Type": "application/json", "User-Agent": "MarsRelay/1"})
        try:
            with urllib.request.urlopen(req, timeout=70) as r:
                return self.send(r.status, r.read(), r.headers.get("Content-Type", "application/json"))
        except urllib.error.HTTPError as e:
            return self.send(e.code, e.read(), e.headers.get("Content-Type", "application/json"))
        except (urllib.error.URLError, OSError) as e:
            return self.send(502, {"error": f"La web no responde ({getattr(e, 'reason', e)})."})


def main():
    global WEB
    load_config()
    a = sys.argv[1:]                              # (for tests: --web URL --port N --no-window; nothing is saved)
    for i, v in enumerate(a):
        if v == "--web" and i + 1 < len(a): CFG["web"] = normalize_web(a[i + 1])
        if v == "--port" and i + 1 < len(a): CFG["port"] = int(a[i + 1])
        if v == "--no-window": CFG["window"] = False
    if not CFG["web"]:
        sys.exit("No web address: set it in the Mars relay window (mars_relay_config.json).")
    WEB = Web()
    httpd = ThreadingHTTPServer(("0.0.0.0", int(CFG["port"])), Handler)
    httpd.daemon_threads = True
    ip = lan_ip()
    log(f"Mars relay {VERSION} on {ip}:{CFG['port']} -> {CFG['web']}")
    log(f"On the Psion: AJUSTES > Conexion: Relay, Direccion del relay: {ip}:{CFG['port']}")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        WEB.call(WEB.stop, 10) if WEB.thread.is_alive() else None


if __name__ == "__main__":
    main()
