"""Assembles the rainy-city buildings from Poly Haven's CC0 modular facade kits with Blender's Python API (bpy).

Kits (CC0, https://polyhaven.com, collection "Hidden Alley"):
  - modular_urban_apartments_facade (plaster apartments: windows, doors, dado bands, cornices, crowns, piers);
  - modular_factory_facade (brick lofts and warehouses: tall windows, garage doors).
Both use 3 m wide x 3 m tall modules whose front face lies on glTF z = 0, facing +z.

Every building is one .gltf (separate files) and all of them share the same textures/ folder, which the engine
loads once (Scene::find_texture). Window glass is replaced by an interior card 0.35 m behind the frame: lit rooms
(emissive atlas made here, own work) or dark ones. Tall towers stack more floors; roofs get a slab, a parapet crown
and rooftop units.

Usage (Blender 4.2+ or the `bpy` wheel, Python 3.13):
  python3 tools/blender/make_city_buildings.py [kit_dir] [out_dir]
kit_dir: where the Poly Haven glTF downloads live (downloaded there when missing).
Default out_dir: apps/studio/resources/models/city/buildings
"""
import json
import math
import os
import random
import sys
import urllib.request

import bpy
import bmesh
from mathutils import Matrix, Vector

HERE = os.path.dirname(os.path.abspath(__file__))
KIT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "..", "build", "polyhaven")
OUT = sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, "..", "..", "apps", "studio", "resources", "models",
                                                         "city", "buildings")
M = 3.0  # module width and floor height (m)
KITS = {"apt": "modular_urban_apartments_facade", "fac": "modular_factory_facade"}
EXTRA = ["exterior_aircon_unit"]
TEX_RES = "1k"


# ---- Download (Poly Haven public API, CC0)
def fetch(name, res):
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


# ---- Library
def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)


LIB = {}  # "apt:wall_standard_standard" -> mesh datablock (module space, Blender Z-up)
MATS = {}


def load_kit(tag, path):
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=path)
    for o in set(bpy.data.objects) - before:
        if o.type != "MESH":
            continue
        base = o.name.split(".")[0]
        stem, _, num = base.rpartition("_")
        key = f"{tag}:{stem if num.isdigit() else base}"
        variant = f"{tag}:{base}"
        o.data.name = variant
        # Keep module-space meshes: the layout translation is dropped.
        LIB.setdefault(key, o.data)
        LIB[variant] = o.data
        bpy.data.objects.remove(o)
    for m in bpy.data.materials:
        MATS[m.name] = m


def decimate(me, target):
    """Collapse-decimates a mesh datablock to about target triangles (UVs kept by the modifier)."""
    tris = sum(len(p.vertices) - 2 for p in me.polygons)
    if tris <= target:
        return
    ob = bpy.data.objects.new("dec", me)
    bpy.context.scene.collection.objects.link(ob)
    mod = ob.modifiers.new("dec", "DECIMATE")
    mod.ratio = target / tris
    mod.use_collapse_triangulate = True
    bpy.context.view_layer.objects.active = ob
    dg = bpy.context.evaluated_depsgraph_get()
    new = bpy.data.meshes.new_from_object(ob.evaluated_get(dg))
    bpy.data.objects.remove(ob)
    me.clear_geometry()
    bm = bmesh.new()
    bm.from_mesh(new)
    bm.to_mesh(me)
    bm.free()
    bpy.data.meshes.remove(new)


def simplify_library():
    """Plaster/brick walls carry a dense flat grid; planar dissolve keeps the shape and UV seams."""
    done = set()
    for key, me in list(LIB.items()):
        if id(me) in done:
            continue
        done.add(id(me))
        if ":window_" in key or ":door_" in key:
            decimate(me, 340 if "double" in key or "large" in key or "garage" in key else 240)
        elif ":crown" in key:
            decimate(me, 160)
        elif key == "aircon":
            decimate(me, 400)
        elif key.startswith("fe:"):
            decimate(me, max(200, int(sum(len(p.vertices) - 2 for p in me.polygons) * 0.25)))
    # Far variants for upper floors (seen from the street only from afar): about a third of the triangles.
    for key, me in list(LIB.items()):
        if (":window_" in key or ":door_window" in key or (key.startswith("fac:wall_window"))) and "#lo" not in key:
            lo = me.copy()
            tris = sum(len(p.vertices) - 2 for p in me.polygons)
            decimate(lo, max(60, tris // 3))
            LIB[key + "#lo"] = lo
    for key, me in list(LIB.items()):
        if ":wall_" not in key and ":base_" not in key and ":dado_" not in key and ":cornice" not in key:
            continue
        if me.get("simplified"):
            continue
        bm = bmesh.new()
        bm.from_mesh(me)
        bmesh.ops.dissolve_limit(bm, angle_limit=math.radians(1.0), verts=bm.verts, edges=bm.edges,
                                 delimit={"UV", "MATERIAL", "SEAM", "SHARP"})
        bmesh.ops.triangulate(bm, faces=bm.faces)
        bm.to_mesh(me)
        bm.free()
        me["simplified"] = True


# ---- Room atlas (own work): 4 x 3 cells of 256 px. Row 0: dark rooms, row 1: lit flats, row 2: lit shops.
ATLAS_W, ATLAS_H, CELL = 1024, 768, 256
ROW_DARK, ROW_LIT, ROW_SHOP = 0, 1, 2


def make_shelves(rnd):
    """Products on three shelf boards: (u0, u1, v0, v1, colour) boxes of muted packaging colours."""
    boxes = []
    palette = [(0.85, 0.3, 0.25), (0.25, 0.45, 0.8), (0.9, 0.8, 0.35), (0.35, 0.65, 0.4), (0.9, 0.9, 0.88),
               (0.6, 0.35, 0.6), (0.95, 0.55, 0.2), (0.3, 0.3, 0.32)]
    for shelf_v in (0.3, 0.5, 0.7):
        u = 0.04
        while u < 0.96:
            wdt = rnd.uniform(0.015, 0.05)
            hgt = rnd.uniform(0.06, 0.15)
            if rnd.random() < 0.85:
                boxes.append((u, u + wdt, shelf_v, shelf_v + hgt, rnd.choice(palette)))
            u += wdt + rnd.uniform(0.0, 0.012)
    person = rnd.random() < 0.5
    return boxes, person, rnd.uniform(0.25, 0.75)


def shop_pixel(u, v, base, shelves):
    boxes, person, px = shelves
    # Back wall: soft vertical falloff from the ceiling strip lights.
    k = 0.42 + 0.25 * v
    c = [base[i] * k for i in range(3)]
    if v > 0.95:
        c = [base[i] * 0.95 for i in range(3)]  # strip light
    for shelf_v in (0.3, 0.5, 0.7):
        if shelf_v - 0.012 < v < shelf_v:
            c = [ch * 0.35 for ch in c]  # shelf board edge
    for u0, u1, v0, v1, col in boxes:
        if u0 <= u < u1 and v0 <= v < v1:
            shade = 0.75 + 0.25 * (v - v0) / (v1 - v0)
            c = [col[i] * base[i] * 0.55 * shade for i in range(3)]
            break
    if v < 0.24 and 0.12 < u < 0.7:
        c = [ch * 0.28 for ch in c]  # counter
        if v > 0.22:
            c = [base[i] * 0.5 for i in range(3)]  # counter top catching the light
    if person:
        # Silhouette of someone at the counter: head and shoulders.
        dh = math.hypot((u - px) * 1.0, (v - 0.47) * 0.9)
        body = abs(u - px) < 0.07 and 0.18 < v < 0.42
        if dh < 0.045 or body:
            c = [ch * 0.18 for ch in c]
    if v < 0.06:
        c = [base[i] * 0.3 for i in range(3)]  # floor
    return c


def make_room_atlas(path):
    rnd = random.Random(7)
    px = [0.0] * (ATLAS_W * ATLAS_H * 4)

    def put(x, y, c):
        i = (y * ATLAS_W + x) * 4
        px[i:i + 4] = [min(c[0], 1.0), min(c[1], 1.0), min(c[2], 1.0), 1.0]

    flat = [(1.0, 0.70, 0.40), (1.0, 0.82, 0.60), (0.95, 0.60, 0.33), (0.55, 0.70, 1.0)]
    shop = [(1.0, 1.0, 1.0), (1.0, 0.86, 0.66), (0.86, 1.0, 0.94), (1.0, 0.92, 0.82)]
    for cell in range(12):
        cx, cy = (cell % 4) * CELL, (cell // 4) * CELL
        row = cell // 4
        base = shop[cell % 4] if row == ROW_SHOP else flat[(cell + 1) % 4]
        lamp_x = rnd.uniform(0.3, 0.7)
        has_curtain = rnd.random() < 0.6
        sofa_h = rnd.uniform(0.18, 0.3)
        shelf = rnd.random() < 0.5
        blinds = rnd.random() < 0.3
        shelves = make_shelves(rnd) if row == ROW_SHOP else None
        for y in range(CELL):
            v = y / (CELL - 1)  # 0 bottom, 1 top (Blender images start at the bottom row)
            for x in range(CELL):
                u = x / (CELL - 1)
                if row == ROW_SHOP:
                    c = shop_pixel(u, v, base, shelves)
                else:
                    # Flat: back wall lit by a ceiling lamp, bright pool near the top, falloff to the floor.
                    d = math.hypot((u - lamp_x) * 1.3, (v - 0.92) * 1.0)
                    k = 0.22 + 0.95 * math.exp(-d * d * 4.0)
                    if v < 0.12:
                        k *= 0.45
                    if v > 0.94:
                        k *= 1.25
                    c = [base[i] * k for i in range(3)]
                    if 0.12 <= v < 0.12 + sofa_h and 0.15 < u < 0.62:
                        c = [ch * 0.25 for ch in c]
                    if shelf and 0.68 < u < 0.86 and 0.12 < v < 0.72:
                        c = [ch * (0.35 if int(v * 14) % 3 else 0.6) for ch in c]
                    if 0.45 < v < 0.62 and 0.25 < u < 0.42:
                        c = [ch * 0.55 for ch in c]
                    if has_curtain and (u < 0.14 or u > 0.88):
                        fold = 0.75 + 0.25 * math.sin(u * 90.0)
                        c = [ch * 0.7 * fold + 0.05 for ch in c]
                    if blinds and int(v * 40) % 2 == 0:
                        c = [ch * 0.55 for ch in c]
                    if row == ROW_DARK:
                        c = [0.03 + ch * 0.035 for ch in c]
                    else:
                        c = [ch * 0.6 for ch in c]  # warm lamp light, not a white box once emitted
                put(cx + x, cy + y, c)
    img = bpy.data.images.new("city_rooms", ATLAS_W, ATLAS_H, alpha=False)
    img.pixels = px
    img.filepath_raw = path
    img.file_format = "PNG"
    img.save()
    return img


def room_material(img, lit):
    name = "city_room_lit" if lit else "city_room_dark"
    if name in bpy.data.materials:
        return bpy.data.materials[name]
    m = bpy.data.materials.new(name)
    m["tefra_interior"] = 0.9  # room depth in window widths (engine interior mapping)
    m.use_nodes = True
    nt = m.node_tree
    bsdf = nt.nodes["Principled BSDF"]
    tex = nt.nodes.new("ShaderNodeTexImage")
    tex.image = img
    nt.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
    bsdf.inputs["Roughness"].default_value = 0.35 if not lit else 0.8
    if lit:
        nt.links.new(tex.outputs["Color"], bsdf.inputs["Emission Color"])
        bsdf.inputs["Emission Strength"].default_value = 1.0
    return m


def simple_material(name, color, rough, metal=0.0):
    if name in bpy.data.materials:
        return bpy.data.materials[name]
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    b = m.node_tree.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = (*color, 1.0)
    b.inputs["Roughness"].default_value = rough
    b.inputs["Metallic"].default_value = metal
    return m


# ---- Assembly
class Builder:
    def __init__(self, name, seed):
        self.name = name
        self.rnd = random.Random(seed)
        self.bm = bmesh.new()
        self.mats = []  # material list of the merged mesh
        self.uv = None

    def mat_index(self, mat):
        if mat not in self.mats:
            self.mats.append(mat)
        return self.mats.index(mat)

    def add_mesh(self, me, matrix, drop_glass=True):
        """Appends a library mesh transformed by matrix; glass faces are left out."""
        tmp = bmesh.new()
        tmp.from_mesh(me)
        if drop_glass:
            glass = [f for f in tmp.faces if me.materials[f.material_index] and
                     "glass" in me.materials[f.material_index].name]
            bmesh.ops.delete(tmp, geom=glass, context="FACES")
        bmesh.ops.transform(tmp, matrix=matrix, verts=tmp.verts)
        remap = [self.mat_index(m) for m in me.materials]
        for f in tmp.faces:
            f.material_index = remap[f.material_index] if remap else 0
        # The temporary mesh carries the merged material list so the indices survive the round trip.
        tmpme = bpy.data.meshes.new("tmp")
        for m in self.mats:
            tmpme.materials.append(m)
        tmp.to_mesh(tmpme)
        tmp.free()
        self.bm.from_mesh(tmpme)
        bpy.data.meshes.remove(tmpme)

    def add_quad(self, corners, uvs, mat):
        uv = self.bm.loops.layers.uv.verify()
        vs = [self.bm.verts.new(c) for c in corners]
        f = self.bm.faces.new(vs)
        f.material_index = self.mat_index(mat)
        for loop, t in zip(f.loops, uvs):
            loop[uv].uv = t
        return f

    def add_box(self, lo, hi, mat, uv_scale=0.25):
        """Axis-aligned box with world-scaled UVs (roof slabs, parapets, tanks)."""
        uv = self.bm.loops.layers.uv.verify()
        x0, y0, z0 = lo
        x1, y1, z1 = hi
        v = [self.bm.verts.new(p) for p in [(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0),
                                             (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)]]
        faces = [(4, 5, 6, 7), (3, 2, 1, 0), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]
        for idx in faces:
            f = self.bm.faces.new([v[i] for i in idx])
            f.material_index = self.mat_index(mat)
            n = f.normal
            for loop in f.loops:
                p = loop.vert.co
                if abs(n.z) > 0.5:
                    loop[uv].uv = (p.x * uv_scale, p.y * uv_scale)
                elif abs(n.x) > 0.5:
                    loop[uv].uv = (p.y * uv_scale, p.z * uv_scale)
                else:
                    loop[uv].uv = (p.x * uv_scale, p.z * uv_scale)

    def add_box_m(self, matrix, lo, hi, mat):
        """Box given in module space, transformed by matrix (frames, sills)."""
        uv = self.bm.loops.layers.uv.verify()
        x0, y0, z0 = lo
        x1, y1, z1 = hi
        pts = [(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0), (x0, y0, z1), (x1, y0, z1), (x1, y1, z1),
               (x0, y1, z1)]
        v = [self.bm.verts.new(matrix @ Vector(p)) for p in pts]
        for idx in [(4, 5, 6, 7), (3, 2, 1, 0), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]:
            f = self.bm.faces.new([v[i] for i in idx])
            f.material_index = self.mat_index(mat)
            for loop in f.loops:
                loop[uv].uv = (0.5, 0.5)

    def finish(self):
        me = bpy.data.meshes.new(self.name)
        self.bm.to_mesh(me)
        self.bm.free()
        for m in self.mats:
            me.materials.append(m)
        ob = bpy.data.objects.new(self.name, me)
        bpy.context.scene.collection.objects.link(ob)
        return ob


def side_frames(w, d):
    """(origin, u, n) per facade: modules run along u, the outward normal is n (Blender Z-up)."""
    W, D = w * M, d * M
    return [(Vector((0, 0, 0)), Vector((1, 0, 0)), Vector((0, -1, 0)), w),
            (Vector((W, 0, 0)), Vector((0, 1, 0)), Vector((1, 0, 0)), d),
            (Vector((W, D, 0)), Vector((-1, 0, 0)), Vector((0, 1, 0)), w),
            (Vector((0, D, 0)), Vector((0, -1, 0)), Vector((-1, 0, 0)), d)]


def frame_matrix(origin, u, n, along, z):
    """Module space (x in [-3, 0] along the facade, front towards -y, z up) to building space."""
    side = -n
    m = Matrix(((u.x, side.x, 0, 0), (u.y, side.y, 0, 0), (0, 0, 1, 0), (0, 0, 0, 1)))
    m.translation = origin + u * along + Vector((0, 0, z))
    return m


def window_bounds(me):
    """Bounding box of the glass faces (module space): where the interior card goes."""
    lo = Vector((1e9, 1e9, 1e9))
    hi = Vector((-1e9, -1e9, -1e9))
    found = False
    for p in me.polygons:
        mat = me.materials[p.material_index]
        if not mat or "glass" not in mat.name:
            continue
        for vi in p.vertices:
            c = me.vertices[vi].co
            lo = Vector(map(min, lo, c))
            hi = Vector(map(max, hi, c))
            found = True
    return (lo, hi) if found else None


def room_card(b, lo, hi, matrix, row, rooms):
    """Interior-mapped card just behind the glass (module space bounds lo..hi, glass plane at max y). The engine
    traces a virtual room behind it from the atlas cell (material extra "tefra_interior")."""
    front = max(hi.y, lo.y) + 0.02  # module +y is inside
    cell = b.rnd.randrange(4)
    u0, v0, du, dv = cell * 0.25, row / 3.0, 0.25, 1.0 / 3.0
    # Inset 4 % of the cell, as the shader expects, so filtering never bleeds into the neighbour cell.
    iu, iv = du * 0.04, dv * 0.04
    uvs = [(u0 + iu, v0 + iv), (u0 + du - iu, v0 + iv), (u0 + du - iu, v0 + dv - iv), (u0 + iu, v0 + dv - iv)]
    mat = rooms[row != ROW_DARK]
    pad = 0.12
    x0, x1, z0, z1 = lo.x - pad, hi.x + pad, lo.z - pad, hi.z + pad
    q = [Vector((x0, front, z0)), Vector((x1, front, z0)), Vector((x1, front, z1)), Vector((x0, front, z1))]
    b.add_quad([matrix @ c for c in q], uvs, mat)


def add_room(b, me, matrix, lit_ratio, rooms, shop=False):
    wb = window_bounds(me)
    if not wb:
        return
    lit = b.rnd.random() < lit_ratio
    row = (ROW_SHOP if shop else ROW_LIT) if lit else ROW_DARK
    room_card(b, wb[0], wb[1], matrix, row, rooms)


def add_shopfront(b, k, matrix, rooms, lit):
    """Ground-floor shop: the large door opening glazed edge to edge, a thin metal frame and a bright interior."""
    frame = simple_material("city_shop_frame", (0.05, 0.05, 0.055), 0.35, 0.9)
    if k == "apt":
        x0, x1, z0, z1 = -2.54, -0.46, 0.0, 2.81
    else:
        x0, x1, z0, z1 = -2.66, -0.34, 0.0, 2.74
    t = 0.06
    y0, y1 = -0.02, 0.06
    # Frame: sill, head, jambs and a transom bar.
    for lo, hi in [((x0, y0, z0), (x1, y1, z0 + 0.35)), ((x0, y0, z1 - t), (x1, y1, z1)),
                   ((x0, y0, z0), (x0 + t, y1, z1)), ((x1 - t, y0, z0), (x1, y1, z1)),
                   ((x0, y0, 2.2), (x1, y1, 2.2 + t)), ((-1.5 - t / 2, y0, z0), (-1.5 + t / 2, y1, 2.2))]:
        b.add_box_m(matrix, lo, hi, frame)
    room_card(b, Vector((x0, 0.0, z0 + 0.35)), Vector((x1, 0.06, z1)), matrix,
              ROW_SHOP if lit else ROW_DARK, rooms)


def bounds(me):
    lo = Vector((min(v.co.x for v in me.vertices), min(v.co.y for v in me.vertices), min(v.co.z for v in me.vertices)))
    hi = Vector((max(v.co.x for v in me.vertices), max(v.co.y for v in me.vertices), max(v.co.z for v in me.vertices)))
    return lo, hi


def place_piece(b, me, origin, u, n, centre, z, sz=1.0):
    """Puts a wall-mounted piece (fire escape) centred at `centre` along the facade, bottom at z, its back on the
    wall (module space: outward is -y)."""
    lo, hi = bounds(me)
    local = Matrix.Translation((-(lo.x + hi.x) * 0.5, -hi.y - 0.03, -lo.z * sz)) if sz == 1.0 else \
        Matrix.Translation((-(lo.x + hi.x) * 0.5, -hi.y - 0.03, 0)) @ Matrix.Diagonal((1, 1, sz, 1)) @ \
        Matrix.Translation((0, 0, -lo.z))
    b.add_mesh(me, frame_matrix(origin, u, n, centre, z) @ local, drop_glass=False)


def add_pipe(b, p0, h, mat, r=0.06, sides=10):
    """Vertical drainpipe from p0 up h metres."""
    uv = b.bm.loops.layers.uv.verify()
    ring0, ring1 = [], []
    for i in range(sides):
        a = 2 * math.pi * i / sides
        off = Vector((math.cos(a) * r, math.sin(a) * r, 0))
        ring0.append(b.bm.verts.new(p0 + off))
        ring1.append(b.bm.verts.new(p0 + off + Vector((0, 0, h))))
    for i in range(sides):
        j = (i + 1) % sides
        f = b.bm.faces.new([ring0[i], ring0[j], ring1[j], ring1[i]])
        f.material_index = b.mat_index(mat)
        for loop in f.loops:
            loop[uv].uv = (0.5, 0.5)


def build(name, kit, w, d, floors, seed, lit_ratio, rooms):
    b = Builder(name, seed)
    rnd = b.rnd
    k = kit
    lib = lambda n: LIB.get(f"{k}:{n}_01") or LIB[f"{k}:{n}"]
    has = lambda n: f"{k}:{n}_01" in LIB or f"{k}:{n}" in LIB

    def lib_far(n):
        for key in (f"{k}:{n}_01#lo", f"{k}:{n}#lo"):
            if key in LIB:
                return LIB[key]
        return lib(n)
    # Upper-floor window palette per building: two window types for a coherent facade.
    if k == "apt":
        win_types = rnd.sample(["window_centered_large", "window_centered_double", "window_centered_small",
                                "window_offset_small", "door_window_small"], 2)
    else:
        win_types = rnd.sample(["window_tall_large", "window_tall_small", "window_centered_large",
                                "window_centered_double", "window_centered_medium"], 2)
    pattern = [rnd.choice(win_types) for _ in range(max(w, d))]
    metal = simple_material("city_pipe", (0.18, 0.19, 0.2), 0.4, 0.85)
    sides = side_frames(w, d)
    for si, (origin, u, n, count) in enumerate(sides):
        door_at = rnd.randrange(count)
        for i in range(count):
            along = (i + 1) * M
            for f in range(floors):
                z = f * M
                mat = frame_matrix(origin, u, n, along, z)
                obj = None
                if f == 0:
                    # Ground floor: shops edge to edge, one entrance per side.
                    if i == door_at:
                        wall, obj = ("wall_door_centered_large", "door_centered_large") if k == "apt" else \
                            ("wall_door_recessed_large", "door_recessed_large")
                        b.add_mesh(lib(wall), mat)
                        b.add_mesh(lib(obj), mat)
                    elif k == "apt":
                        b.add_mesh(lib("wall_door_centered_large"), mat)
                        add_shopfront(b, k, mat, rooms, rnd.random() < min(lit_ratio * 1.9, 0.92))
                    else:
                        if rnd.random() < 0.5:
                            b.add_mesh(lib("wall_door_recessed_large"), mat)
                            add_shopfront(b, k, mat, rooms, rnd.random() < min(lit_ratio * 1.9, 0.92))
                        else:
                            b.add_mesh(lib("wall_window_tall_large"), mat)
                            ome = lib("window_tall_large")
                            b.add_mesh(ome, mat)
                            add_room(b, ome, mat, min(lit_ratio * 1.9, 0.92), rooms, shop=True)
                else:
                    obj = pattern[i % len(pattern)] if (si % 2 == 0) else pattern[(i + 1) % len(pattern)]
                    if not has("wall_" + obj):
                        obj = None
                    far = f >= 4
                    wkey = "wall_" + obj if obj else "wall_standard_standard"
                    b.add_mesh(lib_far(wkey) if far else lib(wkey), mat)
                    if obj:
                        ome = lib_far(obj) if far else lib(obj)
                        b.add_mesh(ome, mat)
                        add_room(b, ome, mat, lit_ratio, rooms)
                    # Window AC units where the window leaves wall free (offset windows).
                    if obj == "window_offset_small" and "aircon" in LIB and f < 7 and rnd.random() < 0.35:
                        b.add_mesh(LIB["aircon"], mat @ Matrix.Translation((-2.15, -0.2, 0.5)), drop_glass=False)
                # Bands: plinth at the foot, a band at every floor line, cornice over the ground floor.
                if f == 0:
                    b.add_mesh(lib("base_standard" if k == "apt" else "base_standard_standard"), mat)
                elif k == "apt":
                    b.add_mesh(lib("dado_standard_standard"), frame_matrix(origin, u, n, along, z - 0.25))
                else:
                    b.add_mesh(lib("cornice02_standard_standard"), frame_matrix(origin, u, n, along, z - 0.08))
                if f == 1:
                    cname = "cornice_standard_standard" if k == "apt" else "cornice01_standard_standard"
                    b.add_mesh(lib(cname), frame_matrix(origin, u, n, along, z - 0.45 if k == "apt" else z - 0.3))
            top = frame_matrix(origin, u, n, along, floors * M)
            if k == "fac":
                b.add_mesh(lib("cornice01_standard_standard"), frame_matrix(origin, u, n, along, floors * M - 0.2))
            b.add_mesh(lib("crown_standard_standard"), top)
        # Corner pier per floor and a crown pier on top.
        for f in range(floors):
            b.add_mesh(lib("wall_pier_corner"), frame_matrix(origin, u, n, 0.0, f * M))
        b.add_mesh(lib("crown_pier_corner"), frame_matrix(origin, u, n, 0.0, floors * M))
        # Fire escape on a side facade: platforms at every floor, stairs between them, ladder at the bottom.
        if si in (1, 3) and floors >= 4 and "fe:platform_middle" in LIB and (si == 1 or rnd.random() < 0.4):
            centre = (rnd.randrange(1, count - 1) + 1) * M if count > 2 else count * M * 0.5
            top_floor = min(floors, 9)
            for f in range(1, top_floor):
                z = f * M - 0.05
                place_piece(b, LIB["fe:platform_middle"], origin, u, n, centre, z - 0.35)
                place_piece(b, LIB["fe:railing_middle"], origin, u, n, centre, z + 0.02)
                if f + 1 < top_floor:
                    place_piece(b, LIB["fe:stairs"], origin, u, n, centre, z, sz=M / 4.01)
            place_piece(b, LIB["fe:ladder_bottom"], origin, u, n, centre + 1.9, 0.9, sz=(M - 0.9) / 6.47)
        # Drainpipe down one corner of each facade.
        if si % 2 == 0:
            add_pipe(b, origin + u * 0.35 + n * 0.12, floors * M, metal)
    # Roof slab, interior core and rooftop units.
    W, D, H = w * M, d * M, floors * M
    roof = simple_material("city_roof", (0.09, 0.09, 0.095), 0.75)
    b.add_box((0.02, 0.02, H - 0.3), (W - 0.02, D - 0.02, H - 0.05), roof)
    core = simple_material("city_core", (0.02, 0.02, 0.02), 0.9)
    b.add_box((0.5, 0.5, 0.0), (W - 0.5, D - 0.5, H - 0.3), core, 0.1)
    roof_metal = simple_material("city_roof_metal", (0.32, 0.33, 0.34), 0.45, 0.8)
    for _ in range(rnd.randint(1, 3)):
        sx, sy = rnd.uniform(1.2, 2.2), rnd.uniform(1.0, 1.8)
        x, y = rnd.uniform(1.0, W - 1.0 - sx), rnd.uniform(1.0, D - 1.0 - sy)
        b.add_box((x, y, H - 0.05), (x + sx, y + sy, H + rnd.uniform(0.8, 1.4)), roof_metal, 0.5)
    if "aircon" in LIB:
        for _ in range(rnd.randint(2, 5)):
            m = Matrix.Translation((rnd.uniform(1.0, W - 1.0), rnd.uniform(1.0, D - 1.0), H - 0.05)) @ \
                Matrix.Rotation(rnd.choice([0, math.pi / 2, math.pi, -math.pi / 2]), 4, "Z")
            b.add_mesh(LIB["aircon"], m, drop_glass=False)
    ob = b.finish()
    # Origin at the footprint centre, ground level: the template places buildings by their centre.
    ob.data.transform(Matrix.Translation((-W / 2, -D / 2, 0)))
    return ob


def metres_per_uv(me):
    """Average ratio of model length to UV length over the edges of a mesh's faces."""
    uv = me.uv_layers.active.data
    num = den = 0.0
    for p in me.polygons:
        li = list(p.loop_indices)
        for a, b in zip(li, li[1:] + li[:1]):
            d3 = (me.vertices[me.loops[a].vertex_index].co - me.vertices[me.loops[b].vertex_index].co).length
            d2 = (Vector(uv[a].uv) - Vector(uv[b].uv)).length
            if d2 > 1e-6:
                num += d3
                den += d2
    return num / den if den else 1.0


def setup_parallax(tex_dir):
    """Parallax occlusion on the facade walls: brick uses Poly Haven's displacement map; the plaster kit has none,
    so its height comes from the ambient occlusion channel (cavities low). Depth is given in metres and converted to
    UV units for the engine (material extras "tefra_parallax" / "tefra_height")."""
    import numpy as np
    jobs = [("modular_factory_facade_brick", "fac:wall_standard_standard_01", 0.018, "disp"),
            ("modular_urban_apartments_facade_plaster", "apt:wall_standard_standard_01", 0.006, "ao")]
    for mat_name, ref, depth_m, source in jobs:
        mat = MATS.get(mat_name)
        if not mat or ref not in LIB:
            continue
        fname = f"{mat_name}_height_1k.jpg"
        dst = os.path.join(tex_dir, fname)
        if source == "disp":
            name = KITS["fac"]
            url = (f"https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/{name}/"
                   f"{name}_brick_disp_1k.jpg")
            opener = urllib.request.build_opener()
            opener.addheaders = [("User-Agent", "tefra-asset-fetch/1.0")]
            with opener.open(url) as r, open(dst, "wb") as f:
                f.write(r.read())
        else:
            arm = next((n.image for n in mat.node_tree.nodes if n.type == "TEX_IMAGE" and n.image and
                        "_arm_" in (n.image.name + n.image.filepath)), None) or \
                next(i for i in bpy.data.images if mat_name + "_arm" in (i.name + i.filepath))
            w, h = arm.size
            px = np.empty(w * h * 4, dtype=np.float32)
            arm.pixels.foreach_get(px)
            ao = px[0::4]
            # Smooth AO reads as a height field: wide cavities (cracks, pits) sink, flat plaster stays on top.
            height = np.clip((ao - 0.55) / 0.45, 0.0, 1.0) ** 0.7
            out = bpy.data.images.new("height", w, h, alpha=False)
            hp = np.repeat(height, 4)
            hp[3::4] = 1.0
            out.pixels.foreach_set(hp)
            settings = bpy.context.scene.render.image_settings
            settings.file_format = "JPEG"
            settings.quality = 90
            out.save_render(dst)
            bpy.data.images.remove(out)
        mpu = metres_per_uv(LIB[ref])
        mat["tefra_parallax"] = depth_m / mpu
        mat["tefra_height"] = "textures/" + fname
        print(f"parallax: {mat_name} depth {depth_m} m = {depth_m / mpu:.4f} uv ({mpu:.2f} m per uv)")


def export(ob, path):
    bpy.ops.object.select_all(action="DESELECT")
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.export_scene.gltf(filepath=path, export_format="GLTF_SEPARATE", export_texture_dir="textures",
                              use_selection=True, export_apply=True, export_yup=True, export_image_format="AUTO",
                              export_materials="EXPORT", export_extras=True)


# Building catalogue: name, kit, footprint (modules), floors, seed, lit fraction.
CATALOGUE = [
    ("apt_a", "apt", 6, 6, 6, 11, 0.45),
    ("apt_b", "apt", 6, 6, 8, 12, 0.4),
    ("apt_c", "apt", 6, 6, 5, 13, 0.5),
    ("apt_tower", "apt", 6, 6, 14, 14, 0.35),
    ("loft_a", "fac", 6, 6, 5, 21, 0.35),
    ("loft_b", "fac", 6, 6, 7, 22, 0.4),
    ("loft_tower", "fac", 6, 6, 10, 23, 0.3),
    ("warehouse", "fac", 6, 6, 3, 24, 0.3),
]


def main():
    reset()
    os.makedirs(os.path.join(OUT, "textures"), exist_ok=True)
    for tag, name in KITS.items():
        load_kit(tag, fetch(name, TEX_RES))
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=fetch("modular_fire_escape", TEX_RES))
    for o in set(bpy.data.objects) - before:
        if o.type == "MESH":
            LIB["fe:" + o.name.split(".")[0].replace("modular_fire_escape_", "")] = o.data
        bpy.data.objects.remove(o)
    ac = fetch("exterior_aircon_unit", TEX_RES)
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=ac)
    for o in set(bpy.data.objects) - before:
        if o.type == "MESH" and "rust" not in o.name:
            me = o.data.copy()
            LIB["aircon"] = me
        bpy.data.objects.remove(o)
    simplify_library()
    if os.environ.get("CITY_STATS"):
        for key, me in sorted(LIB.items()):
            print(f"  {key:50s} {sum(len(p.vertices) - 2 for p in me.polygons)}")
    setup_parallax(os.path.join(OUT, "textures"))
    atlas = make_room_atlas(os.path.join(OUT, "textures", "city_rooms.png"))
    rooms = {True: room_material(atlas, True), False: room_material(atlas, False)}
    only = os.environ.get("CITY_ONLY")
    for name, kit, w, d, floors, seed, lit in CATALOGUE:
        if only and name not in only.split(","):
            continue
        ob = build(name, kit, w, d, floors, seed, lit, rooms)
        tris = sum(len(p.vertices) - 2 for p in ob.data.polygons)
        print(f"{name}: {w * M:.0f} x {d * M:.0f} x {floors * M:.0f} m, {tris} tris, {len(ob.data.materials)} mats")
        export(ob, os.path.join(OUT, name + ".gltf"))
        bpy.data.objects.remove(ob)
    recompress(os.path.join(OUT, "textures"))


def recompress(folder, quality=80):
    """Re-encodes the copied kit JPEGs a little smaller (the repository ships them)."""
    settings = bpy.context.scene.render.image_settings
    settings.file_format = "JPEG"
    settings.quality = quality
    for f in sorted(os.listdir(folder)):
        if not f.endswith(".jpg") or os.path.getsize(os.path.join(folder, f)) < 300_000:
            continue
        img = bpy.data.images.load(os.path.join(folder, f))
        img.save_render(os.path.join(folder, f))
        bpy.data.images.remove(img)


if __name__ == "__main__":
    main()
