"""Converts Poly Haven CC0 assets for the lighthouse-in-the-storm template ("Smuggler's Cove" and "Project Lighthouse"
collections): sea cliffs and shore rocks, the modular wooden pier, an anchored sailing ship, harbour clutter and the
keeper's furniture. Same pipeline as make_city_ph_props.py (one variant, ground origin, front to Blender +Y,
decimated, smaller embedded textures).

Usage (Blender 4.2+ or the `bpy` wheel, Python 3.13):
  python3 tools/blender/make_coast_ph_props.py [kit_dir] [out_dir]   (out default apps/studio/resources/models/coast)
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_city_ph_props as ph  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
ph.KIT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "..", "build", "polyhaven")
ph.OUT = sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, "..", "..", "apps", "studio", "resources", "models",
                                                            "coast")
ALL = lambda n: True  # noqa: E731
WARM = {"glass": (1.0, 0.72, 0.4, 0.9)}

# out name, asset, keep(object), yaw, scale, triangle budget, texture size, emissive
PROPS = [
    ("cliff_a", "coastal_cliff_01", ALL, 0, 1.0, 30000, 1024, {}),
    ("cliff_b", "coastal_cliff_02", ALL, 0, 1.0, 26000, 1024, {}),
    ("shore_rocks_a", "coast_rocks_01", ALL, 0, 1.0, 26000, 1024, {}),
    ("shore_rocks_b", "coast_rocks_03", ALL, 0, 1.0, 20000, 1024, {}),
    ("shore_line", "coast_line_01", ALL, 0, 1.0, 24000, 1024, {}),
    ("boulders", "coast_land_rocks_03", ALL, 0, 1.0, 8000, 512, {}),
    ("pier_a", "modular_wooden_pier", lambda n: n == "modular_wooden_pier_section_01", 0, 1.0, 9000, 512, {}),
    ("pier_b", "modular_wooden_pier", lambda n: n == "modular_wooden_pier_section_02", 0, 1.0, 6000, 512, {}),
    ("pier_c", "modular_wooden_pier", lambda n: n == "modular_wooden_pier_section_03", 0, 1.0, 6000, 512, {}),
    ("pier_end", "modular_wooden_pier", lambda n: n == "modular_wooden_pier_section_04", 0, 1.0, 6000, 512, {}),
    ("sailing_ship", "dutch_ship_medium", ALL, 90, 1.0, 40000, 1024, {}),
    ("barrel", "wooden_barrels_01", lambda n: n == "wooden_barrels_01_barrel01", 0, 1.0, 2500, 512, {}),
    ("barrel_b", "wooden_barrels_01", lambda n: n == "wooden_barrels_01_barrel02", 0, 1.0, 2500, 512, {}),
    ("crate", "wooden_crate_01", ALL, 0, 1.0, 1500, 512, {}),
    ("lantern", "wooden_lantern_01", ALL, 0, 1.0, 3000, 512, WARM),
    ("bucket", "wooden_bucket_01", ALL, 0, 1.0, 1500, 256, {}),
    ("lifebuoy", "lifebuoy", ALL, 0, 1.0, 2500, 512, {}),
    ("buoy", "ocean_buoy", ALL, 0, 1.0, 3000, 512, {}),
    ("life_jacket", "life_jacket", ALL, 0, 1.0, 2000, 512, {}),
    ("rubber_boots", "rubber_boots", ALL, 0, 1.0, 2000, 512, {}),
    ("fishermans_hat", "fishermans_hat", ALL, 0, 1.0, 1500, 256, {}),
    ("ladder", "wooden_ladder", ALL, 0, 1.0, 2000, 512, {}),
    ("day_bed", "vintage_day_bed", ALL, 0, 1.0, 2500, 512, {}),
    ("desk", "metal_office_desk", ALL, 180, 1.0, 3000, 512, {}),
    ("stool", "metal_stool_01", ALL, 0, 1.0, 2000, 256, {}),
    ("wall_clock", "wall_clock", ALL, 0, 1.0, 1200, 256, {}),
    ("radio", "vintage_radio_transceiver", lambda n: "plug" not in n, 180, 1.0, 5000, 512, {}),
    ("compass", "seadogs_compass", ALL, 0, 1.0, 2500, 256, {}),
    ("oil_tin", "oil_tin", ALL, 0, 1.0, 1000, 256, {}),
    ("propane_tank", "propane_tank", ALL, 0, 1.0, 1500, 256, {}),
    ("hanging_lamp", "caged_hanging_light", ALL, 0, 1.0, 3000, 256, {}),
    ("wall_sconce", "industrial_caged_sconce",
     lambda n: n.endswith("_a") or any(k in n for k in ("mount", "elbow", "light", "pipe_short")), 180, 1.0, 3000, 256,
     {}),
    ("binoculars", "binoculars", ALL, 0, 1.0, 2500, 256, {}),
    ("metal_rack", "worn_metal_rack", ALL, 0, 1.0, 2500, 512, {}),
    ("treasure_chest", "treasure_chest", ALL, 0, 1.0, 6000, 512, {}),
]


def main():
    os.makedirs(ph.OUT, exist_ok=True)
    only = os.environ.get("COAST_ONLY")
    for spec in PROPS:
        if only and spec[0] not in only.split(","):
            continue
        ph.convert(*spec)


if __name__ == "__main__":
    main()
