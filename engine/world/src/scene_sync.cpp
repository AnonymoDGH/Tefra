#include "tefra/world/scene_sync.h"

#include "primitives.h"

#include "tefra/core/fs.h"
#include "tefra/core/log.h"
#include "tefra/renderer/gltf.h"
#include "tefra/renderer/scene.h"

#include <cmath>
#include <cstdio>

namespace tefra::world {

namespace {
f32 srgb_to_linear(f32 c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }

struct MaterialLook {
    f32 roughness, metallic, emissive;
};
// Physical look per Material enum entry (see classes.cpp for the order).
MaterialLook material_look(u32 m) {
    static const MaterialLook table[] = {
        {0.55f, 0.0f, 0.0f}, // Plastic
        {0.3f, 0.0f, 0.0f},  // SmoothPlastic
        {0.7f, 0.0f, 0.0f},  // Wood
        {0.75f, 0.0f, 0.0f}, // WoodPlanks
        {0.35f, 1.0f, 0.0f}, // Metal
        {0.45f, 1.0f, 0.0f}, // DiamondPlate
        {0.9f, 0.0f, 0.0f},  // Concrete
        {0.85f, 0.0f, 0.0f}, // Brick
        {0.9f, 0.0f, 0.0f},  // Cobblestone
        {0.6f, 0.0f, 0.0f},  // Granite
        {0.25f, 0.0f, 0.0f}, // Marble
        {0.8f, 0.0f, 0.0f},  // Slate
        {0.95f, 0.0f, 0.0f}, // Sand
        {0.9f, 0.0f, 0.0f},  // Grass
        {0.95f, 0.0f, 0.0f}, // Ground
        {0.85f, 0.0f, 0.0f}, // Rock
        {0.08f, 0.0f, 0.0f}, // Ice
        {0.05f, 0.0f, 0.0f}, // Glass
        {0.5f, 0.0f, 1.0f},  // Neon
        {0.95f, 0.0f, 0.0f}, // Fabric
        {0.2f, 1.0f, 0.0f},  // Foil
    };
    return m < std::size(table) ? table[m] : table[0];
}

bool is_visible_part(const Instance& i, const Instance* workspace) {
    return i.is_a("BasePart") && workspace && i.is_descendant_of(workspace) && i.get_float("Transparency") < 0.99f;
}
} // namespace

SceneSync::SceneSync(render::Scene& scene, DataModel& model) : scene_(scene), model_(&model) { attach_listener(); }

void SceneSync::set_model(DataModel& model) {
    model_->remove_listener(listener_);
    model_ = &model;
    attach_listener();
    structure_dirty_ = true;
    dirty_parts_.clear();
}

void SceneSync::attach_listener() {
    WorldListener l;
    l.added = [this](Instance& i) {
        if (i.is_a("BasePart")) structure_dirty_ = true;
    };
    l.removing = [this](Instance& i) {
        if (i.is_a("BasePart")) structure_dirty_ = true;
    };
    l.reparented = [this](Instance& i, Instance*) {
        if (i.is_a("BasePart") || !i.children().empty()) structure_dirty_ = true;
    };
    l.changed = [this](Instance& i, u32 prop) {
        if (!i.is_a("BasePart")) return;
        const std::string& n = i.cls().prop(prop)->name;
        if (n == "Position" || n == "Orientation" || n == "Size") {
            dirty_parts_.push_back(i.id());
        } else if (n == "Color" || n == "Material" || n == "Reflectance") {
            materials_dirty_ = true;
            dirty_parts_.push_back(i.id());
        } else if (n == "Transparency" || n == "Shape" || n == "MeshId" || n == "CastShadow") {
            structure_dirty_ = true;
        }
    };
    listener_ = model_->add_listener(std::move(l));
}

SceneSync::~SceneSync() { model_->remove_listener(listener_); }

u32 SceneSync::material_for(const Instance& part) {
    const vec3 c = part.get_vec3("Color", {0.64f, 0.64f, 0.64f});
    const u32 mat = part.get("Material") ? part.get("Material")->as_enum() : 0;
    const f32 refl = part.get_float("Reflectance");
    char key[96];
    std::snprintf(key, sizeof(key), "%.3f,%.3f,%.3f|%u|%.2f", f64(c.x), f64(c.y), f64(c.z), mat, f64(refl));
    auto it = material_cache_.find(key);
    if (it != material_cache_.end()) return it->second;
    const MaterialLook look = material_look(mat);
    render::MaterialGPU m{};
    const vec3 lin{srgb_to_linear(c.x), srgb_to_linear(c.y), srgb_to_linear(c.z)};
    m.base_color = vec4(lin, 1.0f);
    m.roughness = lerp(look.roughness, 0.05f, refl);
    m.metallic = max(look.metallic, refl * 0.8f);
    m.base_color_tex = scene_.white_texture();
    m.normal_tex = scene_.flat_normal_texture();
    m.orm_tex = scene_.white_texture();
    m.emissive_tex = render::kNoTexture;
    m.occlusion_strength = 0.0f;
    if (look.emissive > 0.0f) m.emissive_cutoff = vec4(lin * 40.0f, 0.0f); // Neon glows
    const u32 idx = scene_.add_material(m);
    material_cache_[key] = idx;
    materials_dirty_ = true;
    return idx;
}

u32 SceneSync::mesh_for(const Instance& part) {
    u32 shape = 0;
    if (const Value* v = part.get("Shape")) shape = v->as_enum();
    if (shape > 3) shape = 0;
    if (primitive_mesh_[shape] == ~0u) {
        render::MeshData md;
        switch (shape) {
        case 1: md = render::make_sphere(0.5f, 40, 20); break;
        case 2: md = make_unit_cylinder(); break;
        case 3: md = make_unit_wedge(); break;
        default: md = render::make_box({0.5f, 0.5f, 0.5f}); break;
        }
        primitive_mesh_[shape] = scene_.add_mesh(md);
        materials_dirty_ = true; // geometry needs an upload too
    }
    return primitive_mesh_[shape];
}

void SceneSync::write_transform(const Instance& part, u32 ri) {
    render::Instance& r = scene_.instances()[ri];
    const vec3 p = part.get_vec3("Position");
    r.position = {f64(p.x), f64(p.y), f64(p.z)};
    r.rotation = orientation_to_quat(part.get_vec3("Orientation"));
    const vec3 sz = part.get_vec3("Size", {1, 1, 1});
    r.scale = {max(sz.x, 0.001f), max(sz.y, 0.001f), max(sz.z, 0.001f)};
}

void SceneSync::rebuild() {
    std::vector<render::Instance>& out = scene_.instances();
    out.clear();
    parts_.clear();
    part_index_.clear();
    const Instance* ws = model_->workspace();
    if (!ws) return;
    for (Instance* i : ws->descendants()) {
        if (!is_visible_part(*i, ws)) continue;
        if (i->is_a("MeshPart")) {
            const std::string& path = i->get_string("MeshId");
            if (path.empty() || !file_exists(path.c_str())) continue;
            auto it = gltf_cache_.find(path);
            if (it == gltf_cache_.end()) {
                const usize first = out.size();
                render::GltfLoadOptions opt;
                if (!render::load_gltf(scene_, path.c_str(), opt).ok) {
                    TEFRA_LOG_WARN("MeshPart %s: cannot load %s", i->full_name().c_str(), path.c_str());
                    gltf_cache_[path] = {};
                    continue;
                }
                std::vector<std::pair<u32, u32>> prims;
                for (usize k = first; k < out.size(); ++k) prims.push_back({out[k].mesh, out[k].material});
                out.resize(first);
                it = gltf_cache_.emplace(path, std::move(prims)).first;
                materials_dirty_ = true;
            }
            for (const auto& [mesh, mat] : it->second) {
                render::Instance r;
                r.mesh = mesh;
                r.material = mat;
                r.cast_shadows = i->get_bool("CastShadow", true);
                out.push_back(r);
                parts_.push_back({i, static_cast<u32>(out.size() - 1)});
                write_transform(*i, static_cast<u32>(out.size() - 1));
                // glTF meshes keep their own size: Size acts as a scale relative to 1 m.
            }
            part_index_[i->id()] = static_cast<u32>(parts_.size() - 1);
            continue;
        }
        render::Instance r;
        r.mesh = mesh_for(*i);
        r.material = material_for(*i);
        r.cast_shadows = i->get_bool("CastShadow", true);
        out.push_back(r);
        parts_.push_back({i, static_cast<u32>(out.size() - 1)});
        part_index_[i->id()] = static_cast<u32>(parts_.size() - 1);
        write_transform(*i, static_cast<u32>(out.size() - 1));
    }
}

bool SceneSync::update() {
    bool changed = false;
    if (structure_dirty_) {
        rebuild();
        structure_dirty_ = false;
        dirty_parts_.clear();
        changed = true;
    } else if (!dirty_parts_.empty()) {
        for (u64 id : dirty_parts_) {
            auto it = part_index_.find(id);
            if (it == part_index_.end()) continue;
            // A MeshPart may own several render instances: they are consecutive and end at this entry.
            const u32 last = it->second;
            const Instance* inst = parts_[last].inst;
            for (i64 k = i64(last); k >= 0 && parts_[usize(k)].inst == inst; --k) {
                write_transform(*inst, parts_[usize(k)].render_index);
                if (!inst->is_a("MeshPart"))
                    scene_.instances()[parts_[usize(k)].render_index].material = material_for(*inst);
            }
        }
        dirty_parts_.clear();
        changed = true;
    }
    if (materials_dirty_ || !uploaded_once_) {
        scene_.upload();
        materials_dirty_ = false;
        uploaded_once_ = true;
        changed = true;
    }
    return changed;
}

namespace {
// Ray vs oriented unit shape (local space: the part's box of half-size 0.5 after removing Size).
bool ray_box(vec3 o, vec3 d, vec3 half, f32& t, vec3& n) {
    f32 tmin = -1e30f, tmax = 1e30f;
    vec3 nmin{};
    const f32 oc[3] = {o.x, o.y, o.z}, dc[3] = {d.x, d.y, d.z}, hc[3] = {half.x, half.y, half.z};
    for (int a = 0; a < 3; ++a) {
        if (std::fabs(dc[a]) < 1e-9f) {
            if (oc[a] < -hc[a] || oc[a] > hc[a]) return false;
            continue;
        }
        f32 t0 = (-hc[a] - oc[a]) / dc[a], t1 = (hc[a] - oc[a]) / dc[a];
        f32 s = -1.0f;
        if (t0 > t1) {
            const f32 tmp = t0;
            t0 = t1;
            t1 = tmp;
            s = 1.0f;
        }
        if (t0 > tmin) {
            tmin = t0;
            nmin = vec3{a == 0 ? s : 0.0f, a == 1 ? s : 0.0f, a == 2 ? s : 0.0f};
        }
        tmax = min(tmax, t1);
        if (tmin > tmax) return false;
    }
    if (tmax < 0.0f) return false;
    t = tmin >= 0.0f ? tmin : tmax;
    n = nmin;
    return true;
}
} // namespace

RayHit SceneSync::raycast(vec3 origin, vec3 dir, f32 max_distance, const Instance* ignore) const {
    RayHit best;
    best.distance = max_distance;
    dir = normalize(dir);
    for (const PartEntry& e : parts_) {
        const Instance& p = *e.inst;
        if (&p == ignore) continue;
        const quat q = orientation_to_quat(p.get_vec3("Orientation"));
        const quat qi{-q.x, -q.y, -q.z, q.w};
        const vec3 size = p.get_vec3("Size", {1, 1, 1});
        const vec3 lo = rotate(qi, origin - p.get_vec3("Position"));
        const vec3 ld = rotate(qi, dir);
        f32 t = 0.0f;
        vec3 n{};
        u32 shape = p.get("Shape") ? p.get("Shape")->as_enum() : 0;
        bool hit;
        if (shape == 1) {
            // Ellipsoid: scale to a unit sphere.
            const vec3 s{max(size.x, 1e-3f) * 0.5f, max(size.y, 1e-3f) * 0.5f, max(size.z, 1e-3f) * 0.5f};
            const vec3 o2{lo.x / s.x, lo.y / s.y, lo.z / s.z}, d2{ld.x / s.x, ld.y / s.y, ld.z / s.z};
            const f32 a = dot(d2, d2), b = dot(o2, d2), c = dot(o2, o2) - 1.0f;
            const f32 disc = b * b - a * c;
            hit = disc >= 0.0f;
            if (hit) {
                t = (-b - std::sqrt(disc)) / a;
                if (t < 0.0f) t = (-b + std::sqrt(disc)) / a;
                hit = t >= 0.0f;
                const vec3 hp = o2 + d2 * t;
                n = normalize(vec3{hp.x / s.x, hp.y / s.y, hp.z / s.z});
            }
        } else {
            hit = ray_box(lo, ld, size * 0.5f, t, n);
        }
        if (hit && t < best.distance) {
            best.instance = e.inst;
            best.distance = t;
            best.position = origin + dir * t;
            best.normal = normalize(rotate(q, n));
        }
    }
    return best;
}

bool SceneSync::bounds(const Instance& inst, vec3& lo, vec3& hi) const {
    bool any = false;
    lo = vec3(1e30f);
    hi = vec3(-1e30f);
    auto add_part = [&](const Instance& p) {
        const quat q = orientation_to_quat(p.get_vec3("Orientation"));
        const vec3 h = p.get_vec3("Size", {1, 1, 1}) * 0.5f, c = p.get_vec3("Position");
        for (int k = 0; k < 8; ++k) {
            const vec3 corner{(k & 1) ? h.x : -h.x, (k & 2) ? h.y : -h.y, (k & 4) ? h.z : -h.z};
            const vec3 w = c + rotate(q, corner);
            lo = {min(lo.x, w.x), min(lo.y, w.y), min(lo.z, w.z)};
            hi = {max(hi.x, w.x), max(hi.y, w.y), max(hi.z, w.z)};
        }
        any = true;
    };
    if (inst.is_a("BasePart")) add_part(inst);
    for (const Instance* d : inst.descendants())
        if (d->is_a("BasePart")) add_part(*d);
    return any;
}

} // namespace tefra::world
