#!/usr/bin/env python3
"""Downloads the CC0 demo assets from Poly Haven into assets/cache/ (not versioned).

Every Poly Haven asset is CC0 (https://polyhaven.com/license); the rigged characters are Khronos samples under
CC-BY 4.0 (see CHARACTERS). Provenance is written to assets/cache/PROVENANCE.md.
Usage: python3 scripts/fetch_assets.py [--res 1k|2k]
"""
import argparse
import json
import time
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
# Plains (meadow) terrain layers: grassy ground, sparse grass patches (rock and mud are shared with the valley).
PLAINS_LAYERS = ["leafy_grass", "sparse_grass"]
TERRAIN_LAYERS += PLAINS_LAYERS
# Bark: brown conifer bark (valley trunks) and palm trunk bark (palm scene).
TEXTURES = TERRAIN_LAYERS + ["bark_brown_01", "palm_tree_bark"]
API = "https://api.polyhaven.com"
# Rigged characters from the Khronos glTF-Sample-Assets repository (skinning and animation tests, spec M3b). NOT CC0:
# the notices are written to PROVENANCE.md and must be kept with any redistribution (CC-BY 4.0 attribution).
KHRONOS_RAW = "https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/main/Models"
CHARACTERS = {
    "fox": (f"{KHRONOS_RAW}/Fox/glTF-Binary/Fox.glb",
            "Fox (Khronos glTF-Sample-Assets, Models/Fox): model CC0 1.0 by PixelMannen; rig and animations "
            "(Survey, Walk, Run) CC-BY 4.0 by tomkranis; glTF conversion CC-BY 4.0 by @AsoboStudio and @scurest."),
    "cesium_man": (f"{KHRONOS_RAW}/CesiumMan/glTF-Binary/CesiumMan.glb",
                   "CesiumMan (Khronos glTF-Sample-Assets, Models/CesiumMan): CC-BY 4.0 by Cesium."),
}


HEADERS = {"User-Agent": "tefra-engine-asset-fetch/0.1"}


def fetch(url, attempts=5):
    """GET with retries: CDNs answer 502/503/timeouts now and then."""
    for k in range(attempts):
        try:
            with urllib.request.urlopen(urllib.request.Request(url, headers=HEADERS), timeout=60) as r:
                return r.read()
        except Exception as e:  # noqa: BLE001 - retry any transient network error
            if k == attempts - 1:
                raise
            wait = 2 ** (k + 1)
            print(f"   retry in {wait}s after {e}")
            time.sleep(wait)


def get_json(url):
    return json.loads(fetch(url))


def download(url, path):
    if os.path.exists(path):
        return
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = path + ".part"
    data = fetch(url)
    with open(tmp, "wb") as f:
        f.write(data)
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
    for name, (url, notice) in CHARACTERS.items():
        download(url, os.path.join(root, "models", "characters", f"{name}.glb"))
        lines.append(f"- models/characters/{name}.glb: {notice}\n")
    with open(os.path.join(root, "PROVENANCE.md"), "w") as f:
        f.writelines(lines)
    return 0


if __name__ == "__main__":
    sys.exit(main())
