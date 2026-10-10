"""Ajusta una curva de gradación out = lift + gain * in^gamma (en luz lineal de visualización, Rec.709 Y)
que lleva los percentiles 1/5/50/95/99 de los renders a los de los subconjuntos de referencia.
Es una aproximación post-tonemapeado; ver REPORT.md para cómo trasladarla a ACEScg."""
import glob, os
import numpy as np
from PIL import Image
from calib_lib import load_rgb_u8, srgb_to_linear

HERE = os.path.dirname(os.path.abspath(__file__))
PCT = [1, 5, 50, 95, 99]


def Y_of(path_or_arr):
    arr = path_or_arr if isinstance(path_or_arr, np.ndarray) else np.asarray(Image.open(path_or_arr).convert("RGB"))
    lin = srgb_to_linear(arr.astype(np.float32) / 255.0)
    return (0.2126 * lin[..., 0] + 0.7152 * lin[..., 1] + 0.0722 * lin[..., 2]).ravel()


def pct(path):
    return np.percentile(Y_of(path), PCT)


def fit(x, t):
    """Busca gamma en rejilla; para cada gamma resuelve lift y gain por mínimos cuadrados."""
    best = None
    for gamma in np.linspace(0.4, 1.6, 241):
        A = np.stack([np.ones_like(x), x ** gamma], axis=1)
        sol, *_ = np.linalg.lstsq(A, t, rcond=None)
        pred = A @ sol
        err = np.sqrt(np.mean((pred - t) ** 2))
        if best is None or err < best[0]:
            best = (err, gamma, sol[0], sol[1], pred)
    return best


def main():
    forest = [os.path.join(HERE, "views", f) for f in sorted(os.listdir(os.path.join(HERE, "views")))
              if f.startswith(("ref_misty_pines", "ref_phalzer", "ref_je_gray", "ref_spruit", "ref_misty_dawn"))]
    # percentiles de referencia: mediana por percentil sobre las vistas de bosque/arbolado
    T = np.median(np.stack([pct(p) for p in forest]), axis=0)
    renders = {
        "m2-valley-v4-taa": "/home/user/Tefra/docs/captures/m2-valley-v4-taa.png",
        "m2-valley-v5-low-taau": "/home/user/Tefra/docs/captures/m2-valley-v5-low-taau.png",
    }
    lines = ["| Render | " + " | ".join(f"Y P{p} render / ref" for p in PCT) + " | EV medio | gain | gamma | lift | RMSE |",
             "|---|" + "---|" * (len(PCT) + 5)]
    out = {"target_Y_percentiles": dict(zip([f"P{p}" for p in PCT], T.round(5).tolist()))}
    for name, path in renders.items():
        R = pct(path)
        err, gamma, lift, gain, pred = fit(R, T)
        ev = float(np.log2(T[2] / R[2]))
        cells = [f"{R[i]:.4f} / {T[i]:.4f}" for i in range(len(PCT))]
        lines.append(f"| {name} | " + " | ".join(cells) + f" | {ev:+.2f} | {gain:.3f} | {gamma:.3f} | {lift:+.4f} | {err:.4f} |")
        out[name] = {"ev_median": ev, "gain": float(gain), "gamma": float(gamma), "lift": float(lift),
                     "rmse": float(err), "render_pct": R.tolist(), "pred_pct": pred.tolist()}
    # Número de vistas de referencia usadas
    txt = f"Vistas de bosque/arbolado usadas: {len(forest)}\n\n" + "\n".join(lines) + "\n"
    print(txt)
    open(os.path.join(HERE, "grade_fit.md"), "w").write(txt)
    import json
    json.dump(out, open(os.path.join(HERE, "grade_fit.json"), "w"), indent=1)


if __name__ == "__main__":
    main()
