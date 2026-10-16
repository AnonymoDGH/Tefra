"""Converts Poly Haven CC0 street props (collection "Hidden Alley") into game-ready .glb files for the rainy city.

For every prop: pick the objects of one variant, put the origin on the ground at the footprint centre, turn the
front towards Blender +Y (glTF -Z, the convention of the city template), decimate to a triangle budget, shrink the
textures and embed them. Light fittings get emissive bulbs and glass.

Source assets: CC0, https://polyhaven.com (see apps/studio/resources/models/city/README.md).

Usage (Blender 4.2+ or the `bpy` wheel, Python 3.13):
  python3 tools/blender/make_city_ph_props.py [kit_dir] [out_dir]
"""
import json
import math
import os
import sys
import urllib.request

import bpy
import bmesh
from mathutils import Matrix, Vector

HERE = os.path.dirname(os.path.abspath(__file__))
KIT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "..", "build", "polyhaven")
OUT = sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, "..", "..", "apps", "studio", "resources", "models",
                                                         "city")


def fetch(name, res="1k"):
    out = os.path.join(KIT, name)
    gltf = os.path.join(out, f"{name}_{res}.gltf")
    if os.path.exists(gltf):
        return gltf
    opener = urllib.request.build_opener()
    opener.addheaders = [("User-Agent", "tefra-asset-fetch/1.0")]
    files = json.load(opener.open(f"https://api.polyhaven.com/files/{name}"))["gltf"][res]["gltf"]
    items = [(files["url"], os.path.basename(files["url"]))] + [(v["url"], k) for k, v in files["include"].items()]
    for url, rel in items:
        p = os.path.join(out, rel)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with opener.open(url) as r, open(p, "wb") as f:
            f.write(r.read())
    return gltf


# out name, asset, keep(object name) -> bool, yaw (deg, applied after centring), scale, triangle budget,
# texture size, emissive {material substring: (r, g, b, strength)}
WARM = (1.0, 0.78, 0.5)
PROPS = [
    ("fire_hydrant", "fire_hydrant", lambda n: "aged" not in n, 0, 1.0, 4000, 512, {}),
    ("trash_bin", "metal_trash_can", lambda n: "rust" not in n, 0, 1.0, 3000, 512, {}),
    ("trash_bin_rusty", "metal_trash_can", lambda n: "rust" in n, 0, 1.0, 3000, 512, {}),
    ("bench", "modular_street_seating",
     lambda n: n in ("crossbar", "legs_double", "legs_single", "suspended_support_01", "back_support_r",
                     "back_support_l", "arm_rest_01", "seat", "seat_back"), 180, 1.0, 5000, 512, {}),
    ("ac_unit", "exterior_aircon_unit", lambda n: "rust" not in n, 180, 1.0, 1200, 512, {}),
    ("manhole", "water_manhole_cover", lambda n: True, 0, 1.0, 1500, 512, {}),
    ("lamp_classic", "street_lamp_01", lambda n: True, 0, 1.15, 7000, 512,
     {"bulb": (1.0, 0.85, 0.6, 1.0), "glass": (1.0, 0.72, 0.42, 0.85)}),
    ("wall_lantern", "street_lamp_02", lambda n: True, 0, 1.0, 3000, 512,
     {"bulb": (1.0, 0.85, 0.6, 1.0), "glass": (1.0, 0.72, 0.42, 0.85)}),
    ("security_light", "security_light", lambda n: True, 180, 1.0, 1500, 256,
     {"bulb": (0.9, 0.95, 1.0, 1.0), "glass": (0.85, 0.92, 1.0, 0.9)}),
    ("utility_box", "utility_box_02", lambda n: True, 180, 1.0, 2500, 512, {}),
    ("road_barrier", "concrete_road_barrier", lambda n: True, 0, 1.0, 2500, 512, {}),
    ("covered_car", "covered_car", lambda n: True, 0, 1.0, 7000, 1024, {}),
    ("barrel_stove", "barrel_stove", lambda n: True, 0, 1.0, 2500, 512, {}),
    ("cardboard_box", "cardboard_box_01", lambda n: True, 0, 1.0, 1200, 256, {}),
    ("wooden_crate", "wooden_crate_02", lambda n: True, 0, 1.0, 1500, 512, {}),
    ("plastic_crate", "plastic_crate_01", lambda n: True, 0, 1.0, 1500, 256, {}),
    ("planter", "planter_box_01", lambda n: True, 0, 1.0, 2500, 512, {}),
    ("tyre", "old_tyre", lambda n: True, 0, 1.0, 1500, 256, {}),
    ("wet_floor_sign", "WetFloorSign_01", lambda n: True, 0, 1.0, 1000, 256, {}),
    ("rollershutter", "rollershutter_door", lambda n: "graffiti" not in n, 180, 1.0, 1000, 512, {}),
    ("rollershutter_graffiti", "rollershutter_door", lambda n: "graffiti" in n, 180, 1.0, 1000, 512, {}),
]


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def decimate(ob, target):
    tris = sum(len(p.vertices) - 2 for p in ob.data.polygons)
    if tris <= target:
        return
    mod = ob.modifiers.new("dec", "DECIMATE")
    mod.ratio = target / tris
    mod.use_collapse_triangulate = True
    bpy.context.view_layer.objects.active = ob
    bpy.ops.object.modifier_apply(modifier=mod.name)


def make_emissive(mat, color, strength):
    mat.blend_method = "OPAQUE"
    nt = mat.node_tree
    bsdf = next(n for n in nt.nodes if n.type == "BSDF_PRINCIPLED")
    for l in list(bsdf.inputs["Alpha"].links):
        nt.links.remove(l)
    bsdf.inputs["Alpha"].default_value = 1.0
    bsdf.inputs["Emission Color"].default_value = (*color, 1.0)
    bsdf.inputs["Emission Strength"].default_value = strength
    bsdf.inputs["Base Color"].default_value = (*color, 1.0)
    for l in list(bsdf.inputs["Base Color"].links):
        nt.links.remove(l)


def shrink_textures(size):
    for img in bpy.data.images:
        if img.size[0] > size:
            img.scale(size, size)


def convert(name, asset, keep, yaw, scale, budget, tex, emissive):
    reset()
    bpy.ops.import_scene.gltf(filepath=fetch(asset))
    meshes = []
    for o in list(bpy.data.objects):
        if o.type == "MESH" and keep(o.name.split(".")[0]):
            meshes.append(o)
        elif o.type == "MESH":
            bpy.data.objects.remove(o)
    # Bake the hierarchy, then join everything into one object.
    for o in meshes:
        mw = o.matrix_world.copy()
        o.parent = None
        o.matrix_world = mw
    bpy.ops.object.select_all(action="DESELECT")
    for o in meshes:
        o.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0]
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    if len(meshes) > 1:
        bpy.ops.object.join()
    ob = bpy.context.view_layer.objects.active
    for o in list(bpy.data.objects):
        if o != ob:
            bpy.data.objects.remove(o)
    # Ground contact at the origin, footprint centred, front to +Y, scaled.
    vs = [v.co for v in ob.data.vertices]
    lo = Vector((min(v.x for v in vs), min(v.y for v in vs), min(v.z for v in vs)))
    hi = Vector((max(v.x for v in vs), max(v.y for v in vs), max(v.z for v in vs)))
    m = Matrix.Diagonal((scale, scale, scale, 1)) @ Matrix.Rotation(math.radians(yaw), 4, "Z") @ \
        Matrix.Translation((-(lo.x + hi.x) / 2, -(lo.y + hi.y) / 2, -lo.z))
    ob.data.transform(m)
    decimate(ob, budget)
    for mat in ob.data.materials:
        for key, (r, g, b, s) in emissive.items():
            if key in mat.name:
                make_emissive(mat, (r, g, b), s)
    shrink_textures(tex)
    tris = sum(len(p.vertices) - 2 for p in ob.data.polygons)
    path = os.path.join(OUT, name + ".glb")
    bpy.ops.export_scene.gltf(filepath=path, export_format="GLB", export_apply=True, export_yup=True,
                              export_image_format="JPEG", export_jpeg_quality=85, export_extras=False)
    size = hi - lo
    print(f"{name}: {tris} tris, {size.x * scale:.2f} x {size.y * scale:.2f} x {size.z * scale:.2f} m, "
          f"{os.path.getsize(path) // 1024} KiB")


def main():
    os.makedirs(OUT, exist_ok=True)
    only = os.environ.get("CITY_ONLY")
    for spec in PROPS:
        if only and spec[0] not in only.split(","):
            continue
        convert(*spec)


if __name__ == "__main__":
    main()
