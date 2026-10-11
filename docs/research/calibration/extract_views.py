"""Extrae vistas rectilíneas 1280x720 (HFOV 70 grados, pitch 0) de los panoramas CC0 de Poly Haven.
Los encuadres se eligieron a partir de contact_sheet.py (ver PROVENANCE.md / REPORT.md).
El FOV coincide con la cámara del valle (apps/common/scenes.cpp: fovy 60 grados, 16:9),
es decir HFOV = 2*atan(tan(30 grados) * 16/9) ~= 91.5 grados, para que las frecuencias sean comparables."""
import math
import os
import numpy as np
from PIL import Image
from calib_lib import load_rgb_u8, equirect_view

HFOV = 2 * math.degrees(math.atan(math.tan(math.radians(30)) * 16 / 9))
HERE = os.path.dirname(os.path.abspath(__file__))
REFS = os.path.join(HERE, "refs")
OUT = os.path.join(HERE, "views")
# nombre del panorama -> yaws elegidos (grados)
PLAN = {
    "misty_pines": [0, 270],             # pinos en niebla (referencia principal)
    "phalzer_forest_01": [0, 270],       # pinar seco, luz de día difusa
    "misty_dawn": [0, 270],              # amanecer brumoso, campo
    "spruit_dawn": [0, 270],             # amanecer con niebla baja y árboles
    "je_gray_02": [90, 270],             # sol matinal, sombras largas (eucaliptos, no conífera)
    "kloofendal_misty_morning": [0, 270],  # niebla densa, sin arbolado (referencia de bruma)
    "kloofendal_28d_misty": [0, 270],      # niebla densa, sin arbolado
}
os.makedirs(OUT, exist_ok=True)
for name, yaws in PLAN.items():
    src = load_rgb_u8(os.path.join(REFS, name + ".jpg"))
    for yaw in yaws:
        # supermuestreo 2x + promedio por bloques: evita aliasing al reducir 8K -> 1280 px
        v2 = equirect_view(src, yaw, 0.0, HFOV, 2560, 1440).astype(np.float32)
        v = np.round(v2.reshape(720, 2, 1280, 2, 3).mean(axis=(1, 3))).astype(np.uint8)
        Image.fromarray(v).save(os.path.join(OUT, f"ref_{name}_y{yaw}.png"))
        print("ok", name, yaw)
