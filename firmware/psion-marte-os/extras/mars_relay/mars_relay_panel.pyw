"""Mars relay window: set the Ojo de Marte web address, start / stop the relay, see what to type on the Psion.

Double-click start_mars_relay.bat (or this file). Closing the window stops the relay.
"""
import json
import os
import queue
import socket
import subprocess
import sys
import threading
import tkinter as tk
import urllib.error
import urllib.request
import webbrowser
from tkinter import scrolledtext, ttk

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import mars_relay as relay  # noqa: E402

BG, PANEL, TEXT, MUTED, ORANGE, CYAN, GREEN, RED = "#101414", "#171c1c", "#e9ebe5", "#99a5a0", "#f18559", "#9ed9cb", "#3ec484", "#e84c3d"


def python_exe():
    exe = sys.executable
    if exe.lower().endswith("pythonw.exe") and os.path.exists(exe[:-5] + ".exe"):
        exe = exe[:-5] + ".exe"
    return exe


class Panel:
    def __init__(self, root):
        self.root, self.proc, self.lines = root, None, queue.Queue()
        cfg = relay.load_config()
        root.title("Mars relay - Ojo de Marte")
        root.geometry("720x560")
        root.minsize(600, 480)
        root.configure(bg=BG)
        st = ttk.Style()
        st.theme_use("clam")
        st.configure(".", background=BG, foreground=TEXT, fieldbackground=PANEL, font=("Segoe UI", 10))
        st.configure("TButton", background=PANEL, foreground=TEXT, bordercolor="#303737", padding=(10, 4))
        st.map("TButton", background=[("active", "#202727")])
        st.configure("Go.TButton", background=ORANGE, foreground="#1c201d")
        st.map("Go.TButton", background=[("active", "#f39a74")])
        st.configure("TCheckbutton", background=BG, foreground=TEXT)
        st.configure("TEntry", foreground=TEXT, insertcolor=TEXT)

        f = tk.Frame(root, bg=BG, padx=14, pady=10)
        f.pack(fill="both", expand=True)
        tk.Frame(f, bg=ORANGE, height=3).pack(fill="x", pady=(0, 8))
        tk.Label(f, text="MARS RELAY", bg=BG, fg=ORANGE, font=("Consolas", 10, "bold")).pack(anchor="w")
        tk.Label(f, text="Tu PC abre la web de Ojo de Marte y le pasa al Psion el plan, el clima y la vista 3D.",
                 bg=BG, fg=MUTED).pack(anchor="w")

        tk.Label(f, text="Página web de Ojo de Marte", bg=BG, fg=TEXT, font=("Segoe UI", 10, "bold")).pack(anchor="w", pady=(12, 2))
        row = tk.Frame(f, bg=BG); row.pack(fill="x")
        self.web = tk.StringVar(value=cfg["web"])
        ttk.Entry(row, textvariable=self.web, font=("Consolas", 11)).pack(side="left", fill="x", expand=True)
        ttk.Button(row, text="Probar", command=self.test).pack(side="left", padx=(6, 0))
        self.test_msg = tk.Label(f, text="Si la web cambia de nombre o de lugar, escribe aquí la nueva dirección.", bg=BG, fg=MUTED, anchor="w", justify="left")
        self.test_msg.pack(fill="x")

        row = tk.Frame(f, bg=BG); row.pack(fill="x", pady=(8, 0))
        tk.Label(row, text="Puerto para el Psion", bg=BG, fg=TEXT).pack(side="left")
        self.port = tk.StringVar(value=str(cfg["port"]))
        ttk.Entry(row, textvariable=self.port, width=7, font=("Consolas", 11)).pack(side="left", padx=6)
        self.window = tk.BooleanVar(value=bool(cfg["window"]))
        ttk.Checkbutton(f, text="Abrir la web en una ventana para planear ahí (se sincroniza con el Psion en vivo)",
                        variable=self.window).pack(anchor="w", pady=(6, 0))

        row = tk.Frame(f, bg=BG); row.pack(fill="x", pady=(10, 0))
        ttk.Button(row, text="Guardar e iniciar", style="Go.TButton", command=self.start).pack(side="left")
        ttk.Button(row, text="Detener", command=self.stop).pack(side="left", padx=6)
        ttk.Button(row, text="Abrir la web en tu navegador", command=lambda: webbrowser.open(relay.normalize_web(self.web.get()))).pack(side="left")

        box = tk.Frame(f, bg=PANEL, padx=12, pady=8, highlightbackground="#303737", highlightthickness=1)
        box.pack(fill="x", pady=(12, 6))
        tk.Label(box, text="EN EL PSION (Mars OS): AJUSTES > Conexión: Relay > Dirección del relay:", bg=PANEL, fg=MUTED).pack(anchor="w")
        self.addr = tk.Label(box, text="", bg=PANEL, fg=ORANGE, font=("Consolas", 20, "bold"))
        self.addr.pack(anchor="w")
        self.others = tk.Label(box, text="", bg=PANEL, fg=MUTED, anchor="w", justify="left")
        self.others.pack(fill="x")
        self.status = tk.Label(box, text="", bg=PANEL, fg=CYAN, anchor="w", justify="left")
        self.status.pack(fill="x")

        self.log = scrolledtext.ScrolledText(f, height=8, bg="#0b0e0e", fg=MUTED, insertbackground=TEXT, font=("Consolas", 9), relief="flat")
        self.log.pack(fill="both", expand=True)
        self.show_addr()
        root.protocol("WM_DELETE_WINDOW", self.quit)
        root.after(200, self.pump)
        root.after(1500, self.tick)
        self.start()

    def show_addr(self):
        main = relay.lan_ip()
        self.addr.config(text=f"{main}:{self.port.get().strip() or 8790}")
        try:                                             # (a VPN can come first: the Psion needs the Wi-Fi one)
            ips = sorted({a[4][0] for a in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET)} - {main, "127.0.0.1"})
        except OSError:
            ips = []
        self.others.config(text=("Si no funciona, prueba otra dirección de este PC: " + ", ".join(ips)) if ips else "")

    def write(self, line):
        self.log.insert("end", line.rstrip() + "\n")
        self.log.see("end")

    def save(self):
        try:
            port = int(self.port.get())
        except ValueError:
            port = 8790
        cfg = {"web": relay.normalize_web(self.web.get()), "port": port, "window": bool(self.window.get())}
        relay.save_config(cfg)
        self.web.set(cfg["web"]); self.port.set(str(port))
        return cfg

    def start(self):
        self.stop()
        cfg = self.save()
        if not cfg["web"]:
            self.status.config(text="Escribe la dirección de la web.", fg=RED)
            return
        flags = 0x08000000 if os.name == "nt" else 0       # (no black console window)
        self.proc = subprocess.Popen([python_exe(), "-u", os.path.join(HERE, "mars_relay.py")], cwd=HERE, stdout=subprocess.PIPE,
                                     stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace", creationflags=flags)
        threading.Thread(target=self.read, args=(self.proc,), daemon=True).start()
        self.show_addr()
        self.status.config(text="Iniciando: abriendo la web en Edge...", fg=CYAN)

    def read(self, proc):
        for line in proc.stdout:
            self.lines.put(line)
        self.lines.put("(relay detenido)\n")

    def stop(self):
        if self.proc and self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(5)
            except subprocess.TimeoutExpired:
                self.proc.kill()
        self.proc = None
        self.status.config(text="Relay detenido.", fg=MUTED)

    def pump(self):
        while not self.lines.empty():
            self.write(self.lines.get())
        self.root.after(200, self.pump)

    def tick(self):
        if self.proc and self.proc.poll() is None:
            def ask():
                try:
                    with urllib.request.urlopen(f"http://127.0.0.1:{int(self.port.get())}/mr/hello", timeout=2) as r:
                        d = json.load(r)
                    if d.get("webOk"):
                        t, c = f"Listo: la web responde y tiene el gancho del Psion. Plan número {d.get('plan')}.", GREEN
                    else:
                        t, c = d.get("error") or "Esperando a la web...", RED if d.get("error") and "Abriendo" not in d["error"] else CYAN
                except (OSError, ValueError):
                    t, c = "Iniciando...", CYAN
                self.root.after(0, lambda: self.status.config(text=t, fg=c))
            threading.Thread(target=ask, daemon=True).start()
        self.root.after(2000, self.tick)

    def test(self):
        url = relay.normalize_web(self.web.get())
        self.web.set(url)
        self.test_msg.config(text="Probando " + url + " ...", fg=CYAN)

        def run():
            parts, color = [], GREEN
            try:
                with urllib.request.urlopen(url + "api/health", timeout=20) as r:
                    h = json.load(r)
                parts.append("El servidor responde" + (" y tiene el buzón del Psion." if h.get("psion") else " (sin buzón del Psion: los códigos no funcionarán)."))
            except (OSError, ValueError) as e:
                parts.append(f"No responde: {getattr(e, 'reason', e)}"); color = RED
            if color == GREEN:
                try:
                    with urllib.request.urlopen(url + "web/psion.js", timeout=20) as r:
                        hook = b"window.ojoDeMarte" in r.read()
                    parts.append("Gancho del Psion: sí." if hook else "No encontré el gancho del Psion en web/psion.js (se comprueba al iniciar).")
                except (OSError, ValueError):
                    parts.append("El gancho del Psion se comprueba al iniciar el relay.")
            self.root.after(0, lambda: self.test_msg.config(text=" ".join(parts), fg=color))
        threading.Thread(target=run, daemon=True).start()

    def quit(self):
        self.stop()
        self.root.destroy()


if __name__ == "__main__":
    root = tk.Tk()
    Panel(root)
    root.mainloop()
