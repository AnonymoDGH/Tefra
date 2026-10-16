"""Builds the Backrooms template props with Blender's Python API (bpy) and exports them as .glb.

All geometry and materials are made here procedurally (own work, CC0 like the rest of the generated assets).
Units: metres, Blender Z-up; the glTF exporter converts to +Y up. Each prop's origin is where the template places
it (see apps/studio/src/templates.cpp, build_backrooms).

Usage (Blender 4.2+ or the `bpy` wheel):
  python3 tools/blender/make_backrooms_props.py [out_dir]
Default out_dir: apps/studio/resources/models/backrooms
"""
import math
import os
import sys

import bpy
import bmesh

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "..", "apps", "studio", "resources", "models",
                                                         "backrooms")


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def material(name, color, rough=0.5, metal=0.0, emission=None, emission_strength=1.0):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    b = m.node_tree.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = (*color, 1.0)
    b.inputs["Roughness"].default_value = rough
    b.inputs["Metallic"].default_value = metal
    if emission is not None:
        b.inputs["Emission Color"].default_value = (*emission, 1.0)
        b.inputs["Emission Strength"].default_value = emission_strength
    return m


def box(name, size, loc, mat, bevel=0.0, segments=2):
    bpy.ops.mesh.primitive_cube_add(size=1.0, location=loc)
    o = bpy.context.active_object
    o.name = name
    o.scale = size
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    if bevel > 0.0:
        mod = o.modifiers.new("bevel", "BEVEL")
        mod.width = bevel
        mod.segments = segments
        mod.limit_method = "ANGLE"
        bpy.ops.object.modifier_apply(modifier=mod.name)
    o.data.materials.append(mat)
    return o


def cylinder(name, radius, depth, loc, mat, rot=(0, 0, 0), verts=24):
    bpy.ops.mesh.primitive_cylinder_add(vertices=verts, radius=radius, depth=depth, location=loc, rotation=rot)
    o = bpy.context.active_object
    o.name = name
    o.data.materials.append(mat)
    return o


def smooth(o):
    for p in o.data.polygons:
        p.use_smooth = True


def join(objs, name):
    bpy.ops.object.select_all(action="DESELECT")
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    bpy.ops.object.join()
    o = bpy.context.active_object
    o.name = name
    # The prop's origin is the world origin (the placement point), not the first part's centre.
    bpy.context.scene.cursor.location = (0.0, 0.0, 0.0)
    bpy.ops.object.origin_set(type="ORIGIN_CURSOR")
    return o


def export(name):
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, name + ".glb")
    bpy.ops.export_scene.gltf(filepath=path, export_format="GLB", export_apply=True, export_yup=True,
                              export_materials="EXPORT", export_normals=True, export_tangents=False)
    print("wrote", path, os.path.getsize(path), "bytes")


# ---- Fluorescent troffer: 2 x 4 ft recessed fixture, origin at the ceiling plane, light faces down (-Z) ----------
def troffer():
    reset()
    steel = material("PaintedSteel", (0.86, 0.85, 0.82), rough=0.35, metal=0.2)
    inner = material("Reflector", (0.95, 0.95, 0.93), rough=0.15, metal=0.6)
    lens = material("Diffuser", (1.0, 0.97, 0.88), rough=0.6, emission=(1.0, 0.95, 0.80), emission_strength=1.0)
    w, l, d = 0.60, 1.22, 0.09
    parts = []
    # Flange frame flush with the ceiling.
    rim = 0.025
    parts.append(box("FrameN", (w, rim, 0.012), (0, l / 2 - rim / 2, -0.006), steel, bevel=0.003))
    parts.append(box("FrameS", (w, rim, 0.012), (0, -l / 2 + rim / 2, -0.006), steel, bevel=0.003))
    parts.append(box("FrameE", (rim, l, 0.012), (w / 2 - rim / 2, 0, -0.006), steel, bevel=0.003))
    parts.append(box("FrameW", (rim, l, 0.012), (-w / 2 + rim / 2, 0, -0.006), steel, bevel=0.003))
    # Recessed housing walls (visible at grazing angles) and reflector back.
    parts.append(box("Back", (w - 2 * rim, l - 2 * rim, 0.004), (0, 0, d), inner))
    for sx in (-1, 1):
        o = box("Side", (0.004, l - 2 * rim, d), (sx * (w / 2 - rim), 0, d / 2), inner)
        parts.append(o)
    for sy in (-1, 1):
        parts.append(box("End", (w - 2 * rim, 0.004, d), (0, sy * (l / 2 - rim), d / 2), inner))
    # Prismatic diffuser: a thin emissive lens with a fine pyramid relief (micro facets catch the light).
    bpy.ops.mesh.primitive_grid_add(x_subdivisions=48, y_subdivisions=96, size=1.0, location=(0, 0, 0.012))
    g = bpy.context.active_object
    g.scale = (w - 2 * rim, l - 2 * rim, 1.0)
    bpy.ops.object.transform_apply(scale=True)
    bm = bmesh.new()
    bm.from_mesh(g.data)
    for v in bm.verts:
        u = (v.co.x / (w - 2 * rim) + 0.5) * 48
        t = (v.co.y / (l - 2 * rim) + 0.5) * 96
        v.co.z += 0.0015 * (abs(math.sin(u * math.pi)) * abs(math.sin(t * math.pi)))
    bm.to_mesh(g.data)
    bm.free()
    g.data.materials.append(lens)
    g.name = "Lens"
    # The grid faces up: flip it so the lens faces the room below.
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.mesh.flip_normals()
    bpy.ops.object.mode_set(mode="OBJECT")
    parts.append(g)
    # Two T8 tubes behind the lens (seen as soft bright bands through it).
    tube = material("Tube", (1.0, 0.98, 0.9), rough=0.3, emission=(1.0, 0.96, 0.85), emission_strength=1.0)
    for sx in (-0.12, 0.12):
        parts.append(cylinder("TubeA", 0.013, l - 0.12, (sx, 0, d * 0.55), tube, rot=(math.pi / 2, 0, 0), verts=16))
    join(parts, "Troffer")  # flange flush with the ceiling, housing up into the plenum
    export("troffer")


# ---- Return air vent: 60 x 30 cm ceiling grille with angled slats, origin at the ceiling plane --------------------
def vent():
    reset()
    metal = material("VentMetal", (0.80, 0.80, 0.78), rough=0.45, metal=0.7)
    dark = material("VentDark", (0.03, 0.03, 0.03), rough=0.9)
    w, l = 0.60, 0.30
    parts = [box("Back", (w - 0.04, l - 0.04, 0.005), (0, 0, 0.05), dark)]
    rim = 0.02
    for sy in (-1, 1):
        parts.append(box("RimY", (w, rim, 0.01), (0, sy * (l / 2 - rim / 2), -0.005), metal, bevel=0.003))
    for sx in (-1, 1):
        parts.append(box("RimX", (rim, l, 0.01), (sx * (w / 2 - rim / 2), 0, -0.005), metal, bevel=0.003))
    n = 12
    for i in range(n):
        x = -w / 2 + rim + (i + 0.5) * (w - 2 * rim) / n
        s = box("Slat", (0.003, l - 2 * rim, 0.025), (x, 0, 0.01), metal)
        s.rotation_euler = (0, math.radians(35), 0)
        bpy.ops.object.transform_apply(rotation=True)
        parts.append(s)
    join(parts, "Vent")
    export("vent")


# ---- Wall outlet and light switch: plates on the wall plane, facing +Y (glTF -Z after export: towards the room) ----
def outlet():
    reset()
    plastic = material("PlateIvory", (0.88, 0.85, 0.76), rough=0.35)
    dark = material("Slot", (0.05, 0.04, 0.03), rough=0.8)
    parts = [box("Plate", (0.07, 0.006, 0.115), (0, 0.003, 0), plastic, bevel=0.003, segments=3)]
    for z in (0.024, -0.024):
        parts.append(box("Face", (0.034, 0.004, 0.028), (0, 0.007, z), plastic, bevel=0.004, segments=3))
        for x in (-0.006, 0.006):
            parts.append(box("SlotV", (0.0022, 0.002, 0.008), (x, 0.0095, z + 0.002), dark))
        parts.append(box("Ground", (0.004, 0.002, 0.004), (0, 0.0095, z - 0.008), dark))
    parts.append(cylinder("Screw", 0.0035, 0.002, (0, 0.0075, 0), plastic, rot=(math.pi / 2, 0, 0), verts=12))
    join(parts, "Outlet")
    export("outlet")


def switch():
    reset()
    plastic = material("PlateIvory", (0.88, 0.85, 0.76), rough=0.35)
    parts = [box("Plate", (0.07, 0.006, 0.115), (0, 0.003, 0), plastic, bevel=0.003, segments=3)]
    parts.append(box("Rocker", (0.012, 0.01, 0.024), (0, 0.009, 0.004), plastic, bevel=0.003, segments=3))
    s = parts[-1]
    s.rotation_euler = (math.radians(-12), 0, 0)
    bpy.ops.object.transform_apply(rotation=True)
    for z in (0.045, -0.045):
        parts.append(cylinder("Screw", 0.0032, 0.002, (0, 0.0068, z), plastic, rot=(math.pi / 2, 0, 0), verts=12))
    join(parts, "Switch")
    export("switch")


# ---- Door frame: casing around a 0.9 x 2.1 m opening in a 0.18 m wall; origin at floor centre of the opening -----
def door_frame():
    reset()
    wood = material("CasingPaint", (0.82, 0.78, 0.62), rough=0.55)
    W, H, T, C = 0.90, 2.10, 0.18, 0.07  # opening width, height, wall thickness, casing width
    parts = []
    for side in (-1, 1):
        # Jamb lining inside the opening.
        parts.append(box("Jamb", (0.02, T + 0.004, H), (side * (W / 2 + 0.01), 0, H / 2), wood))
        # Casing on both wall faces, with a rounded edge.
        for face in (-1, 1):
            parts.append(box("Casing", (C, 0.015, H + C), (side * (W / 2 + C / 2), face * (T / 2 + 0.0075),
                                                            (H + C) / 2), wood, bevel=0.005, segments=3))
    parts.append(box("Head", (W + 0.04, T + 0.004, 0.02), (0, 0, H + 0.01), wood))
    for face in (-1, 1):
        parts.append(box("HeadCasing", (W + 2 * C, 0.015, C), (0, face * (T / 2 + 0.0075), H + C / 2), wood,
                         bevel=0.005, segments=3))
    join(parts, "DoorFrame")
    export("door_frame")


# ---- Folding office chair, abandoned in a corner: origin at the floor ---------------------------------------------
def chair():
    reset()
    frame = material("ChairFrame", (0.12, 0.12, 0.13), rough=0.4, metal=0.8)
    seat = material("ChairSeat", (0.22, 0.20, 0.17), rough=0.8)
    parts = []
    for sx in (-0.2, 0.2):
        for sy in (-0.2, 0.2):
            leg = cylinder("Leg", 0.011, 0.46, (sx, sy, 0.23), frame, verts=12)
            leg.rotation_euler = (math.radians(6 if sy > 0 else -6), math.radians(4 if sx > 0 else -4), 0)
            bpy.ops.object.transform_apply(rotation=True)
            parts.append(leg)
    s = box("Seat", (0.44, 0.42, 0.03), (0, 0, 0.47), seat, bevel=0.012, segments=4)
    smooth(s)
    parts.append(s)
    for sx in (-0.2, 0.2):
        parts.append(cylinder("Post", 0.011, 0.42, (sx, 0.21, 0.68), frame, verts=12))
    b = box("Back", (0.44, 0.03, 0.2), (0, 0.22, 0.8), seat, bevel=0.012, segments=4)
    b.rotation_euler = (math.radians(-8), 0, 0)
    bpy.ops.object.transform_apply(rotation=True)
    smooth(b)
    parts.append(b)
    join(parts, "Chair")
    export("chair")


if __name__ == "__main__":
    troffer()
    vent()
    outlet()
    switch()
    door_frame()
    chair()
