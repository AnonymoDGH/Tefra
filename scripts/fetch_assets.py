#!/usr/bin/env python3
"""Downloads the CC0 demo assets from Poly Haven into assets/cache/ (not versioned).

Every asset is CC0 (https://polyhaven.com/license). Provenance is written to assets/cache/PROVENANCE.md.
Usage: python3 scripts/fetch_assets.py [--res 1k|2k]
"""
import argparse
import json
import os
import sys
import urllib.request

MODELS = ["boulder_01", "dead_tree_trunk", "fern_02", "grass_medium_02", "rock_moss_set_01", "tree_stump_01", "moss_01"]
# Coast scene: shore rocks, a rocky outcrop and a cliff.
MODELS += ["coast_rocks_05", "sand_rocks_small_01", "coast_land_rocks_02", "coastal_cliff_02"]
HDRIS = ["dark_autumn_forest", "forest_slope"]
# Terrain layers: leaf litter, mud with moss, mossy rock, dirt.
TERRAIN_LAYERS = ["forest_leaves_02", "brown_mud_leaves_01", "aerial_rocks_02", "brown_mud_02"]
# Coast terrain layers: dry sand, grassy hinterland, shore rock, wet sand.
COAST_LAYERS = ["coast_sand_01", "aerial_grass_rock", "coast_land_rocks_01", "damp_beach_sand"]
TERRAIN_LAYERS += COAST_LAYERS
# Bark: brown conifer bark (valley trunks) and palm trunk bark (palm scene).
TEXTURES = TERRAIN_LAYERS + ["bark_brown_01", "palm_tree_bark"]
API = "https://api.polyhaven.com"


HEADERS = {"User-Agent": "tefra-engine-asset-fetch/0.1"}


def get_json(url):
    with urllib.request.urlopen(urllib.request.Request(url, headers=HEADERS)) as r:
        return json.load(r)


def download(url, path):
    if os.path.exists(path):
        return
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = path + ".part"
    with urllib.request.urlopen(urllib.request.Request(url, headers=HEADERS)) as r, open(tmp, "wb") as f:
        f.write(r.read())
    os.replace(tmp, path)
    print("  ", os.path.relpath(path))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--res", default="1k")
    args = ap.parse_args()
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets", "cache")
    lines = ["# Provenance (auto-generated)\n", "All assets: Poly Haven, CC0 1.0. Downloaded by scripts/fetch_assets.py.\n\n"]
    for m in MODELS:
        files = get_json(f"{API}/files/{m}")
        entry = files["gltf"][args.res]["gltf"]
        base = os.path.join(root, "models", m)
        download(entry["url"], os.path.join(base, f"{m}.gltf"))
        for rel, inc in entry.get("include", {}).items():
            download(inc["url"], os.path.join(base, rel))
        lines.append(f"- models/{m}: https://polyhaven.com/a/{m} ({args.res})\n")
    for h in HDRIS:
        files = get_json(f"{API}/files/{h}")
        url = files["hdri"][args.res]["hdr"]["url"]
        download(url, os.path.join(root, "hdri", f"{h}.hdr"))
        lines.append(f"- hdri/{h}.hdr: https://polyhaven.com/a/{h} ({args.res})\n")
    for t in TEXTURES:
        files = get_json(f"{API}/files/{t}")
        kinds = [("Diffuse", "diff"), ("nor_gl", "nor_gl"), ("arm", "arm")]
        # Terrain layers also get the height map (packed into the albedo alpha by terrain.cpp).
        if t in TERRAIN_LAYERS:
            kinds.append(("Displacement", "disp"))
        for kind, name in kinds:
            url = files[kind][args.res]["jpg"]["url"]
            download(url, os.path.join(root, "textures", t, f"{t}_{name}.jpg"))
        lines.append(f"- textures/{t}: https://polyhaven.com/a/{t} ({args.res})\n")
    with open(os.path.join(root, "PROVENANCE.md"), "w") as f:
        f.writelines(lines)
    return 0


if __name__ == "__main__":
    sys.exit(main())
