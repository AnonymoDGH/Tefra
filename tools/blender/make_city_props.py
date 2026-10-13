"""Builds the rainy-night city props with Blender's Python API (bpy) and exports them as .glb.

All geometry and materials are made here procedurally (own work, CC0 like the rest of the generated assets).
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
from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "..", "apps", "studio", "resources", "models",
                                                         "city")
D90 = math.pi / 2
# Street lamp LED head: centre of the emissive lens, in prop space (metres, origin at the pole foot).
LAMP_HEAD = (0.0, 1.8, 7.16)


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


# ---- Street lamp: 7.4 m tapered galvanised pole, 1.8 m curved arm over the road (+Y), LED head below the arm ------
def street_lamp():
    """Origin: pole foot at ground (centre of the base plate). Arm reaches +Y (towards the road).
    LED head (lens centre): LAMP_HEAD = (0.0, 1.8, 7.16) m. The lens faces down (-Z)."""
    reset()
    galv = material("Galvanized", (0.55, 0.58, 0.6), rough=0.4, metal=1.0)
    paint = material("LampPaint", (0.82, 0.84, 0.86), rough=0.35)
    lens = material("LensLED", (1.0, 0.94, 0.8), rough=0.2, emission=(1.0, 0.93, 0.78))
    H = 7.4
    parts = [
        box("BasePlate", (0.42, 0.42, 0.03), (0, 0, 0.015), galv, bevel=0.012),
        cylinder("Collar", 0.1, 0.22, (0, 0, 0.14), galv, verts=24, bevel=0.01),
        cone("Pole", 0.085, 0.045, H - 0.25, (0, 0, 0.25 + (H - 0.25) / 2), galv, verts=16),
        curve_tube("Arm", (0, 0, H - 0.12), (0, 0.45, 7.5), (0, 1.35, 7.5), (0, 1.8, 7.25), 0.03, galv),
        box("Housing", (0.24, 0.44, 0.07), (0, 1.8, 7.2), paint, bevel=0.02, segments=3),
        box("Lens", (0.2, 0.4, 0.008), (0, 1.8, 7.161), lens),
    ]
    for sx in (-1, 1):
        for sy in (-1, 1):
            parts.append(cylinder("Bolt", 0.014, 0.02, (sx * 0.15, sy * 0.15, 0.04), galv, verts=6))
    o = join(parts, "street_lamp")
    export("street_lamp")
    print("street_lamp: LED head (lens centre) =", LAMP_HEAD, "size", tuple(round(v, 3) for v in o.dimensions))


# ---- Traffic light: 4 m pole, signal box facing +Y with three emissive lamps and visors ------------------------
def traffic_light():
    """Origin: pole foot at ground. Lamps face +Y: LampRed z=3.7, LampAmber z=3.4, LampGreen z=3.1 (m)."""
    reset()
    pole = material("PoleGrey", (0.35, 0.37, 0.38), rough=0.4, metal=0.8)
    housing = material("Housing", (0.05, 0.05, 0.05), rough=0.4)
    visor = material("Visor", (0.02, 0.02, 0.02), rough=0.6)
    red = material("LampRed", (0.8, 0.03, 0.02), rough=0.2, emission=(1.0, 0.06, 0.03))
    amber = material("LampAmber", (0.9, 0.45, 0.0), rough=0.2, emission=(1.0, 0.5, 0.02))
    green = material("LampGreen", (0.0, 0.7, 0.2), rough=0.2, emission=(0.05, 1.0, 0.3))
    parts = [
        cylinder("Plinth", 0.13, 0.08, (0, 0, 0.04), pole, verts=32, bevel=0.01),
        cylinder("Pole", 0.06, 4.0, (0, 0, 2.0), pole, verts=20),
        box("Signal", (0.3, 0.26, 1.0), (0, 0, 3.4), housing, bevel=0.03, segments=2),
    ]
    for lamp_name, mat, z in (("LampRed", red, 3.7), ("LampAmber", amber, 3.4), ("LampGreen", green, 3.1)):
        parts.append(cylinder("Bezel", 0.11, 0.03, (0, 0.12, z), housing, rot=(D90, 0, 0), verts=32))
        parts.append(cylinder(lamp_name, 0.095, 0.03, (0, 0.145, z), mat, rot=(D90, 0, 0), verts=32))
        parts.append(box("Visor", (0.24, 0.14, 0.025), (0, 0.2, z + 0.12), visor, bevel=0.005))
    join(parts, "traffic_light")
    export("traffic_light")


# ---- Cars --------------------------------------------------------------------------------------------------------
def car(name, L, W, cab_len, cab_yc, cab_w, cab_top, wheel_y, paint_rgb, ws_angle, rear_angle):
    """Origin: centre of the footprint at ground level. Front = +Y. Wheels touch z = 0."""
    reset()
    paint = material("Paint", paint_rgb, rough=0.3, metal=0.0)
    glass = material("Glass", (0.02, 0.03, 0.035), rough=0.04)
    trim = material("Trim", (0.04, 0.04, 0.045), rough=0.6)
    tire = material("Tire", (0.02, 0.02, 0.02), rough=0.85)
    rim = material("Rim", (0.72, 0.72, 0.74), rough=0.3, metal=1.0)
    head = material("Headlight", (1.0, 1.0, 0.96), rough=0.2, emission=(1.0, 0.97, 0.9))
    tail = material("TailLight", (0.5, 0.02, 0.02), rough=0.2, emission=(1.0, 0.03, 0.02))
    Z0, Z1 = 0.25, 0.95
    cab_z0 = Z1 - 0.02
    zc = (Z1 + cab_top) / 2
    gh = cab_top - Z1 - 0.2
    hx = W / 2 - 0.3
    parts = [
        box("Body", (W, L, Z1 - Z0), (0, 0, (Z0 + Z1) / 2), paint, bevel=0.15, segments=4),
        box("Cabin", (cab_w, cab_len, cab_top - cab_z0), (0, cab_yc, (cab_z0 + cab_top) / 2), paint, bevel=0.08,
            segments=3),
    ]
    for sx in (-1, 1):
        parts.append(box("SideGlass", (0.01, cab_len * 0.72, gh), (sx * (cab_w / 2 + 0.004), cab_yc, zc), glass))
        parts.append(box("Mirror", (0.14, 0.2, 0.09), (sx * (cab_w / 2 + 0.07), cab_yc + cab_len * 0.3, Z1 + 0.1),
                         paint, bevel=0.02))
        parts.append(box("Headlight", (0.28, 0.03, 0.1), (sx * hx, L / 2 - 0.005, 0.6), head))
        parts.append(box("TailLight", (0.3, 0.03, 0.12), (sx * hx, -L / 2 + 0.005, 0.8), tail))
    front_y = cab_yc + cab_len / 2
    back_y = cab_yc - cab_len / 2
    parts.append(box("Windscreen", (cab_w * 0.9, 0.01, gh), (0, front_y + 0.002, zc), glass,
                     rot=(math.radians(ws_angle), 0, 0)))
    parts.append(box("RearGlass", (cab_w * 0.9, 0.01, gh), (0, back_y - 0.002, zc), glass,
                     rot=(math.radians(-rear_angle), 0, 0)))
    parts.append(box("Grille", (0.55, 0.03, 0.14), (0, L / 2 - 0.005, 0.48), trim))
    parts.append(box("BumperF", (W - 0.1, 0.12, 0.16), (0, L / 2 + 0.02, 0.3), trim, bevel=0.03))
    parts.append(box("BumperR", (W - 0.1, 0.12, 0.16), (0, -L / 2 - 0.02, 0.3), trim, bevel=0.03))
    for sx in (-1, 1):
        for wy in (-wheel_y, wheel_y):
            x = sx * (W / 2 - 0.06)
            parts.append(cylinder("Tire", 0.32, 0.24, (x, wy, 0.32), tire, rot=(0, D90, 0), verts=32, bevel=0.03))
            parts.append(cylinder("Rim", 0.2, 0.25, (x, wy, 0.32), rim, rot=(0, D90, 0), verts=24))
    join(parts, name)
    export(name)


def car_sedan():
    car("car_sedan", L=4.6, W=1.8, cab_len=2.3, cab_yc=-0.1, cab_w=1.5, cab_top=1.42, wheel_y=1.4,
        paint_rgb=(0.28, 0.36, 0.44), ws_angle=25, rear_angle=25)


def car_hatch():
    car("car_hatch", L=4.0, W=1.76, cab_len=2.5, cab_yc=-0.3, cab_w=1.44, cab_top=1.40, wheel_y=1.2,
        paint_rgb=(0.45, 0.04, 0.04), ws_angle=35, rear_angle=20)


# ---- Street furniture ----------------------------------------------------------------------------------------------
def fire_hydrant():
    """Origin: ground at the flange centre. Front nozzle faces +Y. Height 0.69 m."""
    reset()
    red = material("HydrantRed", (0.62, 0.04, 0.03), rough=0.4)
    brass = material("Brass", (0.8, 0.62, 0.25), rough=0.3, metal=1.0)
    parts = [
        cylinder("Flange", 0.17, 0.08, (0, 0, 0.04), red, verts=32, bevel=0.012),
        cylinder("Barrel", 0.11, 0.5, (0, 0, 0.33), red, verts=32, bevel=0.02),
        sphere("Dome", 0.11, (0, 0, 0.58), red, scale=(1, 1, 0.5), segments=24, rings=12),
        cylinder("TopNut", 0.05, 0.05, (0, 0, 0.66), brass, verts=6),
        cylinder("FrontNozzle", 0.05, 0.1, (0, 0.16, 0.36), red, rot=(D90, 0, 0), verts=20),
        cylinder("FrontCap", 0.055, 0.02, (0, 0.215, 0.36), brass, rot=(D90, 0, 0), verts=20),
    ]
    for sx in (-1, 1):
        parts.append(cylinder("SideNozzle", 0.045, 0.1, (sx * 0.16, 0, 0.36), red, rot=(0, D90, 0), verts=20))
        parts.append(cylinder("SideCap", 0.055, 0.02, (sx * 0.215, 0, 0.36), brass, rot=(0, D90, 0), verts=20))
    join(parts, "fire_hydrant")
    export("fire_hydrant")


def trash_bin():
    """Municipal bin, 0.6 m square, 0.95 m tall. Origin: ground at the centre. Ash door faces +Y."""
    reset()
    steel = material("BinSteel", (0.1, 0.22, 0.18), rough=0.5, metal=0.6)
    dark = material("BinDark", (0.04, 0.04, 0.04), rough=0.7)
    metal = material("BinMetal", (0.5, 0.5, 0.52), rough=0.35, metal=1.0)
    parts = [
        box("Plinth", (0.6, 0.6, 0.06), (0, 0, 0.03), dark, bevel=0.01),
        box("Body", (0.56, 0.56, 0.84), (0, 0, 0.06 + 0.42), steel, bevel=0.03, segments=3),
        box("Band", (0.58, 0.58, 0.04), (0, 0, 0.5), metal, bevel=0.005),
        box("Lid", (0.6, 0.6, 0.05), (0, 0, 0.925), steel, bevel=0.015, segments=3),
        box("LidSlot", (0.36, 0.07, 0.006), (0, 0.12, 0.946), dark),
        box("AshDoor", (0.4, 0.02, 0.5), (0, 0.29, 0.5), steel, bevel=0.005),
        box("DoorHandle", (0.2, 0.02, 0.02), (0, 0.31, 0.62), metal),
    ]
    join(parts, "trash_bin")
    export("trash_bin")


def bench():
    """Park bench 1.8 m long, 0.9 m tall. Origin: ground at the centre. Seat faces +Y, backrest at -Y."""
    reset()
    wood = material("Wood", (0.42, 0.26, 0.14), rough=0.65)
    iron = material("CastIron", (0.05, 0.06, 0.06), rough=0.45, metal=0.8)
    parts = []
    for sx in (-1, 1):
        x = sx * 0.84
        parts.append(box("LegFront", (0.05, 0.05, 0.44), (x, 0.16, 0.22), iron, bevel=0.008))
        parts.append(box("BackPost", (0.05, 0.05, 0.9), (x, -0.2, 0.45), iron, bevel=0.008))
    parts.append(box("BeamF", (1.7, 0.04, 0.04), (0, 0.1, 0.42), iron))
    parts.append(box("BeamB", (1.7, 0.04, 0.04), (0, -0.1, 0.42), iron))
    for i in range(5):
        y = -0.16 + i * 0.08
        parts.append(box("Slat", (1.8, 0.065, 0.03), (0, y, 0.465), wood, bevel=0.005))
    for z in (0.6, 0.72, 0.84):
        parts.append(box("BackSlat", (1.8, 0.04, 0.07), (0, -0.26, z), wood, bevel=0.005))
    join(parts, "bench")
    export("bench")


def manhole():
    """Cast-iron cover, 0.64 m diameter, 2 cm thick. Origin: ground at the centre."""
    reset()
    iron = material("CastIron", (0.09, 0.09, 0.095), rough=0.55, metal=0.8)
    parts = [
        cylinder("Cover", 0.32, 0.02, (0, 0, 0.01), iron, verts=48, bevel=0.003),
        torus("RimRing", 0.27, 0.012, (0, 0, 0.02), iron),
        torus("CentreRing", 0.09, 0.008, (0, 0, 0.02), iron),
    ]
    for i in range(8):
        a = i * math.pi / 4
        parts.append(box("Spoke", (0.22, 0.03, 0.008), (0.16 * math.cos(a), 0.16 * math.sin(a), 0.024), iron,
                         rot=(0, 0, a)))
    join(parts, "manhole")
    export("manhole")


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


def neon_path(parts, path, radius, mat, tag):
    pts = [Vector((x, TUBE_Y, z)) for x, z in path]
    for i in range(len(pts) - 1):
        parts.append(rod(tag + "Seg", pts[i], pts[i + 1], radius, mat))
    for p in pts:
        parts.append(sphere(tag + "Joint", radius, p, mat))


def neon_board(name, pink_paths, cyan_paths):
    reset()
    board = material("SignBox", (0.03, 0.03, 0.035), rough=0.5, metal=0.2)
    standoff = material("Standoff", (0.5, 0.5, 0.52), rough=0.35, metal=1.0)
    pink = material("NeonPink", (1.0, 0.1, 0.55), rough=0.3, emission=(1.0, 0.08, 0.5))
    cyan = material("NeonCyan", (0.1, 0.9, 1.0), rough=0.3, emission=(0.05, 0.9, 1.0))
    parts = [box("SignBox", (1.6, 0.12, 0.6), (0, 0.11, 0), board, bevel=0.01)]
    for sx in (-1, 1):
        for sz in (-1, 1):
            parts.append(rod("Standoff", (sx * 0.7, 0.0, sz * 0.22), (sx * 0.7, 0.05, sz * 0.22), 0.02, standoff))
    for path in pink_paths:
        neon_path(parts, path, 0.011, pink, "Pink")
    for path in cyan_paths:
        neon_path(parts, path, 0.011, cyan, "Cyan")
    join(parts, name)
    export(name)


def neon_sign_bar():
    pink = [[(-0.66, 0.16), (-0.44, 0.02), (-0.22, 0.16), (0.0, 0.02), (0.22, 0.16), (0.44, 0.02), (0.66, 0.16)]]
    cyan = [[(-0.66, -0.2), (0.66, -0.2)], [(-0.66, -0.1), (-0.4, -0.1)], [(0.4, -0.1), (0.66, -0.1)]]
    neon_board("neon_sign_bar", pink, cyan)


# Stroke glyphs for "OPEN" in a unit cell (u right, v up); each stroke is one polyline.
GLYPHS = {
    "O": [[(0.25, 0), (0.75, 0), (1, 0.25), (1, 0.75), (0.75, 1), (0.25, 1), (0, 0.75), (0, 0.25), (0.25, 0)]],
    "P": [[(0, 0), (0, 1), (0.75, 1), (1, 0.85), (1, 0.65), (0.75, 0.5), (0, 0.5)]],
    "E": [[(1, 0), (0, 0), (0, 1), (1, 1)], [(0, 0.5), (0.8, 0.5)]],
    "N": [[(0, 0), (0, 1), (1, 0), (1, 1)]],
}


def neon_sign_open():
    lw, lh, gap = 0.2, 0.3, 0.09
    x0 = -(4 * lw + 3 * gap) / 2
    pink = []
    for i, ch in enumerate("OPEN"):
        ox = x0 + i * (lw + gap)
        for stroke in GLYPHS[ch]:
            # The front faces +Y, where the viewer's right is -X: negate X so the word reads left to right.
            pink.append([(-(ox + u * lw), -lh / 2 + v * lh) for u, v in stroke])
    cyan = [[(-0.74, -0.27), (0.74, -0.27), (0.74, 0.27), (-0.74, 0.27), (-0.74, -0.27)]]
    neon_board("neon_sign_open", pink, cyan)


# ---- Wall and building props -------------------------------------------------------------------------------------
def ac_unit():
    """Wall split-unit condenser, 0.8 x 0.3 x 0.55 m. Origin: wall point at the bottom centre of the back face.
    Fan grille faces +Y."""
    reset()
    white = material("ACWhite", (0.88, 0.88, 0.86), rough=0.45)
    grille = material("ACGrille", (0.07, 0.07, 0.075), rough=0.6)
    blade = material("ACFan", (0.55, 0.56, 0.58), rough=0.4, metal=0.3)
    W, H, D = 0.8, 0.55, 0.3
    parts = [
        box("Body", (W, D, H), (0, D / 2, H / 2), white, bevel=0.03, segments=3),
        cylinder("GrilleDisc", 0.2, 0.01, (0, D, 0.3), grille, rot=(D90, 0, 0), verts=40),
        torus("GrilleRing", 0.2, 0.012, (0, D, 0.3), white, rot=(D90, 0, 0)),
    ]
    for i in range(6):
        parts.append(box("Blade", (0.38, 0.012, 0.022), (0, D + 0.008, 0.3), blade, rot=(0, math.radians(i * 30), 0)))
    join(parts, "ac_unit")
    export("ac_unit")


def fire_escape():
    """One floor module of a fire escape: 3.0 m wide, 1.25 m out from the wall, 3.0 m tall.
    Origin: wall base centre. Deck at z = 2.0 m, railings to 3.0 m, ladder down the front right. Sticks out +Y."""
    reset()
    iron = material("BlackIron", (0.05, 0.05, 0.055), rough=0.5, metal=0.7)
    grate = material("Grate", (0.3, 0.3, 0.31), rough=0.35, metal=0.8)
    W, Dp, Z = 3.0, 1.2, 2.0
    parts = [
        box("BeamFront", (W, 0.05, 0.05), (0, Dp - 0.025, Z - 0.025), iron),
        box("BeamBack", (W, 0.05, 0.05), (0, 0.025, Z - 0.025), iron),
    ]
    for sx in (-1, 1):
        parts.append(box("BeamSide", (0.05, Dp, 0.05), (sx * (W / 2 - 0.025), Dp / 2, Z - 0.025), iron))
        parts.append(box("Bracket", (0.06, Dp - 0.1, 0.06), (sx * 1.2, Dp / 2, 1.9), iron))
        parts.append(rod("Brace", (sx * 1.2, 0.0, 1.5), (sx * 1.2, Dp - 0.1, 1.9), 0.02, iron))
        parts.append(box("PostFront", (0.05, 0.05, Z), (sx * (W / 2 - 0.05), Dp - 0.05, Z / 2), iron))
        parts.append(box("PostBack", (0.05, 0.05, Z), (sx * (W / 2 - 0.05), 0.05, Z / 2), iron))
    for i in range(12):
        y = 0.05 + (i + 0.5) * (Dp - 0.1) / 12
        parts.append(box("GrateX", (W - 0.1, 0.02, 0.03), (0, y, Z - 0.015), grate))
    for i in range(15):
        x = -W / 2 + 0.05 + (i + 0.5) * (W - 0.1) / 15
        parts.append(box("GrateY", (0.02, Dp - 0.1, 0.03), (x, Dp / 2, Z - 0.015), grate))
    RH = 1.0
    for i in range(7):
        x = -1.475 + i * (2.95 / 6)
        parts.append(box("RailPostF", (0.04, 0.04, RH), (x, Dp - 0.025, Z + RH / 2), iron))
    parts.append(box("RailTopF", (W, 0.04, 0.05), (0, Dp - 0.025, Z + RH - 0.025), iron))
    parts.append(box("RailMidF", (W, 0.03, 0.03), (0, Dp - 0.025, Z + 0.5), iron))
    for sx in (-1, 1):
        x = sx * (W / 2 - 0.025)
        for i in range(5):
            parts.append(box("RailPostS", (0.04, 0.04, RH), (x, 0.1 + i * 0.25, Z + RH / 2), iron))
        parts.append(box("RailTopS", (0.04, Dp, 0.05), (x, Dp / 2, Z + RH - 0.025), iron))
        parts.append(box("RailMidS", (0.03, Dp, 0.03), (x, Dp / 2, Z + 0.5), iron))
    LX, LY = 1.25, 1.25
    parts.append(rod("LadderRailL", (LX - 0.15, LY, 0.0), (LX - 0.15, LY, Z), 0.015, iron))
    parts.append(rod("LadderRailR", (LX + 0.15, LY, 0.0), (LX + 0.15, LY, Z), 0.015, iron))
    for i in range(7):
        z = 0.3 + i * 0.27
        parts.append(rod("Rung", (LX - 0.15, LY, z), (LX + 0.15, LY, z), 0.012, iron))
    join(parts, "fire_escape")
    export("fire_escape")


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


def storefront():
    """Ground-floor shop module, 6.0 m wide x 3.5 m tall. Origin: bottom centre of the facade plane (y = 0).
    Glass and frames sit in the reveal, the facade wall is at y < 0, and the shop lights face out +Y.
    The emissive interior panels are the 'ShopLight' material, behind the glass."""
    reset()
    facade = material("Facade", (0.5, 0.45, 0.4), rough=0.85)
    frame = material("ShopFrame", (0.07, 0.2, 0.16), rough=0.45, metal=0.1)
    glass = material("Glass", (0.02, 0.03, 0.035), rough=0.05)
    light = material("ShopLight", (1.0, 0.7, 0.38), rough=0.5, emission=(1.0, 0.66, 0.32))
    door = material("DoorWood", (0.12, 0.07, 0.04), rough=0.5)
    brass = material("Brass", (0.8, 0.62, 0.3), rough=0.3, metal=1.0)
    fascia = material("Fascia", (0.1, 0.1, 0.11), rough=0.4, metal=0.3)
    shelf = material("Shelf", (0.25, 0.15, 0.08), rough=0.6)
    wy, wd = -0.15, 0.3
    parts = [
        box("PierOuterL", (0.2, wd, 3.5), (-2.9, wy, 1.75), facade),
        box("PierOuterR", (0.2, wd, 3.5), (2.9, wy, 1.75), facade),
        box("PierL", (0.25, wd, 3.5), (-0.575, wy, 1.75), facade),
        box("PierR", (0.25, wd, 3.5), (0.575, wy, 1.75), facade),
        box("Lintel", (6.0, wd, 0.6), (0, wy, 3.2), facade),
        box("DoorHead", (0.9, wd, 0.6), (0, wy, 2.6), facade),
        box("SillL", (2.1, wd, 0.6), (-1.75, wy, 0.3), facade),
        box("SillR", (2.1, wd, 0.6), (1.75, wy, 0.3), facade),
        box("Fascia", (6.0, 0.12, 0.12), (0, 0.06, 3.44), fascia),
    ]
    fy, fd, t = -0.05, 0.14, 0.07  # frame bars: depth y -0.12..0.02

    def window(tag, x0, x1, z0, z1):
        w, h = x1 - x0, z1 - z0
        xc, zc = (x0 + x1) / 2, (z0 + z1) / 2
        return [
            box(tag + "Top", (w, fd, t), (xc, fy, z1 - t / 2), frame),
            box(tag + "Bot", (w, fd, t), (xc, fy, z0 + t / 2), frame),
            box(tag + "L", (t, fd, h), (x0 + t / 2, fy, zc), frame),
            box(tag + "R", (t, fd, h), (x1 - t / 2, fy, zc), frame),
            box(tag + "Glass", (w, 0.01, h), (xc, -0.1, zc), glass),
            box(tag + "ShopLight", (w - 0.1, 0.02, h - 0.1), (xc, -0.22, zc), light),
        ]

    parts += window("WinL", -2.8, -0.7, 0.6, 2.9)
    parts += window("WinR", 0.7, 2.8, 0.6, 2.9)
    for xc in (-1.75, 1.75):
        for z in (1.1, 1.8):
            parts.append(box("Shelf", (1.9, 0.06, 0.03), (xc, -0.16, z), shelf))
    # Door: leaf with a glass light, handle, and a frame.
    parts.append(box("DoorLeaf", (0.9, 0.05, 2.3), (0, -0.1, 1.15), door))
    parts.append(box("DoorGlass", (0.6, 0.006, 1.1), (0, -0.072, 1.7), glass))
    parts.append(box("DoorHandle", (0.03, 0.04, 0.5), (0.34, -0.04, 1.0), brass))
    parts.append(box("DoorJambL", (0.07, fd, 2.3), (-0.45 + 0.035, fy, 1.15), frame))
    parts.append(box("DoorJambR", (0.07, fd, 2.3), (0.45 - 0.035, fy, 1.15), frame))
    parts.append(box("DoorTop", (0.9, fd, t), (0, fy, 2.3 - t / 2), frame))
    join(parts, "storefront")
    export("storefront")


if __name__ == "__main__":
    street_lamp()
    traffic_light()
    car_sedan()
    car_hatch()
    fire_hydrant()
    trash_bin()
    bench()
    manhole()
    bollard()
    parking_meter()
    neon_sign_bar()
    neon_sign_open()
    ac_unit()
    fire_escape()
    awning()
    storefront()
