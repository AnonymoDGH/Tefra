"""Lee results.json y escribe tables.md: mediana de referencias (con P25-P75) frente a cada render."""
import json, os
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
R = json.load(open(os.path.join(HERE, "results.json")))
refs = {k: v for k, v in R.items() if v["group"] == "ref"}
rends = {k: v for k, v in R.items() if v["group"] == "render"}
rnames = list(rends)


def ref_stat(key):
    x = np.array([v[key] for v in refs.values()], dtype=float)
    return np.nanmedian(x), np.nanpercentile(x, 25), np.nanpercentile(x, 75)


def row(label, key, fmt="{:.1f}"):
    med, q1, q3 = ref_stat(key)
    cells = [fmt.format(med), f"{fmt.format(q1)}–{fmt.format(q3)}"]
    cells += [fmt.format(rends[n][key]) for n in rnames]
    return f"| {label} | " + " | ".join(cells) + " |"


def header():
    h = "| Métrica | Ref. mediana | Ref. P25–P75 | " + " | ".join(rnames) + " |"
    s = "|" + "---|" * (3 + len(rnames))
    return h + "\n" + s


lines = []
lines.append("## A. Histograma de luminancia (L* CIELAB; códigos sRGB 0-255 de luma)\n")
lines.append(header())
for lab, key in [("L* P1", "L_p01"), ("L* P5", "L_p05"), ("L* P50", "L_p50"), ("L* P95", "L_p95"), ("L* P99", "L_p99"), ("L* media", "L_mean")]:
    lines.append(row(lab, key))
for lab, key in [("código luma P1", "code_p01"), ("código luma P50", "code_p50"), ("código luma P99", "code_p99")]:
    lines.append(row(lab, key, "{:.0f}"))
lines.append(row("Rango dinámico P1–P99 (stops, Y lineal)", "DR_stops_p01_p99", "{:.2f}"))

lines.append("\n## B. Contraste local (desv. típica de L* en bloques 32x32)\n")
lines.append(header())
for lab, key in [("mediana bloques", "lc_std_median"), ("media bloques", "lc_std_mean"), ("P90 bloques", "lc_std_p90")]:
    lines.append(row(lab, key, "{:.2f}"))

lines.append("\n## C. Saturación (croma CIE C*) por rango de L* y fracción de píxeles\n")
lines.append(header())
for lo, hi in [(0, 20), (20, 40), (40, 60), (60, 80), (80, 100)]:
    lines.append(row(f"C* L*∈[{lo},{hi})", f"C_L{lo}_{hi}", "{:.1f}"))
for lo, hi in [(0, 20), (20, 40), (40, 60), (60, 80), (80, 100)]:
    lines.append(row(f"fracción L*∈[{lo},{hi})", f"frac_L{lo}_{hi}", "{:.3f}"))
lines.append(row("C* global", "global_C", "{:.1f}"))

lines.append("\n## D. Tono por región (a*, b*, ángulo de tinte en grados; 0 = rojo, 90 = amarillo, 180 = verde/cian azulado, 270 = azul)\n")
lines.append(header())
for name, lab in [("sh", "sombras L*<30"), ("mid", "medios 30-70"), ("hi", "luces L*>70")]:
    lines.append(row(f"{lab}: a*", f"{name}_a", "{:.2f}"))
    lines.append(row(f"{lab}: b*", f"{name}_b", "{:.2f}"))
    lines.append(row(f"{lab}: tinte (grados)", f"{name}_hue_deg", "{:.0f}"))
    lines.append(row(f"{lab}: fracción de píxeles", f"{name}_frac", "{:.3f}"))
lines.append(row("global a*", "global_a", "{:.2f}"))
lines.append(row("global b*", "global_b", "{:.2f}"))

lines.append("\n## E. Cielo y bruma (heurística: textura 9x9 de L* < 1.0)\n")
lines.append(header())
lines.append(row("fracción textura muy baja (bruma+cielo)", "haze_frac_lowtex", "{:.3f}"))
lines.append(row("fracción azulada (b*<-2) con textura baja", "sky_frac_blue", "{:.3f}"))
lines.append(row("fracción brillante (L*>70) con textura baja", "sky_frac_bright", "{:.3f}"))

lines.append("\n## F. Nitidez y energía de alta frecuencia\n")
lines.append(header())
lines.append(row("|Laplaciano| medio de L*", "lap_abs_mean", "{:.3f}"))
lines.append(row("|Laplaciano| P99 de L*", "lap_abs_p99", "{:.2f}"))
lines.append(row("desv. típica del Laplaciano", "lap_std", "{:.3f}"))

lines.append("\n## G. Densidad de detalle: fracción del espectro de L* por banda de octava (ciclos por 1280 px)\n")
lines.append("Periodo en px = 1280 / banda (p. ej. 256-512 ~ 2.5-5 px; 32-64 ~ 20-40 px).\n")
lines.append(header())
for k in ["2-4", "4-8", "8-16", "16-32", "32-64", "64-128", "128-256", "256-512"]:
    lines.append(row(f"banda {k}", f"band_{k}", "{:.4f}"))
lines.append(row("detalle medio 32-256 (suma)", "detail_32_256", "{:.4f}"))
lines.append(row("alta frecuencia 256-512", "detail_256_512", "{:.5f}"))
lines.append(row("baja frecuencia 2-16", "lowfreq_2_16", "{:.4f}"))

lines.append("\n## H. Exposición relativa a la mediana de referencias\n")
lines.append("EV = log2(mediana(Y ref) / mediana(Y render)). Positivo = el render está más oscuro que las referencias. Las referencias son JPG tonemapeados por Poly Haven, así que el EV es orientativo.\n")
ref_Y50 = np.median([v["Y_p50"] for v in refs.values()])
ref_Y95 = np.median([v["Y_p95"] for v in refs.values()])
ref_Y99 = np.median([v["Y_p99"] for v in refs.values()])
lines.append("| Render | EV (mediana) | EV (P95) | EV (P99) | P95 de L* | P99 de L* |")
lines.append("|---|---|---|---|---|---|")
for n in rnames:
    v = rends[n]
    lines.append(f"| {n} | {np.log2(ref_Y50 / v['Y_p50']):+.2f} | {np.log2(ref_Y95 / v['Y_p95']):+.2f} | {np.log2(ref_Y99 / v['Y_p99']):+.2f} | {v['L_p95']:.1f} | {v['L_p99']:.1f} |")
rl95 = np.median([v["L_p95"] for v in refs.values()])
rl99 = np.median([v["L_p99"] for v in refs.values()])
lines.append(f"| *referencia (mediana)* | 0 | 0 | 0 | {rl95:.1f} | {rl99:.1f} |")

out = "\n".join(lines) + "\n"
open(os.path.join(HERE, "tables.md"), "w").write(out)
print(out)
