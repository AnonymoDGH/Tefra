"""Builds the rainy-night city props with Blender's Python API (bpy) and exports them as .glb.

All geometry and materials are made here procedurally (own work, CC0 like the rest of the generated assets):
traffic lights, neon signs (glyph outlines of Blender's built-in font swept into tubes), bollards, parking meters
and awnings. Buildings come from make_city_buildings.py, cars from make_city_cars.py and the Poly Haven street
furniture from make_city_ph_props.py.
Units: metres, Blender Z-up; the glTF exporter converts to +Y up. A prop's front is Blender +Y, which becomes
glTF -Z after export. Every prop's origin is its ground contact point (or the wall point stated below).

Light sources use emissive materials (Emission Colour + Strength 1.0) so the engine shows them glowing.

Usage (Blender 4.2+ or the `bpy` wheel):
  python3 tools/blender/make_city_props.py [out_dir]
Default out_dir: apps/studio/resources/models/city
"""
import math
import os
import sys

import bpy
import bmesh  # noqa: E402 (after bpy, which sets up the module path)
from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "..", "apps", "studio", "resources", "models",
                                                         "city")
D90 = math.pi / 2


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


def bevel_obj(o, width, segments=2):
    bpy.ops.object.select_all(action="DESELECT")
    o.select_set(True)
    bpy.context.view_layer.objects.active = o
    mod = o.modifiers.new("bevel", "BEVEL")
    mod.width = width
    mod.segments = segments
    mod.limit_method = "ANGLE"
    bpy.ops.object.modifier_apply(modifier=mod.name)


def box(name, size, loc, mat, bevel=0.0, segments=2, rot=(0.0, 0.0, 0.0)):
    bpy.ops.mesh.primitive_cube_add(size=1.0, location=loc)
    o = bpy.context.active_object
    o.name = name
    o.scale = size
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    if any(rot):
        o.rotation_euler = rot
        bpy.ops.object.transform_apply(location=False, rotation=True, scale=False)
    if bevel > 0.0:
        bevel_obj(o, bevel, segments)
    o.data.materials.append(mat)
    return o


def cylinder(name, radius, depth, loc, mat, rot=(0.0, 0.0, 0.0), verts=24, bevel=0.0):
    bpy.ops.mesh.primitive_cylinder_add(vertices=verts, radius=radius, depth=depth, location=loc, rotation=rot)
    o = bpy.context.active_object
    o.name = name
    if bevel > 0.0:
        bevel_obj(o, bevel, 2)
    o.data.materials.append(mat)
    return o


def cone(name, r1, r2, depth, loc, mat, verts=16):
    bpy.ops.mesh.primitive_cone_add(vertices=verts, radius1=r1, radius2=r2, depth=depth, location=loc)
    o = bpy.context.active_object
    o.name = name
    o.data.materials.append(mat)
    return o


def sphere(name, radius, loc, mat, scale=(1.0, 1.0, 1.0), segments=8, rings=6):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=segments, ring_count=rings, radius=radius, location=loc)
    o = bpy.context.active_object
    o.name = name
    o.scale = scale
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    o.data.materials.append(mat)
    return o


def torus(name, major, minor, loc, mat, rot=(0.0, 0.0, 0.0)):
    bpy.ops.mesh.primitive_torus_add(major_radius=major, minor_radius=minor, major_segments=40, minor_segments=8,
                                     location=loc, rotation=rot)
    o = bpy.context.active_object
    o.name = name
    o.data.materials.append(mat)
    return o


def rod(name, a, b, radius, mat, verts=8):
    """Straight cylinder from point a to point b (metres)."""
    a = Vector(a)
    b = Vector(b)
    d = b - a
    bpy.ops.mesh.primitive_cylinder_add(vertices=verts, radius=radius, depth=d.length, location=(a + b) / 2)
    o = bpy.context.active_object
    o.name = name
    o.rotation_mode = "QUATERNION"
    o.rotation_quaternion = Vector((0.0, 0.0, 1.0)).rotation_difference(d)
    o.data.materials.append(mat)
    return o


def curve_tube(name, p0, p1, p2, p3, radius, mat):
    """Cubic Bezier from p0 to p3 (handles p1, p2) swept into a round tube."""
    cu = bpy.data.curves.new(name + "_curve", "CURVE")
    cu.dimensions = "3D"
    cu.resolution_u = 16
    cu.bevel_depth = radius
    cu.bevel_resolution = 4
    cu.use_fill_caps = True
    sp = cu.splines.new("BEZIER")
    sp.bezier_points.add(1)
    a, b = sp.bezier_points[0], sp.bezier_points[1]
    a.co = Vector(p0)
    a.handle_left = Vector(p0)
    a.handle_right = Vector(p1)
    a.handle_left_type = "FREE"
    a.handle_right_type = "FREE"
    b.co = Vector(p3)
    b.handle_left = Vector(p2)
    b.handle_right = Vector(p3)
    b.handle_left_type = "FREE"
    b.handle_right_type = "FREE"
    obj = bpy.data.objects.new(name + "_tmp", cu)
    bpy.context.collection.objects.link(obj)
    obj.data.materials.append(mat)
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.convert(target="MESH")
    o = bpy.context.active_object
    o.name = name
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


def bollard():
    """Steel bollard 0.98 m tall with a red band. Origin: ground at the centre."""
    reset()
    steel = material("BollardSteel", (0.72, 0.72, 0.7), rough=0.4, metal=0.9)
    band = material("BandRed", (0.7, 0.04, 0.03), rough=0.5)
    concrete = material("Concrete", (0.5, 0.5, 0.48), rough=0.9)
    parts = [
        cylinder("Foot", 0.12, 0.04, (0, 0, 0.02), concrete, verts=32, bevel=0.01),
        cylinder("Post", 0.075, 0.9, (0, 0, 0.04 + 0.45), steel, verts=32, bevel=0.01),
        cylinder("Band", 0.078, 0.12, (0, 0, 0.61), band, verts=32),
        sphere("Cap", 0.075, (0, 0, 0.94), steel, scale=(1, 1, 0.5), segments=24, rings=8),
    ]
    join(parts, "bollard")
    export("bollard")


def parking_meter():
    """Pay-and-display meter, 1.36 m tall. Origin: ground at the post foot. Display faces +Y."""
    reset()
    steel = material("MeterSteel", (0.22, 0.24, 0.27), rough=0.4, metal=0.7)
    body = material("MeterBody", (0.55, 0.6, 0.62), rough=0.35, metal=0.2)
    screen = material("MeterScreen", (0.01, 0.02, 0.03), rough=0.08)
    solar = material("MeterSolar", (0.02, 0.03, 0.08), rough=0.2)
    parts = [
        cylinder("Base", 0.12, 0.05, (0, 0, 0.025), steel, verts=32, bevel=0.008),
        cylinder("Post", 0.035, 1.0, (0, 0, 0.55), steel, verts=16),
        box("Head", (0.22, 0.2, 0.3), (0, 0, 1.2), body, bevel=0.03, segments=3),
        box("Screen", (0.16, 0.01, 0.1), (0, 0.105, 1.22), screen, bevel=0.004),
        box("Keypad", (0.1, 0.01, 0.06), (0, 0.105, 1.12), steel),
        box("Solar", (0.2, 0.18, 0.01), (0, 0, 1.355), solar, bevel=0.003),
    ]
    join(parts, "parking_meter")
    export("parking_meter")


# ---- Neon signs (wall-mounted, origin = wall point at the sign centre, sign sticks out along +Y) -------------------
TUBE_Y = 0.178


# ---- Traffic light: pole, signal head with three lenses under hooded visors, back plate, pedestrian unit ---------
def traffic_light(lit="none"):
    """Origin: pole foot at ground. Lamps face +Y: red z=3.7, amber z=3.4, green z=3.1 (m). lit: which lamp glows."""
    reset()
    pole = material("PoleGrey", (0.16, 0.17, 0.17), rough=0.45, metal=0.7)
    housing = material("Housing", (0.02, 0.022, 0.02), rough=0.45)
    plate = material("BackPlate", (0.015, 0.015, 0.015), rough=0.6)
    stripe = material("PlateStripe", (0.8, 0.75, 0.1), rough=0.5)
    off = {"red": (0.18, 0.02, 0.02), "amber": (0.2, 0.1, 0.0), "green": (0.0, 0.13, 0.06)}
    on = {"red": (1.0, 0.07, 0.03), "amber": (1.0, 0.5, 0.02), "green": (0.05, 1.0, 0.45)}
    parts = [
        cylinder("Plinth", 0.15, 0.1, (0, 0, 0.05), pole, verts=32, bevel=0.015),
        cylinder("Pole", 0.065, 3.0, (0, 0, 1.6), pole, verts=24),
        cylinder("Collar", 0.08, 0.06, (0, 0, 2.9), pole, verts=24, bevel=0.01),
        box("Bracket", (0.12, 0.16, 0.08), (0, 0.08, 3.35), pole, bevel=0.01),
        box("Signal", (0.34, 0.26, 1.0), (0, 0.2, 3.4), housing, bevel=0.04, segments=3),
        box("Plate", (0.56, 0.02, 1.22), (0, 0.06, 3.4), plate, bevel=0.03, segments=2),
    ]
    for sx in (-1, 1):
        parts.append(box("Stripe", (0.025, 0.021, 1.16), (sx * 0.255, 0.059, 3.4), stripe))
    for colour, z in (("red", 3.7), ("amber", 3.4), ("green", 3.1)):
        glow = colour == lit
        mat = material("Lamp" + colour.capitalize() + ("On" if glow else "Off"), on[colour] if glow else off[colour],
                       rough=0.15, emission=on[colour] if glow else None)
        parts.append(cylinder("Bezel", 0.125, 0.03, (0, 0.335, z), housing, rot=(D90, 0, 0), verts=32))
        parts.append(sphere("Lens", 0.105, (0, 0.33, z), mat, scale=(1, 0.35, 1), segments=24, rings=12))
        # Hooded visor: the upper half of a thin tube in front of the lens.
        bpy.ops.mesh.primitive_cylinder_add(radius=0.125, depth=0.2, location=(0, 0.44, z), rotation=(D90, 0, 0),
                                            vertices=32, end_fill_type="NOTHING")
        v = bpy.context.active_object
        bm = bmesh.new()
        bm.from_mesh(v.data)
        bmesh.ops.delete(bm, geom=[f for f in bm.faces if f.calc_center_median().z < -0.02], context="FACES")
        bm.to_mesh(v.data)
        bm.free()
        sol = v.modifiers.new("t", "SOLIDIFY")
        sol.thickness = 0.008
        v.data.materials.append(housing)
        parts.append(v)
    # Pedestrian unit on the pole.
    walk = material("WalkMan", (0.9, 0.9, 0.85), rough=0.3, emission=(1.0, 0.95, 0.85) if lit == "green" else None)
    parts += [box("PedBox", (0.26, 0.2, 0.3), (0, 0.13, 2.35), housing, bevel=0.02),
              box("PedFace", (0.2, 0.01, 0.22), (0, 0.235, 2.35), walk)]
    name = "traffic_light" if lit == "none" else "traffic_light_" + lit
    join(parts, name)
    export(name)


# ---- Neon signs: glyph outlines of real text swept into glowing tubes, on a dark backboard ------------------------
NEON = {
    "neon_sign_open": ("OPEN", (1.0, 0.12, 0.45), (0.15, 0.85, 1.0), 0.32),
    "neon_sign_bar": ("BAR", (0.15, 0.85, 1.0), (1.0, 0.12, 0.45), 0.4),
    "neon_sign_hotel": ("HOTEL", (1.0, 0.18, 0.1), None, 0.36),
    "neon_sign_ramen": ("RAMEN", (1.0, 0.55, 0.12), (1.0, 0.12, 0.45), 0.32),
    "neon_sign_24h": ("24H", (0.2, 1.0, 0.4), None, 0.42),
    "neon_sign_pizza": ("PIZZA", (1.0, 0.85, 0.2), (1.0, 0.15, 0.1), 0.32),
    "neon_sign_cafe": ("CAFE", (0.75, 0.35, 1.0), None, 0.36),
    "neon_sign_liquor": ("LIQUOR", (0.2, 0.6, 1.0), (1.0, 0.2, 0.5), 0.28),
}


def neon_text(text, height, mat, y):
    """Outline curves of `text` (Blender's built-in font), height metres, swept into round tubes; reads correctly
    from +Y."""
    cu = bpy.data.curves.new("txt", "FONT")
    cu.body = text
    cu.align_x = "CENTER"
    cu.align_y = "CENTER"
    cu.size = height * 1.38  # cap height of the built-in font is about 0.72 em
    cu.space_character = 1.15
    ob = bpy.data.objects.new("txt", cu)
    bpy.context.scene.collection.objects.link(ob)
    # Text lies in XY; stand it up facing +Y (the viewer at +Y sees it unmirrored).
    ob.rotation_euler = (D90, 0, math.pi)
    ob.location = (0, y, 0)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.object.select_all(action="DESELECT")
    ob.select_set(True)
    bpy.ops.object.convert(target="CURVE")
    c = bpy.context.active_object
    c.data.dimensions = "3D"  # 2D curves would fill the glyphs; 3D sweeps a round tube along the outline
    c.data.fill_mode = "FULL"
    c.data.bevel_depth = 0.011
    c.data.bevel_resolution = 2
    c.data.resolution_u = 4
    bpy.ops.object.convert(target="MESH")
    m = bpy.context.active_object
    m.data.materials.clear()
    m.data.materials.append(mat)
    return m


def neon_sign(name, text, colour, frame_colour, height):
    """Origin: wall point at the board centre; the sign stands 0.12 m off the wall and faces +Y."""
    reset()
    board = material("SignBoard", (0.02, 0.02, 0.022), rough=0.4, metal=0.3)
    standoff = material("Standoff", (0.5, 0.5, 0.52), rough=0.35, metal=1.0)
    tube = material("Neon_" + name, colour, rough=0.25, emission=colour)
    letters = neon_text(text, height, tube, 0.17)
    w = letters.dimensions.x + 0.3
    h = height + 0.26
    parts = [letters, box("Board", (w, 0.05, h), (0, 0.13, 0), board, bevel=0.012)]
    for sx in (-1, 1):
        for sz in (-1, 1):
            parts.append(rod("Standoff", (sx * (w / 2 - 0.08), 0.0, sz * (h / 2 - 0.06)),
                             (sx * (w / 2 - 0.08), 0.11, sz * (h / 2 - 0.06)), 0.015, standoff))
    if frame_colour:
        ring = material("NeonFrame_" + name, frame_colour, rough=0.25, emission=frame_colour)
        hw, hh = w / 2 - 0.05, h / 2 - 0.05
        corners = [(-hw, -hh), (hw, -hh), (hw, hh), (-hw, hh), (-hw, -hh)]
        for (x0, z0), (x1, z1) in zip(corners, corners[1:]):
            parts.append(rod("Frame", (x0, 0.165, z0), (x1, 0.165, z1), 0.01, ring))
    join(parts, name)
    export(name)


def awning():
    """Shop awning 3.0 m wide, 1.2 m projection, fabric in six alternating stripes with a scalloped valance.
    Origin: wall point at the top mounting line (centre). It drops towards -Z and projects along +Y."""
    reset()
    green = material("AwningGreen", (0.04, 0.22, 0.14), rough=0.9)
    cream = material("AwningCream", (0.85, 0.82, 0.72), rough=0.9)
    metal = material("AwningMetal", (0.5, 0.5, 0.52), rough=0.35, metal=1.0)
    ang = -math.atan2(0.55, 1.2)
    slope = math.hypot(1.2, 0.55)
    parts = [box("WallRail", (3.0, 0.05, 0.05), (0, 0.025, 0.0), metal)]
    for i in range(6):
        xc = -1.25 + i * 0.5
        parts.append(box("Canvas", (0.5, slope, 0.012), (xc, 0.6, -0.275), green if i % 2 == 0 else cream,
                         rot=(ang, 0, 0)))
    parts.append(box("Valance", (3.0, 0.012, 0.12), (0, 1.2, -0.61), green))
    for i in range(6):
        parts.append(cylinder("Scallop", 0.08, 0.012, (-1.25 + i * 0.5, 1.2, -0.67), green, rot=(D90, 0, 0), verts=24))
    for sx in (-1, 1):
        parts.append(rod("Arm", (sx * 1.4, 0.03, -0.03), (sx * 1.4, 1.1, -0.5), 0.012, metal))
    join(parts, "awning")
    export("awning")


if __name__ == "__main__":
    for state in ("none", "red", "green"):
        traffic_light(state)
    bollard()
    parking_meter()
    for sign, spec in NEON.items():
        neon_sign(sign, *spec)
    awning()
