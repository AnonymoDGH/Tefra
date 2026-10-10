"""Genera una hoja de contacto de vistas candidatas (4 yaws) por panorama, para elegir encuadres."""
import os, sys
import numpy as np
from PIL import Image, ImageDraw
from calib_lib import load_rgb_u8, equirect_view

HERE = os.path.dirname(os.path.abspath(__file__))
REFS = os.path.join(HERE, "refs")
OUT = os.path.join(HERE, "views", "contact")
os.makedirs(OUT, exist_ok=True)
names = sys.argv[1:] or sorted(f[:-4] for f in os.listdir(REFS) if f.endswith(".jpg"))
for name in names:
    src = load_rgb_u8(os.path.join(REFS, name + ".jpg"))
    tiles = []
    for yaw in (0, 90, 180, 270):
        v = equirect_view(src, yaw, 0.0, 70.0, 480, 270)
        im = Image.fromarray(v)
        ImageDraw.Draw(im).text((6, 6), f"{name} yaw={yaw}", fill=(255, 0, 0))
        tiles.append(im)
    sheet = Image.new("RGB", (960, 540))
    for i, t in enumerate(tiles):
        sheet.paste(t, ((i % 2) * 480, (i // 2) * 270))
    sheet.save(os.path.join(OUT, name + ".png"))
    print("ok", name)
