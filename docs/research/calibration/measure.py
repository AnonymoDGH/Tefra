"""Mide estadística de imagen en las referencias (views/ref_*.png) y en los renders M2 (docs/captures).
Salida: results.json (por imagen) y tables.md (tablas agregadas).
"""
import glob, json, os
import numpy as np
from PIL import Image
from calib_lib import (srgb_u8_to_lab, local_std, block_std, radial_bands,
                       sobel_like_laplacian, radial_spectrum_slope, W_OUT, H_OUT)

HERE = os.path.dirname(os.path.abspath(__file__))
REFS = sorted(glob.glob(os.path.join(HERE, "views", "ref_*.png")))
RENDERS = [
    ("m2-valley-v1", "/home/user/Tefra/docs/captures/m2-valley-v1.png"),
    ("m2-valley-v2", "/home/user/Tefra/docs/captures/m2-valley-v2.png"),
    ("m2-valley-v3-water", "/home/user/Tefra/docs/captures/m2-valley-v3-water.png"),
    ("m2-valley-v4-taa", "/home/user/Tefra/docs/captures/m2-valley-v4-taa.png"),
    ("m2-valley-v5-low-taau", "/home/user/Tefra/docs/captures/m2-valley-v5-low-taau.png"),
    ("m2-atmosphere-fog", "/home/user/Tefra/docs/captures/m2-atmosphere-fog.png"),
]
L_BINS = [0, 20, 40, 60, 80, 101]
BANDS = [(2, 4), (4, 8), (8, 16), (16, 32), (32, 64), (64, 128), (128, 256), (256, 512)]


def measure(rgb_u8):
    L, a, b, Y = srgb_u8_to_lab(rgb_u8)
    luma_code = 0.2126 * rgb_u8[..., 0] + 0.7152 * rgb_u8[..., 1] + 0.0722 * rgb_u8[..., 2]
    m = {}
    pct = [1, 5, 50, 95, 99]
    Lp = np.percentile(L, pct)
    m["L_p01"], m["L_p05"], m["L_p50"], m["L_p95"], m["L_p99"] = [float(x) for x in Lp]
    m["L_mean"] = float(L.mean())
    Yp = np.percentile(Y, pct) + 1e-4
    m["Y_p01"], m["Y_p05"], m["Y_p50"], m["Y_p95"], m["Y_p99"] = [float(x) for x in Yp]
    m["DR_stops_p01_p99"] = float(np.log2(Yp[4] / Yp[0]))
    cp = np.percentile(luma_code, pct)
    m["code_p01"], m["code_p05"], m["code_p50"], m["code_p95"], m["code_p99"] = [float(x) for x in cp]
    # contraste local: std de L* en bloques 32x32 no solapados
    bs = block_std(L, 32)
    m["lc_std_median"] = float(np.median(bs))
    m["lc_std_mean"] = float(bs.mean())
    m["lc_std_p90"] = float(np.percentile(bs, 90))
    # saturación (croma CIE C*) por rango de luminancia L*
    C = np.sqrt(a * a + b * b)
    for lo, hi in zip(L_BINS[:-1], L_BINS[1:]):
        sel = (L >= lo) & (L < hi)
        frac = float(sel.mean())
        m[f"C_L{lo}_{min(hi,100)}"] = float(C[sel].mean()) if sel.any() else float("nan")
        m[f"frac_L{lo}_{min(hi,100)}"] = frac
    # tono por región: sombras (L<30), medios (30-70), luces (>70)
    for name, sel in [("sh", L < 30), ("mid", (L >= 30) & (L <= 70)), ("hi", L > 70)]:
        m[f"{name}_frac"] = float(sel.mean())
        if sel.any():
            aa, bb = float(a[sel].mean()), float(b[sel].mean())
            m[f"{name}_a"] = aa
            m[f"{name}_b"] = bb
            m[f"{name}_hue_deg"] = float(np.degrees(np.arctan2(bb, aa)) % 360)
            m[f"{name}_C"] = float(C[sel].mean())
        else:
            m[f"{name}_a"] = m[f"{name}_b"] = m[f"{name}_hue_deg"] = m[f"{name}_C"] = float("nan")
    m["global_a"] = float(a.mean())
    m["global_b"] = float(b.mean())
    m["global_C"] = float(C.mean())
    # cielo / bruma: textura muy baja en ventana 9x9 (heurística)
    ls9 = local_std(L, 9)
    smooth = ls9 < 1.0
    m["haze_frac_lowtex"] = float(smooth.mean())
    m["sky_frac_blue"] = float((smooth & (b < -2.0)).mean())
    m["sky_frac_bright"] = float((smooth & (L > 70)).mean())
    # nitidez / alta frecuencia
    lap = sobel_like_laplacian(L)
    m["lap_abs_mean"] = float(np.abs(lap).mean())
    m["lap_std"] = float(lap.std())
    m["lap_abs_p99"] = float(np.percentile(np.abs(lap), 99))
    # energía por bandas de octava (fracción del espectro, ciclos por imagen de 1280 px)
    bands = radial_bands(L, edges=tuple([2, 4, 8, 16, 32, 64, 128, 256, 512]))
    for k, v in bands.items():
        m[f"band_{k}"] = v
    m["spec_slope_4_256"] = radial_spectrum_slope(L, 4, 256)
    m["detail_32_256"] = float(sum(bands[f"{lo}-{hi}"] for lo, hi in [(32, 64), (64, 128), (128, 256)]))
    m["detail_256_512"] = float(bands["256-512"])
    m["lowfreq_2_16"] = float(sum(bands[f"{lo}-{hi}"] for lo, hi in [(2, 4), (4, 8), (8, 16)]))
    return m


def main():
    res = {}
    for p in REFS:
        name = os.path.basename(p)[:-4]
        res[name] = {"group": "ref", **measure(np.asarray(Image.open(p).convert("RGB")))}
        print("ref", name)
    for name, p in RENDERS:
        img = np.asarray(Image.open(p).convert("RGB"))
        assert img.shape[:2] == (H_OUT, W_OUT), img.shape
        res[name] = {"group": "render", **measure(img)}
        print("render", name)
    with open(os.path.join(HERE, "results.json"), "w") as f:
        json.dump(res, f, indent=1)
    print("written results.json")


if __name__ == "__main__":
    main()
