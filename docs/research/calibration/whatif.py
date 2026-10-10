"""Validación numérica de propuestas: aplica transformaciones de color post-proceso sobre el render v5
(luz lineal sRGB, Rec.709) y vuelve a medir. NO modifica el motor: es una predicción de primer orden.
Operadores: (A) exposición +EV, (B) curva de grado de grade_fit.json (gain/gamma/lift en Y),
(C) balance cálido por ganancia RGB, (D) saturación por croma respecto a luma."""
import json, os
import numpy as np
from PIL import Image
from calib_lib import srgb_to_linear, linear_to_srgb
from measure import measure

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = "/home/user/Tefra/docs/captures/m2-valley-v5-low-taau.png"
G = json.load(open(os.path.join(HERE, "grade_fit.json")))["m2-valley-v5-low-taau"]
rgb = np.asarray(Image.open(SRC).convert("RGB")).astype(np.float32) / 255.0
lin = srgb_to_linear(rgb)


def luma(x):
    return 0.2126 * x[..., 0] + 0.7152 * x[..., 1] + 0.0722 * x[..., 2]


def to_u8(x):
    return np.round(linear_to_srgb(np.clip(x, 0, 1)) * 255).astype(np.uint8)


def expo(x, ev):
    return x * (2.0 ** ev)


def curve_Y(x, gain, gamma, lift):
    """Aplica la curva sobre la luma y reescala los canales (conserva el tinte)."""
    Y = luma(x)
    Yn = np.maximum(lift + gain * np.power(np.maximum(Y, 1e-6), gamma), 0)
    k = np.where(Y > 1e-6, Yn / np.maximum(Y, 1e-6), 0)[..., None]
    return x * k


def warm(x, gr, gg, gb):
    return x * np.array([gr, gg, gb], dtype=np.float32)


def sat(x, s):
    Y = luma(x)[..., None]
    return Y + s * (x - Y)


def sharpen(x, amt):
    """Enfoque tipo unsharp con caja 3x3 en luz lineal (como un CAS suave)."""
    pad = np.pad(x, ((1, 1), (1, 1), (0, 0)), mode="edge")
    blur = sum(pad[i:i + x.shape[0], j:j + x.shape[1]] for i in range(3) for j in range(3)) / 9.0
    return np.maximum(x + amt * (x - blur), 0)


base = curve_Y(lin, G["gain"], G["gamma"], G["lift"])
cases = {
    "v5 actual": lin,
    "A: +1.2 EV": expo(lin, 1.2),
    "B: curva fit (gain,gamma,lift)": curve_Y(lin, G["gain"], G["gamma"], G["lift"]),
    "C: B + cálido (1.04,1.00,0.95)": warm(curve_Y(lin, G["gain"], G["gamma"], G["lift"]), 1.04, 1.00, 0.95),
    "D: C + saturación x1.6": sat(warm(curve_Y(lin, G["gain"], G["gamma"], G["lift"]), 1.04, 1.00, 0.95), 1.6),
    "E: B + cálido (1.03,1.00,0.97) + sat x1.35": sat(warm(base, 1.03, 1.00, 0.97), 1.35),
    "F: E + enfoque 0.5": sharpen(sat(warm(base, 1.03, 1.00, 0.97), 1.35), 0.5),
    "G: E + enfoque 1.0": sharpen(sat(warm(base, 1.03, 1.00, 0.97), 1.35), 1.0),
}
keys = [("L_p50", "L* P50", "{:.1f}"), ("L_p95", "L* P95", "{:.1f}"), ("L_p99", "L* P99", "{:.1f}"),
        ("DR_stops_p01_p99", "DR P1-P99", "{:.2f}"), ("lc_std_median", "contraste local", "{:.2f}"),
        ("global_C", "C* global", "{:.1f}"), ("mid_b", "b* medios", "{:.2f}"), ("sh_b", "b* sombras", "{:.2f}"),
        ("haze_frac_lowtex", "fracción textura muy baja", "{:.3f}"), ("band_256-512", "alta frec. 256-512", "{:.5f}")]
rows = {}
for name, x in cases.items():
    rows[name] = measure(to_u8(x))
lines = ["| Métrica | " + " | ".join(cases) + " |", "|---|" + "---|" * len(cases)]
for k, lab, fmt in keys:
    lines.append(f"| {lab} | " + " | ".join(fmt.format(rows[n][k]) for n in cases) + " |")
txt = "\n".join(lines) + "\n"
print(txt)
open(os.path.join(HERE, "whatif.md"), "w").write(txt)
json.dump(rows, open(os.path.join(HERE, "whatif.json"), "w"), indent=1)
