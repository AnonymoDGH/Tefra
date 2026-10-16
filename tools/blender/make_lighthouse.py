"""Lighthouse-in-the-storm models, built with Blender's Python API (bpy). Geometry is own work (CC0); the surface
textures are Poly Haven's CC0 scans, downloaded by the script (https://polyhaven.com).

Models (metres, Blender Z-up, front = +Y = glTF -Z, origin on the ground at the footprint centre unless noted):
  lighthouse.gltf  plinth, tapered masonry tower (white with two red bands, door and windows cut through the wall,
                   real wall thickness and plastered inside), corbelled gallery with iron railing, watch room with a
                   door onto the gallery, polygonal lantern room (parapet, glazing with astragals, cornice), domed
                   roof, ventilator ball and lightning rod. Stairs and collision are Parts placed by the template.
  lens_off.glb / lens_on.glb  first-order Fresnel lens: stacked glass prism rings in a brass frame on a pedestal
                   (origin at the lens centre). lens_on is emissive.
  keeper_cottage.gltf  stone cottage with slate gable roof, chimney, door and interior-mapped windows.
  rowboat.glb      clinker-style rowing boat with thwarts and oars (origin at the waterline, centre).
All .gltf files share textures/ (the engine loads each texture once).

Usage: python3 tools/blender/make_lighthouse.py [cache_dir] [out_dir]
  cache_dir: Poly Haven downloads (default build/polyhaven); out_dir default apps/studio/resources/models/coast
"""
import json
import math
import os
import random
import shutil
import sys
import urllib.request

import bpy
import bmesh  # noqa: E402
from mathutils import Matrix, Vector

HERE = os.path.dirname(os.path.abspath(__file__))
CACHE = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "..", "build", "polyhaven")
OUT = sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, "..", "..", "apps", "studio", "resources", "models",
                                                         "coast")
TEX = os.path.join(OUT, "textures")
D90 = math.pi / 2


# ---- Poly Haven textures ---------------------------------------------------------------------------------------
def opener():
    o = urllib.request.build_opener()
    o.addheaders = [("User-Agent", "tefra-asset-fetch/1.0")]
    return o


def fetch_texture(name, res="1k"):
    """Downloads diffuse / normal (GL) / ARM / displacement of a Poly Haven texture into TEX; returns the paths."""
    os.makedirs(TEX, exist_ok=True)
    out = {}
    files = None
    for key, api in (("diff", "Diffuse"), ("nor", "nor_gl"), ("arm", "arm"), ("disp", "Displacement")):
        path = os.path.join(TEX, f"{name}_{key}_{res}.jpg")
        if not os.path.exists(path):
            cached = os.path.join(CACHE, "textures", os.path.basename(path))
            if os.path.exists(cached):
                shutil.copy(cached, path)
            else:
                if files is None:
                    files = json.load(opener().open(f"https://api.polyhaven.com/files/{name}"))
                if api not in files:
                    continue
                with opener().open(files[api][res]["jpg"]["url"]) as r, open(path, "wb") as f:
                    f.write(r.read())
                os.makedirs(os.path.dirname(cached), exist_ok=True)
                shutil.copy(path, cached)
        out[key] = path
    return out


def pbr(name, texture, tint=(1, 1, 1), rough=None, metal=None, parallax=0.0, tile=2.0, normal_strength=1.0):
    """Principled material from a Poly Haven texture set. parallax: relief depth in metres (engine POM, converted to
    UV units with the tile size, metres per UV unit)."""
    if name in bpy.data.materials:
        return bpy.data.materials[name]
    t = fetch_texture(texture)
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    b = nt.nodes["Principled BSDF"]

    def img(path, colour):
        n = nt.nodes.new("ShaderNodeTexImage")
        n.image = bpy.data.images.load(path, check_existing=True)
        n.image.colorspace_settings.name = "sRGB" if colour else "Non-Color"
        return n

    diff = img(t["diff"], True)
    if tint != (1, 1, 1):
        mix = nt.nodes.new("ShaderNodeMix")
        mix.data_type = "RGBA"
        mix.blend_type = "MULTIPLY"
        mix.inputs["Factor"].default_value = 1.0
        mix.inputs[7].default_value = (*tint, 1.0)
        nt.links.new(diff.outputs["Color"], mix.inputs[6])
        nt.links.new(mix.outputs[2], b.inputs["Base Color"])
    else:
        nt.links.new(diff.outputs["Color"], b.inputs["Base Color"])
    if "arm" in t:
        arm = img(t["arm"], False)
        sep = nt.nodes.new("ShaderNodeSeparateColor")
        nt.links.new(arm.outputs["Color"], sep.inputs["Color"])
        nt.links.new(sep.outputs["Green"], b.inputs["Roughness"])
        nt.links.new(sep.outputs["Blue"], b.inputs["Metallic"])
    if rough is not None:
        b.inputs["Roughness"].default_value = rough
    if metal is not None:
        b.inputs["Metallic"].default_value = metal
    if "nor" in t:
        nor = img(t["nor"], False)
        nm = nt.nodes.new("ShaderNodeNormalMap")
        nm.inputs["Strength"].default_value = normal_strength
        nt.links.new(nor.outputs["Color"], nm.inputs["Color"])
        nt.links.new(nm.outputs["Normal"], b.inputs["Normal"])
    if parallax > 0.0 and "disp" in t:
        m["tefra_parallax"] = parallax / tile
        m["tefra_height"] = "textures/" + os.path.basename(t["disp"])
    m["tile"] = tile
    return m


def flat(name, colour, rough=0.5, metal=0.0, emission=None, strength=1.0):
    if name in bpy.data.materials:
        return bpy.data.materials[name]
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    b = m.node_tree.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = (*colour, 1.0)
    b.inputs["Roughness"].default_value = rough
    b.inputs["Metallic"].default_value = metal
    if emission:
        b.inputs["Emission Color"].default_value = (*emission, 1.0)
        b.inputs["Emission Strength"].default_value = strength
    return m


# ---- Mesh helpers -----------------------------------------------------------------------------------------------
def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def link(me, name):
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    return ob


def world_uv(ob, tile_of=None):
    """Box-projected UVs in metres / tile (per face, by its dominant normal axis); cylinders get a wrap-around u."""
    me = ob.data
    bm = bmesh.new()
    bm.from_mesh(me)
    uv = bm.loops.layers.uv.verify()
    for f in bm.faces:
        mat = me.materials[f.material_index] if me.materials else None
        tile = (mat.get("tile", 2.0) if mat else 2.0) if tile_of is None else tile_of
        n = f.normal
        ax = max(range(3), key=lambda i: abs(n[i]))
        for loop in f.loops:
            p = ob.matrix_world @ loop.vert.co
            if ax == 2:
                loop[uv].uv = (p.x / tile, p.y / tile)
            elif ax == 0:
                loop[uv].uv = (p.y / tile, p.z / tile)
            else:
                loop[uv].uv = (p.x / tile, p.z / tile)
    bm.to_mesh(me)
    bm.free()


def cyl_uv(ob, radius_ref):
    """Wrap-around UVs for a body of revolution about Z: u = arc length (m) / tile, v = height / tile."""
    me = ob.data
    bm = bmesh.new()
    bm.from_mesh(me)
    uv = bm.loops.layers.uv.verify()
    for f in bm.faces:
        mat = me.materials[f.material_index] if me.materials else None
        tile = mat.get("tile", 2.0) if mat else 2.0
        c = f.calc_center_median()
        ac = math.atan2(c.y, c.x)
        for loop in f.loops:
            p = loop.vert.co
            a = math.atan2(p.y, p.x)
            if a - ac > math.pi:
                a -= 2 * math.pi
            elif ac - a > math.pi:
                a += 2 * math.pi
            if abs(f.normal.z) > 0.8:
                loop[uv].uv = (p.x / tile, p.y / tile)
            else:
                loop[uv].uv = (a * radius_ref / tile, p.z / tile)
    bm.to_mesh(me)
    bm.free()


def frustum(name, r0, r1, z0, z1, segs, mat, cap=True):
    """Closed truncated cone about Z."""
    bm = bmesh.new()
    lo = [bm.verts.new((r0 * math.cos(2 * math.pi * i / segs), r0 * math.sin(2 * math.pi * i / segs), z0))
          for i in range(segs)]
    hi = [bm.verts.new((r1 * math.cos(2 * math.pi * i / segs), r1 * math.sin(2 * math.pi * i / segs), z1))
          for i in range(segs)]
    for i in range(segs):
        j = (i + 1) % segs
        bm.faces.new([lo[i], lo[j], hi[j], hi[i]])
    if cap:
        bm.faces.new(list(reversed(lo)))
        bm.faces.new(hi)
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    me.materials.append(mat)
    return link(me, name)


def box(name, size, loc, mat, rot=(0, 0, 0), bevel=0.0):
    bpy.ops.mesh.primitive_cube_add(size=1, location=loc, rotation=rot)
    o = bpy.context.active_object
    o.name = name
    o.scale = size
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    if bevel > 0:
        m = o.modifiers.new("b", "BEVEL")
        m.width, m.segments = bevel, 2
        apply(o)
    o.data.materials.append(mat)
    return o


def cylinder(name, r, depth, loc, mat, rot=(0, 0, 0), verts=16):
    bpy.ops.mesh.primitive_cylinder_add(radius=r, depth=depth, location=loc, rotation=rot, vertices=verts)
    o = bpy.context.active_object
    o.name = name
    o.data.materials.append(mat)
    return o


def torus(name, major, minor, z, mat, segs=64):
    bpy.ops.mesh.primitive_torus_add(major_radius=major, minor_radius=minor, major_segments=segs, minor_segments=6,
                                     location=(0, 0, z))
    o = bpy.context.active_object
    o.name = name
    o.data.materials.append(mat)
    return o


def apply(o):
    bpy.context.view_layer.objects.active = o
    for m in list(o.modifiers):
        bpy.ops.object.modifier_apply(modifier=m.name)


def boolean(target, cutter, op="DIFFERENCE"):
    m = target.modifiers.new("bool", "BOOLEAN")
    m.object = cutter
    m.operation = op
    m.solver = "EXACT"
    apply(target)
    bpy.data.objects.remove(cutter)


def join(objs, name):
    bpy.ops.object.select_all(action="DESELECT")
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    bpy.ops.object.join()
    o = bpy.context.active_object
    o.name = name
    bpy.context.scene.cursor.location = (0, 0, 0)
    bpy.ops.object.origin_set(type="ORIGIN_CURSOR")
    return o


def shade_smooth(o, angle=40.0):
    for p in o.data.polygons:
        p.use_smooth = True
    # Auto-smooth equivalent: split edges sharper than angle.
    m = o.modifiers.new("split", "EDGE_SPLIT")
    m.split_angle = math.radians(angle)
    apply(o)


def export(o, name, gltf=True):
    bpy.ops.object.select_all(action="DESELECT")
    o.select_set(True)
    bpy.context.view_layer.objects.active = o
    path = os.path.join(OUT, name + (".gltf" if gltf else ".glb"))
    kw = dict(filepath=path, use_selection=True, export_apply=True, export_yup=True, export_extras=True,
              export_image_format="AUTO" if gltf else "JPEG")
    if gltf:
        kw.update(export_format="GLTF_SEPARATE", export_texture_dir="textures")
    else:
        kw.update(export_format="GLB", export_jpeg_quality=85)
    bpy.ops.export_scene.gltf(**kw)
    tris = sum(len(p.vertices) - 2 for p in o.data.polygons)
    print(f"{name}: {tris} tris, size {tuple(round(v, 2) for v in o.dimensions)}")


# ---- Lighthouse --------------------------------------------------------------------------------------------------
# Dimensions shared with the template (apps/studio/src/templates.cpp, build_lighthouse_storm).
PLINTH_H = 1.2
TOWER_TOP = 20.5           # top of the masonry shaft = gallery and watch-room floor
R_BASE, R_TOP = 3.6, 2.7   # outer radius of the shaft at its foot and top
WALL = 0.55                # masonry thickness
WATCH_R, WATCH_H = 2.35, 2.9
LANTERN_R = 1.85
LENS_Z = TOWER_TOP + WATCH_H + 1.9  # lens centre


def radius_at(z):
    return R_BASE + (R_TOP - R_BASE) * (z - PLINTH_H) / (TOWER_TOP - PLINTH_H)


def lighthouse():
    reset()
    stone = pbr("lh_stone", "seaworn_stone_tiles", tile=2.5, parallax=0.02)
    plaster = pbr("lh_plaster_white", "white_plaster_rough_01", tint=(0.95, 0.94, 0.9), tile=3.0, parallax=0.006)
    red = pbr("lh_plaster_red", "white_plaster_rough_01", tint=(0.62, 0.08, 0.06), tile=3.0, parallax=0.006)
    inner = pbr("lh_plaster_inside", "worn_plaster_wall", tile=2.5, parallax=0.004)
    iron = pbr("lh_iron", "rusty_painted_metal", tint=(0.16, 0.17, 0.18), tile=1.5, metal=0.6)
    dome_m = pbr("lh_dome", "rusty_painted_metal", tint=(0.42, 0.06, 0.05), tile=1.5, metal=0.5)
    wood = pbr("lh_door_wood", "wood_peeling_paint_weathered", tint=(0.35, 0.5, 0.55), tile=1.2)
    floor = pbr("lh_floor", "old_wooden_floor_02", tile=1.5)
    # Glazing: blended glass (the engine treats it as see-through, so the lens shows behind the panes).
    glass = flat("lh_glass", (0.6, 0.7, 0.7), 0.04, 0.0)
    glass.node_tree.nodes["Principled BSDF"].inputs["Alpha"].default_value = 0.12
    glass.blend_method = "BLEND"
    parts = []

    # Octagonal plinth with three steps up to the door (+Y).
    p = frustum("plinth", 5.4, 5.2, 0.0, PLINTH_H, 8, stone)
    p.rotation_euler.z = math.pi / 8
    parts.append(p)
    for k in range(3):
        parts.append(box("step", (1.8, 0.45, PLINTH_H * (k + 1) / 3), (0, 5.2 + 0.45 * (2 - k) + 0.225,
                                                                         PLINTH_H * (k + 1) / 6), stone))

    # Masonry shaft: outer cone minus inner cone, door and windows cut through.
    segs = 64
    outer = frustum("outer", R_BASE, R_TOP, PLINTH_H, TOWER_TOP, segs, plaster)
    # Horizontal loops every 0.5 m so the red bands and the openings have edges to follow.
    bpy.context.view_layer.objects.active = outer
    bm = bmesh.new()
    bm.from_mesh(outer.data)
    edges = [e for e in bm.edges if abs(e.verts[0].co.z - e.verts[1].co.z) > 1.0]
    bmesh.ops.subdivide_edges(bm, edges=edges, cuts=int((TOWER_TOP - PLINTH_H) / 0.5) - 1, use_grid_fill=False)
    bm.to_mesh(outer.data)
    bm.free()
    inner_c = frustum("inner", R_BASE - WALL, R_TOP - WALL, PLINTH_H - 0.01, TOWER_TOP + 0.01, segs, inner)
    boolean(outer, inner_c)
    # Door (front, +Y) and six windows climbing with the stair.
    door_cut = box("doorcut", (1.25, 2.0, 2.35), (0, R_BASE, PLINTH_H + 1.175), plaster)
    boolean(outer, door_cut)
    windows = []
    for k in range(6):
        z = 4.2 + k * 2.75
        a = math.radians(90 + 60 * (k + 1))
        r = radius_at(z)
        cut = box("wincut", (0.62, 2.0, 1.05), (r * math.cos(a), r * math.sin(a), z), plaster, rot=(0, 0, a - D90))
        boolean(outer, cut)
        windows.append((a, z, r))
    # Materials of the shaft faces: inside (normals towards the axis), red bands, reveals.
    me = outer.data
    me.materials.clear()
    for m in (plaster, red, inner):
        me.materials.append(m)
    for f in me.polygons:
        c = f.center
        radial = Vector((c.x, c.y, 0.0)).normalized()
        towards_axis = f.normal.dot(radial) < -0.3
        rr = math.hypot(c.x, c.y)
        if towards_axis or rr < radius_at(c.z) - WALL * 0.5:
            f.material_index = 2
        elif 9.5 < c.z < 12.0 or 15.5 < c.z < 18.0:
            f.material_index = 1
        else:
            f.material_index = 0
    cyl_uv(outer, (R_BASE + R_TOP) * 0.5)
    shade_smooth(outer, 30)
    parts.append(outer)

    # Window frames (iron) with a dark pane set back in the reveal, sills of stone.
    for a, z, r in windows:
        rot = (0, 0, a - D90)
        n = Vector((math.cos(a), math.sin(a), 0))
        c = n * (r - WALL * 0.6)
        parts.append(box("pane", (0.6, 0.02, 1.03), (c.x, c.y, z), glass, rot=rot))
        for dz in (-0.53, 0.53):
            parts.append(box("frame", (0.66, 0.06, 0.06), (c.x, c.y, z + dz), iron, rot=rot))
        mid = box("mullion", (0.04, 0.05, 1.03), (c.x, c.y, z), iron, rot=rot)
        parts.append(mid)
        s = n * (r + 0.05)
        parts.append(box("sill", (0.8, 0.22, 0.08), (s.x, s.y, z - 0.57), stone, rot=rot))
    # Door: frame, a heavy plank door standing ajar, granite lintel.
    dz = PLINTH_H
    parts.append(box("lintel", (1.6, 0.6, 0.3), (0, R_BASE - 0.1, dz + 2.5), stone))
    door = box("door", (1.15, 0.08, 2.3), (0.0, 0.0, 0.0), wood, bevel=0.01)
    door.location = (-0.58, R_BASE - 0.3, dz + 1.15)
    door.rotation_euler.z = math.radians(-70)
    bpy.context.view_layer.objects.active = door
    bpy.ops.object.select_all(action="DESELECT")
    door.select_set(True)
    # Hinge on the door's left edge.
    door.data.transform(Matrix.Translation((0.575, 0, 0)))
    door.location.x -= 0.0
    parts.append(door)
    # Ground floor and the watch-room floor (with the stair well), inside the shaft.
    parts.append(frustum("floor0", R_BASE - WALL + 0.02, R_BASE - WALL + 0.02, PLINTH_H, PLINTH_H + 0.05, 32, floor))
    ring = frustum("floor_top", R_TOP - WALL + 0.05, R_TOP - WALL + 0.05, TOWER_TOP - 0.25, TOWER_TOP, 32, floor)
    well = cylinder("well", 1.25, 1.0, (0.55, -0.35, TOWER_TOP - 0.1), floor, verts=24)
    boolean(ring, well)
    parts.append(ring)

    # Gallery: corbelled stone deck, iron railing.
    deck_r = R_TOP + 1.1
    parts.append(frustum("gallery", deck_r, deck_r, TOWER_TOP - 0.3, TOWER_TOP, 48, stone))
    parts.append(frustum("corbel", R_TOP + 0.05, deck_r - 0.1, TOWER_TOP - 1.1, TOWER_TOP - 0.3, 48, stone))
    for k in range(24):
        a = 2 * math.pi * k / 24
        c = Vector((math.cos(a), math.sin(a), 0)) * (R_TOP + 0.55)
        parts.append(box("bracket", (0.25, 1.0, 0.7), (c.x, c.y, TOWER_TOP - 1.0), stone, rot=(math.radians(-35), 0,
                                                                                               a - D90)))
    rail_r = deck_r - 0.12
    for k in range(40):
        a = 2 * math.pi * k / 40
        parts.append(cylinder("post", 0.025, 1.05, (rail_r * math.cos(a), rail_r * math.sin(a), TOWER_TOP + 0.52),
                              iron, verts=8))
    for z in (0.35, 0.7, 1.05):
        parts.append(torus("rail", rail_r, 0.028 if z == 1.05 else 0.015, TOWER_TOP + z, iron))

    # Watch room: plastered drum with a door onto the gallery (front) and two small windows.
    wr = frustum("watch", WATCH_R, WATCH_R, TOWER_TOP, TOWER_TOP + WATCH_H, 48, plaster)
    wi = frustum("watch_in", WATCH_R - 0.25, WATCH_R - 0.25, TOWER_TOP - 0.01, TOWER_TOP + WATCH_H + 0.01, 48, inner)
    boolean(wr, wi)
    boolean(wr, box("gdoor", (0.95, 1.0, 2.05), (0, WATCH_R, TOWER_TOP + 1.025), plaster))
    for a in (math.radians(210), math.radians(330)):
        boolean(wr, box("wwin", (0.5, 1.0, 0.7), (WATCH_R * math.cos(a), WATCH_R * math.sin(a), TOWER_TOP + 1.6),
                        plaster, rot=(0, 0, a - D90)))
    me = wr.data
    me.materials.clear()
    me.materials.append(plaster)
    me.materials.append(inner)
    for f in me.polygons:
        c = f.center
        if f.normal.dot(Vector((c.x, c.y, 0)).normalized()) < -0.3 or math.hypot(c.x, c.y) < WATCH_R - 0.12:
            f.material_index = 1
    cyl_uv(wr, WATCH_R)
    parts.append(wr)
    parts.append(frustum("watch_ceiling", WATCH_R + 0.25, WATCH_R + 0.25, TOWER_TOP + WATCH_H,
                         TOWER_TOP + WATCH_H + 0.2, 48, stone))

    # Lantern room: 10-sided iron parapet, glazing with diagonal astragals, cornice, dome, ventilator, rod.
    z0 = TOWER_TOP + WATCH_H + 0.2
    sides = 10
    parts.append(frustum("parapet", LANTERN_R, LANTERN_R, z0, z0 + 0.9, sides, iron))
    zg0, zg1 = z0 + 0.9, z0 + 2.9
    for k in range(sides):
        a0 = 2 * math.pi * k / sides
        a1 = 2 * math.pi * (k + 1) / sides
        p0 = Vector((math.cos(a0), math.sin(a0), 0)) * LANTERN_R
        p1 = Vector((math.cos(a1), math.sin(a1), 0)) * LANTERN_R
        mid = (p0 + p1) * 0.5
        w = (p1 - p0).length
        rot = (0, 0, math.atan2(mid.y, mid.x) - D90)
        parts.append(box("glazing", (w, 0.02, zg1 - zg0), (mid.x * 0.98, mid.y * 0.98, (zg0 + zg1) * 0.5), glass,
                         rot=rot))
        parts.append(cylinder("mullion", 0.035, zg1 - zg0, (p0.x, p0.y, (zg0 + zg1) * 0.5), iron, verts=8))
        # Diagonal astragal across each pane.
        d = box("astragal", (0.03, 0.03, math.hypot(w, zg1 - zg0)), (mid.x, mid.y, (zg0 + zg1) * 0.5), iron)
        d.rotation_euler = (0, 0, 0)
        d.rotation_euler.rotate_axis("Y", math.atan2(w, zg1 - zg0) * (1 if k % 2 else -1))
        d.rotation_euler.rotate_axis("Z", rot[2])
        parts.append(d)
    parts.append(frustum("cornice", LANTERN_R + 0.15, LANTERN_R + 0.3, zg1, zg1 + 0.25, sides, iron))
    bpy.ops.mesh.primitive_uv_sphere_add(radius=LANTERN_R + 0.25, segments=40, ring_count=20, location=(0, 0, zg1 + 0.2))
    dome = bpy.context.active_object
    dome.scale.z = 0.6
    bm = bmesh.new()
    bm.from_mesh(dome.data)
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if v.co.z < -0.01], context="VERTS")
    bm.to_mesh(dome.data)
    bm.free()
    dome.data.materials.append(dome_m)
    parts.append(dome)
    top = zg1 + 0.2 + (LANTERN_R + 0.25) * 0.6
    bpy.ops.mesh.primitive_uv_sphere_add(radius=0.32, segments=20, ring_count=12, location=(0, 0, top + 0.25))
    ball = bpy.context.active_object
    ball.data.materials.append(dome_m)
    parts.append(ball)
    parts.append(cylinder("rod", 0.02, 1.6, (0, 0, top + 1.2), iron, verts=6))
    # Lens pedestal (the lens itself is a separate model that changes when the lamp is lit).
    parts.append(cylinder("pedestal", 0.32, LENS_Z - 0.85 - z0, (0, 0, (z0 + LENS_Z - 0.85) * 0.5), iron, verts=16))

    for o in parts:
        if o.data.uv_layers.active is None or o.name.startswith(("step", "lintel", "sill", "bracket", "gallery",
                                                                 "corbel", "plinth", "watch_ceiling", "floor",
                                                                 "door", "parapet", "cornice")):
            world_uv(o)
    lh = join(parts, "lighthouse")
    export(lh, "lighthouse")


def fresnel_lens(lit):
    reset()
    brass = flat("lens_brass", (0.55, 0.4, 0.16), 0.3, 1.0)
    if lit:
        g = flat("lens_glass_on", (1.0, 0.92, 0.75), 0.05, 0.0, emission=(1.0, 0.93, 0.78), strength=1.0)
    else:
        g = flat("lens_glass_off", (0.32, 0.38, 0.36), 0.03, 0.0)
    parts = []
    # Central drum: a stack of refracting prism rings (a bulging lens profile), catadioptric rings above and below.
    rings = 14
    for k in range(rings):
        z = -0.75 + 1.5 * (k + 0.5) / rings
        r = 0.62 - 0.08 * abs(z) / 0.75
        t = torus("prism", r, 0.055, z, g, segs=48)
        t.scale.z = 0.95
        parts.append(t)
    for k in range(5):
        for sgn in (-1, 1):
            z = sgn * (0.8 + 0.09 * k)
            parts.append(torus("cata", 0.6 - 0.09 * k, 0.04, z, g, segs=40))
    # Bull's-eye panels: four lens discs facing the beams' directions (the beams rotate with the lens).
    for a in (0.0, math.pi):
        n = Vector((math.cos(a), math.sin(a), 0))
        d = cylinder("bullseye", 0.38, 0.06, (n.x * 0.66, n.y * 0.66, 0.0), g, rot=(D90, 0, a + D90), verts=32)
        parts.append(d)
    for k in range(8):
        a = 2 * math.pi * k / 8
        parts.append(cylinder("frame", 0.018, 1.95, (0.7 * math.cos(a), 0.7 * math.sin(a), 0.0), brass, verts=6))
    for z in (-0.98, 0.0, 0.98):
        parts.append(torus("band", 0.7, 0.02, z, brass, segs=48))
    parts.append(cylinder("lamp", 0.07, 0.3, (0, 0, 0), g, verts=12))
    lens = join(parts, "lens")
    for p in lens.data.polygons:
        p.use_smooth = True
    export(lens, "lens_on" if lit else "lens_off", gltf=False)


# ---- Keeper's cottage --------------------------------------------------------------------------------------------
def make_room_atlas(path):
    """Small 4 x 3 interior atlas for the cottage windows (interior mapping): kitchen and parlour lit by lamps."""
    W, H = 512, 384
    px = [0.0] * (W * H * 4)
    rnd = random.Random(3)
    for cell in range(12):
        cx, cy = (cell % 4) * 128, (cell // 4) * 128
        lit = cell >= 4
        lamp = rnd.uniform(0.3, 0.7)
        warm = (1.0, 0.62, 0.3) if cell % 2 else (1.0, 0.72, 0.42)
        for y in range(128):
            v = y / 127
            for x in range(128):
                u = x / 127
                d = math.hypot((u - lamp) * 1.4, (v - 0.55) * 1.1)
                k = 0.18 + 0.85 * math.exp(-d * d * 5.0)
                c = [warm[i] * k * 0.7 for i in range(3)]
                if v < 0.1:
                    c = [ch * 0.4 for ch in c]
                if 0.1 < v < 0.38 and 0.2 < u < 0.7:
                    c = [ch * 0.3 for ch in c]  # table / dresser
                if 0.62 < v < 0.8 and 0.72 < u < 0.86:
                    c = [ch * 0.5 for ch in c]  # picture
                if u < 0.12 or u > 0.9:
                    c = [ch * (0.55 + 0.2 * math.sin(u * 80.0)) for ch in c]  # curtains
                if not lit:
                    c = [0.025 + ch * 0.04 for ch in c]
                i = ((cy + y) * W + cx + x) * 4
                px[i:i + 4] = [min(c[0], 1), min(c[1], 1), min(c[2], 1), 1.0]
    img = bpy.data.images.new("cottage_rooms", W, H, alpha=False)
    img.pixels = px
    img.filepath_raw = path
    img.file_format = "PNG"
    img.save()
    return img


def cottage():
    reset()
    wall = pbr("ct_stone", "rustic_stone_wall", tile=2.5, parallax=0.03)
    roof = pbr("ct_slate", "roof_slates_02", tile=2.0, parallax=0.012)
    wood = pbr("ct_wood", "weathered_planks", tile=1.5)
    trim = pbr("ct_trim", "white_plaster_rough_01", tint=(0.85, 0.85, 0.82), tile=2.0)
    os.makedirs(TEX, exist_ok=True)
    atlas = make_room_atlas(os.path.join(TEX, "cottage_rooms.png"))
    room = bpy.data.materials.new("ct_room")
    room["tefra_interior"] = 0.9
    room.use_nodes = True
    nt = room.node_tree
    b = nt.nodes["Principled BSDF"]
    tex = nt.nodes.new("ShaderNodeTexImage")
    tex.image = atlas
    nt.links.new(tex.outputs["Color"], b.inputs["Base Color"])
    nt.links.new(tex.outputs["Color"], b.inputs["Emission Color"])
    b.inputs["Emission Strength"].default_value = 1.0
    L, Wd, H = 8.0, 5.5, 3.0
    parts = []
    body = box("walls", (L, Wd, H), (0, 0, H / 2), wall)
    hollow = box("hollow", (L - 0.9, Wd - 0.9, H - 0.4), (0, 0, H / 2 + 0.2), wall)
    boolean(body, hollow)
    # Door (front +Y) and four windows; each window gets an interior-mapped card behind the frame.
    boolean(body, box("dcut", (1.0, 1.0, 2.1), (-1.6, Wd / 2, 1.05), wall))
    wins = [(1.4, Wd / 2, 0), (-3.0, Wd / 2, 0), (1.4, -Wd / 2, math.pi), (-2.0, -Wd / 2, math.pi)]
    for x, y, a in wins:
        boolean(body, box("wcut", (0.9, 1.0, 1.1), (x, y, 1.65), wall))
    parts.append(body)
    for i, (x, y, a) in enumerate(wins):
        s = 1 if y > 0 else -1
        # Interior card just inside the wall, UVs in one atlas cell (lit row for three of them).
        cell = i % 4
        row = 2 if i != 3 else 0
        u0, v0 = cell * 0.25, row / 3
        bm = bmesh.new()
        uv = bm.loops.layers.uv.verify()
        yy = y - s * 0.3
        vs = [bm.verts.new((x - 0.5, yy, 1.05)), bm.verts.new((x + 0.5, yy, 1.05)), bm.verts.new((x + 0.5, yy, 2.25)),
              bm.verts.new((x - 0.5, yy, 2.25))]
        if s < 0:
            vs = [vs[1], vs[0], vs[3], vs[2]]
        f = bm.faces.new(vs)
        for loop, t in zip(f.loops, [(u0 + 0.01, v0 + 0.013), (u0 + 0.24, v0 + 0.013), (u0 + 0.24, v0 + 0.32),
                                     (u0 + 0.01, v0 + 0.32)]):
            loop[uv].uv = t
        me = bpy.data.meshes.new("card")
        bm.to_mesh(me)
        bm.free()
        me.materials.append(room)
        parts.append(link(me, "card"))
        for dz in (-0.58, 0.58):
            parts.append(box("wframe", (1.0, 0.12, 0.08), (x, y - s * 0.05, 1.65 + dz), trim))
        parts.append(box("wbar", (0.05, 0.08, 1.1), (x, y - s * 0.1, 1.65), trim))
        parts.append(box("wbar", (0.9, 0.08, 0.05), (x, y - s * 0.1, 1.65), trim))
        parts.append(box("shutter", (0.48, 0.05, 1.15), (x - 0.75, y + s * 0.04, 1.65), wood))
        parts.append(box("shutter", (0.48, 0.05, 1.15), (x + 0.75, y + s * 0.04, 1.65), wood))
    parts.append(box("door", (0.95, 0.08, 2.05), (-1.6, Wd / 2 - 0.25, 1.03), wood, bevel=0.01))
    # Gable roof (40 degrees) with overhangs, ridge and chimney.
    pitch = math.radians(40)
    half = Wd / 2 + 0.35
    rise = half * math.tan(pitch)
    slope_len = half / math.cos(pitch)
    for s in (1, -1):
        r = box("roof", (L + 0.7, slope_len + 0.1, 0.12), (0, s * half / 2, H + rise / 2 + 0.05), roof,
                rot=(-s * pitch, 0, 0))
        parts.append(r)
    for x in (-L / 2, L / 2):
        bm = bmesh.new()
        vs = [bm.verts.new((x, -Wd / 2, H)), bm.verts.new((x, Wd / 2, H)), bm.verts.new((x, 0, H + rise - 0.1))]
        bm.faces.new(vs if x > 0 else list(reversed(vs)))
        me = bpy.data.meshes.new("gable")
        bm.to_mesh(me)
        bm.free()
        me.materials.append(wall)
        parts.append(link(me, "gable"))
    parts.append(box("ridge", (L + 0.75, 0.28, 0.16), (0, 0, H + rise + 0.08), roof))
    parts.append(box("chimney", (0.8, 0.8, 2.6), (L / 2 - 1.0, -0.8, H + rise - 0.2), wall))
    parts.append(box("chimney_cap", (0.95, 0.95, 0.12), (L / 2 - 1.0, -0.8, H + rise + 1.15), trim))
    parts.append(box("step", (1.4, 0.6, 0.18), (-1.6, Wd / 2 + 0.3, 0.09), wall))
    for o in parts:
        if not o.name.startswith("card"):
            world_uv(o)
    c = join(parts, "keeper_cottage")
    export(c, "keeper_cottage")
    print("cottage chimney top (template smoke):", (L / 2 - 1.0, -0.8, H + rise + 1.25))


# ---- Rowing boat -------------------------------------------------------------------------------------------------
def rowboat():
    reset()
    paint = pbr("boat_paint", "wood_peeling_paint_weathered", tint=(0.9, 0.9, 0.88), tile=1.2)
    inside = pbr("boat_wood", "weathered_planks", tile=1.0)
    L, B, D = 4.2, 1.45, 0.62
    stations = 28
    rings = []
    bm = bmesh.new()
    for i in range(stations):
        t = i / (stations - 1)
        y = (t - 0.5) * L
        # Plan: pointed bow (+Y), fuller transom stern; sheer rises at both ends.
        width = B * 0.5 * (math.sin(math.pi * min(t * 1.15, 1.0)) ** 0.7 if t > 0.12 else 0.72 + 0.28 * t / 0.12)
        width = max(width, 0.02)
        sheer = D + 0.18 * (2 * t - 1) ** 2 + (0.08 if t > 0.9 else 0)
        keel = 0.0 + 0.1 * (2 * t - 1) ** 4
        pts = []
        for k in range(9):
            a = k / 8  # 0 keel .. 1 gunwale
            x = width * math.sin(a * D90) ** 0.8
            z = keel + (sheer - keel) * (1 - math.cos(a * D90)) ** 0.9
            pts.append((x, z))
        ring = [bm.verts.new((x, y, z)) for x, z in pts] + [bm.verts.new((-x, y, z)) for x, z in reversed(pts[:-1])]
        rings.append(ring[::-1] if False else ring)
    n = len(rings[0])
    for a, b in zip(rings, rings[1:]):
        for k in range(n - 1):
            if k == 8:
                continue  # open at the gunwale (k=8 joins the two sides across the top)
            bm.faces.new([a[k], a[k + 1], b[k + 1], b[k]])
    # Transom (stern, -Y) closed.
    bm.faces.new(rings[0][:])
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    me = bpy.data.meshes.new("hull")
    bm.to_mesh(me)
    bm.free()
    hull = link(me, "hull")
    me.materials.append(paint)
    me.materials.append(inside)
    sol = hull.modifiers.new("t", "SOLIDIFY")
    sol.thickness = 0.04
    sol.material_offset = 1
    apply(hull)
    for p in hull.data.polygons:
        p.use_smooth = True
    parts = [hull]
    # Thwarts, gunwale rails, oars resting across the boat.
    for y in (-0.9, 0.1, 0.95):
        parts.append(box("thwart", (B * 0.85, 0.24, 0.04), (0, y, D * 0.62), inside))
    for s in (1, -1):
        o = cylinder("oar", 0.025, 3.0, (s * 0.35, 0.2, D * 0.66), inside, rot=(D90, 0, s * 0.08), verts=8)
        parts.append(o)
        parts.append(box("blade", (0.14, 0.5, 0.015), (s * 0.35 + s * 0.12, 1.55, D * 0.66), inside))
    for o in parts[1:]:
        world_uv(o, 1.0)
    world_uv(hull, 1.2)
    boat = join(parts, "rowboat")
    # Origin at the waterline (0.22 m above the keel).
    boat.data.transform(Matrix.Translation((0, 0, -0.22)))
    export(boat, "rowboat", gltf=False)


def main():
    os.makedirs(OUT, exist_ok=True)
    only = os.environ.get("COAST_ONLY")
    for name, fn in (("lighthouse", lighthouse), ("lens", lambda: (fresnel_lens(False), fresnel_lens(True))),
                     ("cottage", cottage), ("rowboat", rowboat)):
        if only and name not in only.split(","):
            continue
        fn()


if __name__ == "__main__":
    main()
