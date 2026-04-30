#!/usr/bin/env python3
"""
Tabling Demo Dashboard
=====================================================
simulated sensor/mission readout for ECE senior design tabling events.
all values are procedurally generated.

How to run:
    cd ~/path/to/project
    source venv/bin/activate
    python demo_gui.py

Press ESC or Q to quit.
"""

import tkinter as tk
from tkinter import font as tkfont
import random
import time
from datetime import datetime
from collections import deque

# ─── Palette ──────────────────────────────────────────────────────────────────
BG      = "#0d0f14"
BG_PNL  = "#111620"
BORDER  = "#232d3c"
FG      = "#b8c4d8"
CYAN    = "#00d4ff"
GREEN   = "#00e676"
YELLOW  = "#ffd060"
RED     = "#ff4050"
DIM     = "#435261"

TICK_MS = 100    # 10 Hz simulation + display
CLK_MS  = 1000   # 1 Hz wall-clock


# ─── Smooth random walk ───────────────────────────────────────────────────────
class Walk:
    """Gaussian random walk with EMA smoothing and soft boundary."""

    def __init__(self, center, spread, alpha=0.12, step_frac=0.06):
        self.center = center
        self.spread = spread
        self.alpha  = alpha
        self.step   = spread * step_frac
        self._raw   = center + random.uniform(-spread * 0.25, spread * 0.25)
        self.value  = self._raw

    def tick(self):
        self._raw += random.gauss(0, self.step)
        delta = self._raw - self.center
        if abs(delta) > self.spread:
            self._raw = self.center + delta * (self.spread / abs(delta)) * 0.95
        self.value = self.alpha * self._raw + (1.0 - self.alpha) * self.value
        return self.value


# ─── Application ─────────────────────────────────────────────────────────────
class Dashboard:
    def __init__(self, root: tk.Tk):
        self.root = root
        self.root.title("EC463 Aerial Sensing System")
        self.root.configure(bg=BG)
        self.root.attributes("-fullscreen", True)
        self.root.bind("<Escape>", lambda _e: self.root.destroy())
        self.root.bind("<q>",      lambda _e: self.root.destroy())
        self.root.bind("<Q>",      lambda _e: self.root.destroy())

        # Fonts — prefer a monospace family, fall back gracefully
        fams = tkfont.families()
        mono = next(
            (f for f in ("DejaVu Sans Mono", "Courier New", "Liberation Mono",
                         "Monospace", "Courier")
             if f in fams),
            "Courier",
        )
        self.f_sm  = (mono, 10)
        self.f_md  = (mono, 12)
        self.f_hdr = (mono, 13, "bold")
        self.f_lg  = (mono, 15, "bold")
        self.f_xl  = (mono, 21, "bold")

        # ── Simulation state ──────────────────────────────────────────────
        self._t0    = time.time()
        self._blink = True

        # BMP390
        self._pressure = Walk(1013.0, 8.0,  alpha=0.08, step_frac=0.04)
        self._temp     = Walk(22.0,   3.0,  alpha=0.06, step_frac=0.04)
        self._phist: deque = deque([1013.0] * 30, maxlen=30)

        # GPS (NEO-6M) — Boston area
        self._lat  = Walk(42.3601,   0.001, alpha=0.04, step_frac=0.03)
        self._lon  = Walk(-71.0589,  0.001, alpha=0.04, step_frac=0.03)
        self._galt = Walk(15.0,      2.0,   alpha=0.06)
        self._hdop = Walk(1.1,       0.3,   alpha=0.06, step_frac=0.05)

        # Camera (Insta360 X5)
        self._batt      = random.uniform(75.0, 95.0)
        self._stor_used = 23.0
        self._stor_tot  = 64.0
        self._cam_state = "STANDBY"
        self._cam_until = 0.0

        # Photo countdown
        self._photo_ivl = random.randint(8, 15)
        self._photo_cd  = float(self._photo_ivl)

        # Iridium (RockBLOCK 9603)
        self._iri_cd    = random.uniform(10.0, 40.0)
        self._iri_ivl   = 45.0
        self._iri_total = 0
        self._iri_ok    = 0
        self._iri_last  = "—"
        self._iri_sig   = Walk(4.0, 1.0, alpha=0.10, step_frac=0.15)

        # Anomaly timer
        self._anomaly_t = time.time() + random.uniform(20.0, 60.0)

        # ── Build UI then start loops ──────────────────────────────────────
        self._build_ui()
        self.root.after(TICK_MS, self._tick)
        self.root.after(CLK_MS,  self._clock)
        self.root.after(500,     self._blink_cb)

    # ── UI Construction ───────────────────────────────────────────────────────

    def _build_ui(self):
        self._build_header()
        self._build_grid()
        self._build_p1_baro()
        self._build_p2_gps()
        self._build_p3_cam()
        self._build_p4_mission()
        self._build_p5_iridium()
        self._build_p6_log()
        self._log("System initialized — all subsystems nominal.", "info")

    def _build_header(self):
        hdr = tk.Frame(self.root, bg=BG_PNL,
                       highlightbackground=BORDER, highlightthickness=1)
        hdr.pack(side="top", fill="x", padx=4, pady=(4, 2))

        tk.Label(hdr, text="◈  EC463 AERIAL SENSING SYSTEM",
                 font=self.f_xl, bg=BG_PNL, fg=CYAN
                 ).pack(side="left", padx=16, pady=8)

        self._lbl_status = tk.Label(hdr, text="●  SYSTEM NOMINAL",
                                    font=self.f_lg, bg=BG_PNL, fg=GREEN)
        self._lbl_status.pack(side="right", padx=20, pady=8)

        self._lbl_clock = tk.Label(hdr, text="", font=self.f_lg,
                                   bg=BG_PNL, fg=FG)
        self._lbl_clock.pack(side="right", padx=30, pady=8)

    def _build_grid(self):
        g = tk.Frame(self.root, bg=BG)
        g.pack(fill="both", expand=True, padx=4, pady=(2, 4))
        g.columnconfigure(0, weight=1, uniform="c")
        g.columnconfigure(1, weight=1, uniform="c")
        for r in range(3):
            g.rowconfigure(r, weight=1, uniform="r")
        self._grid = g

    def _panel(self, row, col, title, sub=""):
        """Return (inner_frame, outer_frame) for a grid cell."""
        outer = tk.Frame(self._grid, bg=BORDER)
        outer.grid(row=row, column=col, padx=3, pady=3, sticky="nsew")
        inner = tk.Frame(outer, bg=BG_PNL)
        inner.pack(fill="both", expand=True, padx=1, pady=1)

        tbar = tk.Frame(inner, bg=BG_PNL)
        tbar.pack(fill="x", padx=8, pady=(6, 2))
        tk.Label(tbar, text=title, font=self.f_hdr, bg=BG_PNL,
                 fg=CYAN, anchor="w").pack(side="left")
        if sub:
            tk.Label(tbar, text=sub, font=self.f_sm, bg=BG_PNL,
                     fg=DIM).pack(side="right")
        tk.Frame(inner, bg=BORDER, height=1).pack(fill="x", padx=8,
                                                   pady=(0, 4))
        return inner, outer

    def _kv(self, parent, label, unit="", lw=18):
        """A label : value unit row; returns the value Label widget."""
        f = tk.Frame(parent, bg=BG_PNL)
        f.pack(fill="x", padx=10, pady=1)
        tk.Label(f, text=label + ":", font=self.f_sm, bg=BG_PNL,
                 fg=DIM, width=lw, anchor="w").pack(side="left")
        val = tk.Label(f, text="—", font=self.f_md, bg=BG_PNL,
                       fg=FG, anchor="w")
        val.pack(side="left")
        if unit:
            tk.Label(f, text=" " + unit, font=self.f_sm, bg=BG_PNL,
                     fg=DIM).pack(side="left")
        return val

    # ── Panel 1: Barometer ────────────────────────────────────────────────────
    def _build_p1_baro(self):
        inner, _ = self._panel(0, 0, "BAROMETER / ENVIRONMENTAL",
                               "BMP390 via I²C")
        self._lp = self._kv(inner, "Pressure",    "hPa")
        self._lt = self._kv(inner, "Temperature", "°C")
        self._la = self._kv(inner, "Altitude",    "m")

        tk.Label(inner, text="Pressure history (30 s)", font=self.f_sm,
                 bg=BG_PNL, fg=DIM).pack(anchor="w", padx=10, pady=(6, 0))
        self._sparkcv = tk.Canvas(inner, height=56, bg=BG, bd=0,
                                  highlightthickness=0)
        self._sparkcv.pack(fill="x", padx=10, pady=(2, 8))

    def _draw_sparkline(self, values):
        c = self._sparkcv
        c.delete("all")
        w, h = c.winfo_width(), c.winfo_height()
        if w < 12 or len(values) < 2:
            return
        mn, mx = min(values), max(values)
        rng    = (mx - mn) or 1.0
        n, pad = len(values), 4

        def xy(i, v):
            px = int(i / (n - 1) * (w - pad * 2)) + pad
            py = int((1.0 - (v - mn) / rng) * (h - pad * 2)) + pad
            return px, py

        pts = [xy(i, v) for i, v in enumerate(values)]

        # Horizontal grid lines
        for k in range(1, 4):
            yg = int(h * k / 4)
            c.create_line(0, yg, w, yg, fill=BORDER, dash=(3, 6))

        # Filled area with stipple (Tkinter has no alpha, stipple fakes it)
        poly = pts + [(pts[-1][0], h), (pts[0][0], h)]
        c.create_polygon(poly, fill=CYAN, outline="", stipple="gray12")

        # Line on top
        for i in range(len(pts) - 1):
            c.create_line(pts[i][0], pts[i][1],
                          pts[i+1][0], pts[i+1][1],
                          fill=CYAN, width=2)

    # ── Panel 2: GPS ──────────────────────────────────────────────────────────
    def _build_p2_gps(self):
        inner, _ = self._panel(0, 1, "GPS NAVIGATION", "NEO-6M via UART3")
        self._gps_lat  = self._kv(inner, "Latitude",   "°N")
        self._gps_lon  = self._kv(inner, "Longitude",  "°W")
        self._gps_alt  = self._kv(inner, "Altitude",   "m")
        self._gps_fix  = self._kv(inner, "Fix Status", "")
        self._gps_sats = self._kv(inner, "Satellites", "")
        self._gps_hdop = self._kv(inner, "HDOP",       "")

    # ── Panel 3: Camera ───────────────────────────────────────────────────────
    def _build_p3_cam(self):
        inner, _ = self._panel(1, 0, "INSTA360 X5 CAMERA",
                               "Insta360 X5 via USB")
        self._cam_batt_lbl  = self._kv(inner, "Battery",       "%")
        self._cam_stor_lbl  = self._kv(inner, "Storage",       "")
        self._cam_state_lbl = self._kv(inner, "Camera Status", "")

        brow = tk.Frame(inner, bg=BG_PNL)
        brow.pack(fill="x", padx=10, pady=(4, 4))
        tk.Label(brow, text="Storage Fill:", font=self.f_sm, bg=BG_PNL,
                 fg=DIM, width=18, anchor="w").pack(side="left")
        self._stor_bar = tk.Canvas(brow, height=14, bg=BG, bd=0,
                                   highlightthickness=1,
                                   highlightbackground=BORDER)
        self._stor_bar.pack(side="left", fill="x", expand=True,
                            padx=(0, 10))

    def _draw_stor_bar(self, used, total):
        c = self._stor_bar
        c.delete("all")
        w, h = c.winfo_width(), c.winfo_height()
        if w < 4:
            return
        pct = used / total
        fw  = int(pct * (w - 2))
        col = GREEN if pct < 0.60 else YELLOW if pct < 0.85 else RED
        if fw > 0:
            c.create_rectangle(1, 1, fw + 1, h - 1, fill=col, outline="")

    # ── Panel 4: Mission Timer ────────────────────────────────────────────────
    def _build_p4_mission(self):
        inner, self._p4_out = self._panel(1, 1, "MISSION TIMER",
                                          "Photo Interval Controller")
        self._mis_elapsed = self._kv(inner, "Mission Elapsed", "")
        self._mis_photo   = self._kv(inner, "Next Photo In",   "")
        self._mis_ivl     = self._kv(inner, "Interval Mode",   "")

    # ── Panel 5: Iridium ──────────────────────────────────────────────────────
    def _build_p5_iridium(self):
        inner, self._p5_out = self._panel(2, 0, "IRIDIUM HEARTBEAT",
                                          "RockBLOCK 9603 via Serial")
        self._iri_next_lbl = self._kv(inner, "Next TX In",      "")
        self._iri_last_lbl = self._kv(inner, "Last TX",         "")
        self._iri_succ_lbl = self._kv(inner, "TX Success",      "")
        self._iri_sig_lbl  = self._kv(inner, "Signal Strength", "")

    # ── Panel 6: Event Log ────────────────────────────────────────────────────
    def _build_p6_log(self):
        inner, _ = self._panel(2, 1, "EVENT LOG", "System Events")
        self._evlog = tk.Text(
            inner,
            font=self.f_sm,
            bg=BG_PNL, fg=FG,
            bd=0, highlightthickness=0,
            state="disabled",
            wrap="word",
            cursor="",
        )
        self._evlog.pack(fill="both", expand=True, padx=8, pady=(0, 6))
        self._evlog.tag_configure("info",    foreground=FG)
        self._evlog.tag_configure("warn",    foreground=YELLOW)
        self._evlog.tag_configure("iridium", foreground=CYAN)
        self._evlog.tag_configure("photo",   foreground=GREEN)

    def _log(self, msg: str, tag: str = "info"):
        ts = datetime.now().strftime("%H:%M:%S")
        self._evlog.config(state="normal")
        self._evlog.insert("1.0", f"[{ts}] {msg}\n", tag)
        line_count = int(self._evlog.index("end-1c").split(".")[0])
        if line_count > 20:
            self._evlog.delete("21.0", "end")
        self._evlog.config(state="disabled")

    # ── Helpers ───────────────────────────────────────────────────────────────

    def _flash(self, outer: tk.Frame, color: str, duration: int = 280):
        outer.config(bg=color)
        self.root.after(duration, lambda: outer.config(bg=BORDER))

    def _clock(self):
        self._lbl_clock.config(
            text=datetime.now().strftime("%Y-%m-%d  %H:%M:%S"))
        self.root.after(CLK_MS, self._clock)

    def _blink_cb(self):
        self._blink = not self._blink
        self._lbl_status.config(fg=GREEN if self._blink else BG_PNL)
        self.root.after(500, self._blink_cb)

    # ── Main simulation + display tick (10 Hz) ────────────────────────────────
    def _tick(self):
        dt  = TICK_MS / 1000.0
        now = time.time()

        # BMP390
        press    = self._pressure.tick()
        temp     = self._temp.tick()
        self._phist.append(press)
        alt_baro = 44330.0 * (1.0 - (press / 1013.25) ** 0.190284)

        # GPS
        lat  = self._lat.tick()
        lon  = self._lon.tick()
        galt = self._galt.tick()
        hdop = max(0.5, min(2.5, self._hdop.tick()))

        # Camera battery drain (~0.05%/s → zero in ~33 min)
        self._batt      -= 0.05 * dt
        self._batt       = max(0.0, self._batt)
        # Background storage trickle
        self._stor_used += 0.00015 * dt
        self._stor_used  = min(self._stor_used, self._stor_tot)

        # Camera state machine: CAPTURING → WRITING → STANDBY
        if now >= self._cam_until:
            if self._cam_state == "CAPTURING":
                self._cam_state = "WRITING"
                self._cam_until = now + random.uniform(1.5, 3.5)
            elif self._cam_state == "WRITING":
                self._cam_state = "STANDBY"
                self._cam_until = now + 99999.0

        # Photo countdown
        self._photo_cd -= dt
        if self._photo_cd <= 0.0:
            self._photo_cd   = float(self._photo_ivl)
            self._photo_ivl  = random.randint(8, 15)
            self._cam_state  = "CAPTURING"
            self._cam_until  = now + random.uniform(0.6, 1.4)
            self._stor_used += random.uniform(0.015, 0.040)
            self._stor_used  = min(self._stor_used, self._stor_tot)
            self._flash(self._p4_out, "#003a00", 280)
            self._log("PHOTO CAPTURED — saved to camera storage", "photo")

        # Iridium countdown
        self._iri_cd -= dt
        if self._iri_cd <= 0.0:
            self._iri_cd     = self._iri_ivl
            self._iri_total += 1
            self._iri_ok    += 1
            mid = f"{random.randint(0, 999999):06d}"
            self._iri_last  = datetime.now().strftime("%H:%M:%S")
            self._flash(self._p5_out, "#001133", 280)
            self._log(f"IRIDIUM TX OK — MO:{mid}", "iridium")

        # Occasional sensor anomaly events
        if now >= self._anomaly_t:
            choices = [
                "BMP390: pressure spike filtered",
                "BMP390: temperature self-heat correction applied",
                "GPS: fix quality degraded momentarily",
                "GPS: clock drift +2 ms corrected",
                "CAM: USB heartbeat delayed 120 ms",
                "SYS: watchdog ping OK",
            ]
            self._log(random.choice(choices), "warn")
            self._anomaly_t = now + random.uniform(30.0, 90.0)

        # ── Refresh all panel labels ───────────────────────────────────────

        # Panel 1 — Barometer
        self._lp.config(text=f"{press:.2f}")
        self._lt.config(text=f"{temp:.1f}")
        self._la.config(text=f"{alt_baro:.1f}")
        self._draw_sparkline(list(self._phist))

        # Panel 2 — GPS
        self._gps_lat.config(text=f"{abs(lat):.6f}")
        self._gps_lon.config(text=f"{abs(lon):.6f}")
        self._gps_alt.config(text=f"{galt:.1f}")
        self._gps_fix.config(text="3D Fix", fg=GREEN)
        self._gps_sats.config(text="8")
        hdop_col = GREEN if hdop < 1.2 else YELLOW if hdop < 1.8 else RED
        self._gps_hdop.config(text=f"{hdop:.2f}", fg=hdop_col)

        # Panel 3 — Camera
        bc = GREEN if self._batt > 50 else YELLOW if self._batt > 20 else RED
        self._cam_batt_lbl.config(text=f"{self._batt:.1f}", fg=bc)
        self._cam_stor_lbl.config(
            text=f"{self._stor_used:.2f} / {self._stor_tot:.0f} GB")
        sc = {"STANDBY": FG, "CAPTURING": YELLOW, "WRITING": CYAN}
        self._cam_state_lbl.config(text=self._cam_state,
                                   fg=sc.get(self._cam_state, FG))
        self._draw_stor_bar(self._stor_used, self._stor_tot)

        # Panel 4 — Mission Timer
        elapsed = int(now - self._t0)
        hh, rem = divmod(elapsed, 3600)
        mm, ss  = divmod(rem, 60)
        self._mis_elapsed.config(text=f"{hh:02d}:{mm:02d}:{ss:02d}")
        self._mis_photo.config(text=f"{max(0.0, self._photo_cd):.1f} s")
        self._mis_ivl.config(text=f"~{self._photo_ivl} s")

        # Panel 5 — Iridium
        iri_mm, iri_ss = divmod(int(max(0.0, self._iri_cd)), 60)
        self._iri_next_lbl.config(text=f"{iri_mm:02d}:{iri_ss:02d}")
        self._iri_last_lbl.config(text=self._iri_last)
        self._iri_succ_lbl.config(
            text=(f"{self._iri_ok}/{self._iri_total}"
                  if self._iri_total else "—"))
        sig     = max(1, min(5, round(self._iri_sig.tick())))
        bars    = "█" * sig + "░" * (5 - sig)
        sig_col = RED if sig <= 2 else YELLOW if sig == 3 else GREEN
        self._iri_sig_lbl.config(text=bars, fg=sig_col)

        self.root.after(TICK_MS, self._tick)


# ─── Entry point ─────────────────────────────────────────────────────────────
if __name__ == "__main__":
    root = tk.Tk()
    Dashboard(root)
    root.mainloop()
