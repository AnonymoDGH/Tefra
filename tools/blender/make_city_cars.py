"""Parked cars for the rainy city, modelled procedurally with Blender's Python API (bpy). Own work, CC0.

The body is lofted from cross-sections along the length: a side profile (bumpers, bonnet, windscreen, roof, rear
window, boot), a plan taper and a tumblehome greenhouse, then subdivided for smooth panels. Wheel arches are cut
with booleans; faces are classified into paint, glass, lamps, grille and trim by position. Wheels have tyres with
sidewall bevels, multi-spoke rims and brake discs.

Convention: metres, Blender Z-up, front = +Y (glTF -Z after export), origin at the footprint centre on the ground.

Usage: python3 tools/blender/make_city_cars.py [out_dir]   (default apps/studio/resources/models/city)
"""
import math
import os
import sys

import bpy
import bmesh
from mathutils import Matrix, Vector

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "..", "apps", "studio", "resources", "models",
                                                         "city")


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def material(name, color, rough=0.5, metal=0.0, emission=None, strength=1.0):
    m = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    m.use_nodes = True
    b = m.node_tree.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = (*color, 1.0)
    b.inputs["Roughness"].default_value = rough
    b.inputs["Metallic"].default_value = metal
    if emission:
        b.inputs["Emission Color"].default_value = (*emission, 1.0)
        b.inputs["Emission Strength"].default_value = strength
    return m


def interp(table, t):
    """Piecewise-linear lookup in [(t, value), ...] (sorted by t)."""
    if t <= table[0][0]:
        return table[0][1]
    for (t0, v0), (t1, v1) in zip(table, table[1:]):
        if t <= t1:
            k = (t - t0) / (t1 - t0)
            return v0 + (v1 - v0) * k
    return table[-1][1]


def smoothstep(a, b, x):
    t = min(max((x - a) / (b - a), 0.0), 1.0)
    return t * t * (3 - 2 * t)


class Spec:
    def __init__(self, name, L, W, top, bottom, belt, roof_hw, wheels, wheel_r, glass_y, pillars, paint, screens):
        self.name, self.L, self.W = name, L, W
        self.top, self.bottom, self.belt = top, bottom, belt  # profile tables over t = 0 (rear) .. 1 (front)
        self.roof_hw = roof_hw
        self.wheels = wheels  # t positions of the axles
        self.wheel_r = wheel_r
        self.glass_y = glass_y  # (t0, t1) span of the side glass
        self.pillars = pillars  # t centres of B/C pillars (paint bands inside the glass span)
        self.paint = paint
        self.screens = screens  # t spans of the rear window and the windscreen


def section(spec, t):
    """Right half of the cross-section at t (x >= 0), bottom centre to roof centre: 9 points (x, z)."""
    hw0 = spec.W * 0.5
    end = min(t, 1 - t)
    hw = hw0 * (1.0 - 0.14 * (1 - smoothstep(0.0, 0.09, end)))  # rounded plan at both ends
    zb = interp(spec.bottom, t)
    zt = interp(spec.top, t)
    belt = interp(spec.belt, t)
    g = smoothstep(0.02, 0.14, zt - belt)  # 0 on bonnet/boot, 1 under the roof
    low = [(0.0, zb), (hw - 0.14, zb), (hw - 0.01, zb + 0.1), (hw, zb + 0.32 * (min(zt, belt) - zb))]
    flat = [(hw, min(zt, belt) - 0.09), (hw - 0.07, zt - 0.015), (hw - 0.2, zt), (hw * 0.5, zt), (0.0, zt)]
    rw = spec.roof_hw * (1.0 - 0.1 * (1 - smoothstep(0.0, 0.25, end)))
    house = [(hw - 0.015, belt - 0.03), (hw - 0.07, belt + 0.03), (rw + 0.04, zt - 0.07), (rw - 0.1, zt),
             (0.0, zt)]
    up = [(fx * (1 - g) + hx * g, fz * (1 - g) + hz * g) for (fx, fz), (hx, hz) in zip(flat, house)]
    return low + up


def body_mesh(spec, stations=44):
    ts = {round(0.5 - 0.5 * math.cos(math.pi * i / (stations - 1)), 4) for i in range(stations)}
    # Stations on every material boundary so glass edges follow mesh edges (clean after subdivision).
    for t in [spec.glass_y[0], spec.glass_y[1]] + [p + d for p in spec.pillars for d in (-0.022, 0.022)] + \
            [t for span in spec.screens for t in span]:
        ts = {x for x in ts if abs(x - t) > 0.012}
        ts.add(round(t, 4))
    ts = sorted(ts)
    rings = []
    bm = bmesh.new()
    tag = bm.faces.layers.int.new("tag")
    for t in ts:
        y = (t - 0.5) * spec.L
        half = section(spec, t)
        pts = [Vector((x, y, z)) for x, z in half] + [Vector((-x, y, z)) for x, z in reversed(half[1:-1])]
        rings.append([bm.verts.new(p) for p in pts])
    n = len(rings[0])
    for si, (a, b) in enumerate(zip(rings, rings[1:])):
        tm = (ts[si] + ts[si + 1]) * 0.5
        for i in range(n):
            j = (i + 1) % n
            f = bm.faces.new([a[i], a[j], b[j], b[i]])
            seg = i if i < n // 2 else n - 1 - i  # right-half segment index 0 (bottom) .. 7 (roof centre)
            f[tag] = classify_segment(spec, tm, seg)
    # Crisp shoulder (belt line) and roof edge: creased edges along those section points (both sides).
    crease = bm.edges.layers.float.get("crease_edge") or bm.edges.layers.float.new("crease_edge")
    for k in (4, 7, n - 4, n - 7):
        for a, b in zip(rings, rings[1:]):
            e = bm.edges.get([a[k], b[k]])
            if e:
                e[crease] = 0.7
    for ring, flip in ((rings[0], True), (rings[-1], False)):
        c = bm.verts.new(sum((v.co for v in ring), Vector()) / len(ring))
        for i in range(n):
            j = (i + 1) % n
            f = bm.faces.new([ring[i], ring[j], c] if not flip else [ring[j], ring[i], c])
            f[tag] = MAT_TRIM if ring[i].co.z < interp(spec.bottom, 0.5) + 0.12 else MAT_PAINT
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    me = bpy.data.meshes.new(spec.name + "_body")
    bm.to_mesh(me)
    bm.free()
    ob = bpy.data.objects.new(spec.name + "_body", me)
    bpy.context.scene.collection.objects.link(ob)
    return ob


MAT_PAINT, MAT_GLASS, MAT_TRIM = 0, 1, 4


def classify_segment(spec, t, seg):
    zt = interp(spec.top, t)
    belt = interp(spec.belt, t)
    if seg == 0:
        return MAT_TRIM
    if zt - belt < 0.15:
        return MAT_PAINT
    in_screen = any(a <= t <= b for a, b in spec.screens)
    if in_screen:
        return MAT_GLASS if seg in (6, 7) else MAT_PAINT
    if seg == 5 and spec.glass_y[0] < t < spec.glass_y[1] and not any(abs(t - p) < 0.022 for p in spec.pillars):
        return MAT_GLASS
    return MAT_PAINT


def apply_mods(ob):
    bpy.context.view_layer.objects.active = ob
    for m in list(ob.modifiers):
        bpy.ops.object.modifier_apply(modifier=m.name)


def cylinder(name, r, depth, loc, rot, verts=32):
    bpy.ops.mesh.primitive_cylinder_add(radius=r, depth=depth, location=loc, rotation=rot, vertices=verts)
    o = bpy.context.active_object
    o.name = name
    return o


def classify(ob, mats):
    """Materials from the face tags of the coarse loft (subdivision keeps them per face)."""
    me = ob.data
    me.materials.clear()  # the boolean cutters leave empty slots behind
    for nme in ["paint", "glass", "head", "tail", "trim", "chrome"]:
        me.materials.append(mats[nme])
    tags = me.attributes.get("tag")
    for p in me.polygons:
        p.material_index = tags.data[p.index].value if tags else 0


def lamp(body, name, origin, direction, size, mat, parts, inset=0.015):
    """A lens shaped like a rounded slab, placed where a ray from origin along direction meets the body."""
    hit, loc, nrm, _ = body.ray_cast(origin, direction)
    if not hit:
        return
    bpy.ops.mesh.primitive_cube_add(size=1, location=loc - nrm * inset)
    o = bpy.context.active_object
    o.name = name
    o.scale = size
    o.rotation_euler = nrm.to_track_quat("Y", "Z").to_euler()
    bev = o.modifiers.new("b", "BEVEL")
    bev.width, bev.segments = min(size) * 0.45, 3
    o.data.materials.append(mat)
    parts.append(o)


def wheel(spec, name, x, y, mats, parts):
    r = spec.wheel_r
    side = 1 if x > 0 else -1
    rot = (0, math.pi / 2, 0)
    tyre = cylinder(name + "_tyre", r, 0.22, (x, y, r), rot, 40)
    bev = tyre.modifiers.new("b", "BEVEL")
    bev.width, bev.segments = 0.05, 4
    tyre.data.materials.append(mats["tyre"])
    rim = cylinder(name + "_rim", r * 0.62, 0.02, (x + side * 0.1, y, r), rot, 32)
    rim.data.materials.append(mats["rim"])
    hub = cylinder(name + "_hub", r * 0.16, 0.05, (x + side * 0.11, y, r), rot, 16)
    hub.data.materials.append(mats["rim"])
    disc = cylinder(name + "_disc", r * 0.5, 0.03, (x + side * 0.06, y, r), rot, 24)
    disc.data.materials.append(mats["trim"])
    parts += [tyre, rim, hub, disc]
    for k in range(5):
        a = 2 * math.pi * k / 5
        bpy.ops.mesh.primitive_cube_add(size=1, location=(x + side * 0.112, y + math.cos(a) * r * 0.36,
                                                          r + math.sin(a) * r * 0.36))
        s = bpy.context.active_object
        s.scale = (0.025, 0.06, r * 0.5)
        s.rotation_euler = (-a + math.pi / 2, 0, 0)
        s.data.materials.append(mats["rim"])
        parts.append(s)


def build_car(spec):
    reset()
    paint = material("car_paint", spec.paint, 0.22, 0.55)
    mats = {
        "paint": paint,
        "glass": material("car_glass", (0.02, 0.025, 0.03), 0.05, 0.0),
        "head": material("car_headlamp", (0.75, 0.78, 0.8), 0.05, 0.3),
        "tail": material("car_taillamp", (0.35, 0.015, 0.015), 0.1, 0.0),
        "trim": material("car_trim", (0.025, 0.025, 0.028), 0.6, 0.0),
        "chrome": material("car_chrome", (0.8, 0.8, 0.82), 0.15, 1.0),
        "tyre": material("car_tyre", (0.03, 0.03, 0.03), 0.85, 0.0),
        "rim": material("car_rim", (0.62, 0.63, 0.65), 0.3, 1.0),
        "plate": material("car_plate", (0.85, 0.85, 0.8), 0.4, 0.0),
    }
    body = body_mesh(spec)
    sub = body.modifiers.new("s", "SUBSURF")
    sub.levels = sub.render_levels = 1
    apply_mods(body)
    # Wheel arches: cut slightly larger cylinders out of the body, then line them with a black well.
    parts = []
    hw = spec.W * 0.5
    for t in spec.wheels:
        y = (t - 0.5) * spec.L
        cutter = cylinder("arch", spec.wheel_r + 0.06, spec.W + 0.4, (0, y, spec.wheel_r + 0.02), (0, math.pi / 2, 0),
                          40)
        bo = body.modifiers.new("arch", "BOOLEAN")
        bo.object = cutter
        bo.operation = "DIFFERENCE"
        bo.solver = "EXACT"
        apply_mods(body)
        bpy.data.objects.remove(cutter)
        for sx in (1, -1):
            well = cylinder("well", spec.wheel_r + 0.055, 0.32, (sx * (hw - 0.2), y, spec.wheel_r + 0.02),
                            (0, math.pi / 2, 0), 32)
            well.data.materials.append(mats["trim"])
            parts.append(well)
            wheel(spec, f"w{t}{sx}", sx * (hw - 0.14), y, mats, parts)
    classify(body, mats)
    hw, belt_f, belt_r = spec.W * 0.5, interp(spec.belt, 0.97), interp(spec.belt, 0.03)
    for sx in (1, -1):
        lamp(body, "head", Vector((sx * (hw - 0.3), spec.L, belt_f - 0.11)), Vector((0, -1, 0)), (0.34, 0.06, 0.11),
             mats["head"], parts)
        lamp(body, "tail", Vector((sx * (hw - 0.26), -spec.L, belt_r - 0.1)), Vector((0, 1, 0)), (0.36, 0.06, 0.1),
             mats["tail"], parts)
    lamp(body, "grille", Vector((0, spec.L, interp(spec.bottom, 0.97) + 0.24)), Vector((0, -1, 0)),
         (0.78, 0.06, 0.16), mats["trim"], parts, 0.02)
    for p in body.data.polygons:
        p.use_smooth = True
    # Mirrors at the base of the A-pillars, plates front and rear, door handles.
    t_a = spec.glass_y[1]
    for sx in (1, -1):
        bpy.ops.mesh.primitive_cube_add(size=1, location=(sx * (hw + 0.03), (t_a - 0.5) * spec.L - 0.12,
                                                          interp(spec.belt, t_a) + 0.07))
        m = bpy.context.active_object
        m.scale = (0.12, 0.06, 0.09)
        bev = m.modifiers.new("b", "BEVEL")
        bev.width, bev.segments = 0.025, 3
        m.data.materials.append(paint)
        parts.append(m)
        for t in (spec.glass_y[0] + 0.12, (spec.glass_y[0] + spec.glass_y[1]) * 0.5 + 0.06):
            bpy.ops.mesh.primitive_cube_add(size=1, location=(sx * (hw - 0.005), (t - 0.5) * spec.L,
                                                              interp(spec.belt, t) - 0.08))
            h = bpy.context.active_object
            h.scale = (0.02, 0.14, 0.025)
            h.data.materials.append(mats["chrome"])
            parts.append(h)
    lamp(body, "plate_f", Vector((0, spec.L, interp(spec.bottom, 0.97) + 0.1)), Vector((0, -1, 0)),
         (0.52, 0.03, 0.12), mats["plate"], parts, -0.01)
    lamp(body, "plate_r", Vector((0, -spec.L, belt_r - 0.17)), Vector((0, 1, 0)), (0.52, 0.03, 0.12), mats["plate"],
         parts, -0.01)
    for o in parts:
        for p in o.data.polygons:
            p.use_smooth = o.name.split("_")[-1] in ("tyre", "rim", "disc", "hub") or o.name.startswith("well")
        apply_mods(o)
    bpy.ops.object.select_all(action="DESELECT")
    for o in parts + [body]:
        o.select_set(True)
    bpy.context.view_layer.objects.active = body
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
    bpy.ops.object.join()
    car = bpy.context.active_object
    car.name = spec.name
    return car


def export(ob, name):
    bpy.ops.object.select_all(action="DESELECT")
    ob.select_set(True)
    path = os.path.join(OUT, name + ".glb")
    bpy.ops.export_scene.gltf(filepath=path, export_format="GLB", use_selection=True, export_apply=True,
                              export_yup=True, export_extras=False)
    tris = sum(len(p.vertices) - 2 for p in ob.data.polygons)
    print(f"{name}: {tris} tris, {os.path.getsize(path) // 1024} KiB")


def sedan(name, paint):
    return Spec(name, 4.6, 1.8,
                top=[(0.0, 0.55), (0.03, 0.8), (0.07, 0.93), (0.24, 0.97), (0.3, 1.03), (0.4, 1.39), (0.62, 1.43),
                     (0.74, 1.04), (0.78, 0.98), (0.95, 0.88), (0.99, 0.7), (1.0, 0.52)],
                bottom=[(0.0, 0.32), (0.06, 0.2), (0.94, 0.2), (1.0, 0.3)],
                belt=[(0.0, 0.9), (0.3, 0.97), (0.7, 0.96), (1.0, 0.86)],
                roof_hw=0.62, wheels=(0.19, 0.795), wheel_r=0.33, glass_y=(0.33, 0.71), pillars=(0.52,),
                paint=paint, screens=((0.3, 0.4), (0.62, 0.74)))


def hatch(name, paint):
    return Spec(name, 4.05, 1.76,
                top=[(0.0, 0.6), (0.02, 0.95), (0.05, 1.32), (0.12, 1.45), (0.6, 1.47), (0.72, 1.06), (0.76, 1.0),
                     (0.94, 0.9), (0.99, 0.72), (1.0, 0.52)],
                bottom=[(0.0, 0.32), (0.06, 0.2), (0.94, 0.2), (1.0, 0.3)],
                belt=[(0.0, 0.98), (0.3, 1.0), (0.7, 0.98), (1.0, 0.88)],
                roof_hw=0.62, wheels=(0.17, 0.81), wheel_r=0.31, glass_y=(0.1, 0.7), pillars=(0.17, 0.46),
                paint=paint, screens=((0.02, 0.1), (0.6, 0.72)))


def suv(name, paint):
    return Spec(name, 4.75, 1.92,
                top=[(0.0, 0.75), (0.02, 1.15), (0.05, 1.62), (0.12, 1.76), (0.63, 1.78), (0.74, 1.3), (0.78, 1.22),
                     (0.95, 1.12), (0.99, 0.92), (1.0, 0.65)],
                bottom=[(0.0, 0.42), (0.07, 0.3), (0.93, 0.3), (1.0, 0.4)],
                belt=[(0.0, 1.18), (0.3, 1.22), (0.7, 1.2), (1.0, 1.1)],
                roof_hw=0.7, wheels=(0.18, 0.8), wheel_r=0.38, glass_y=(0.09, 0.72), pillars=(0.18, 0.47),
                paint=paint, screens=((0.02, 0.1), (0.63, 0.74)))


def taxi_sign(car, spec):
    """Roof sign (emissive) on a sedan body."""
    bpy.ops.mesh.primitive_cube_add(size=1, location=(0, 0.0, 1.52))
    s = bpy.context.active_object
    s.scale = (0.56, 0.18, 0.16)
    bev = s.modifiers.new("b", "BEVEL")
    bev.width, bev.segments = 0.04, 3
    s.data.materials.append(material("taxi_sign", (1.0, 0.85, 0.4), 0.3, 0.0, (1.0, 0.82, 0.35), 0.8))
    apply_mods(s)
    bpy.ops.object.select_all(action="DESELECT")
    s.select_set(True)
    car.select_set(True)
    bpy.context.view_layer.objects.active = car
    bpy.ops.object.join()
    return car


CARS = [
    ("car_sedan", sedan, (0.55, 0.04, 0.05)),
    ("car_sedan_black", sedan, (0.012, 0.012, 0.014)),
    ("car_sedan_silver", sedan, (0.55, 0.56, 0.58)),
    ("car_hatch", hatch, (0.08, 0.16, 0.35)),
    ("car_hatch_white", hatch, (0.78, 0.78, 0.76)),
    ("car_suv", suv, (0.06, 0.07, 0.07)),
    ("car_suv_green", suv, (0.08, 0.17, 0.12)),
    ("car_taxi", sedan, (0.95, 0.62, 0.04)),
]


def main():
    os.makedirs(OUT, exist_ok=True)
    only = os.environ.get("CITY_ONLY")
    for name, kind, paint in CARS:
        if only and name not in only.split(","):
            continue
        spec = kind(name, paint)
        car = build_car(spec)
        if name == "car_taxi":
            car = taxi_sign(car, spec)
        export(car, name)


if __name__ == "__main__":
    main()
