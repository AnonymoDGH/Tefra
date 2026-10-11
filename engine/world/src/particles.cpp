#include "tefra/world/particles.h"

#include "tefra/world/datamodel.h"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace tefra::world {

namespace {

constexpr u32 kMaxPerEmitter = 5000;
constexpr f32 kPi = 3.14159265f;

f32 srgb_to_linear(f32 c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }
vec3 to_linear(vec3 c) { return {srgb_to_linear(c.x), srgb_to_linear(c.y), srgb_to_linear(c.z)}; }

u32 shape_of(std::string_view tex) {
    static constexpr std::string_view kNames[] = {"circle", "smoke", "spark", "star", "fire", "snow", "ring"};
    if (tex.rfind("builtin:", 0) == 0) tex.remove_prefix(8);
    for (u32 i = 0; i < std::size(kNames); ++i)
        if (tex == kNames[i]) return i;
    return 0;
}

// Local emission direction of a NormalId (Top, Bottom, Front, Back, Left, Right); parts face -Z.
vec3 face_dir(u32 n) {
    static const vec3 kDirs[] = {{0, 1, 0}, {0, -1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}};
    return kDirs[n < 6 ? n : 0];
}

u32 enum_of(const Instance& i, std::string_view prop) {
    const Value* v = i.get(prop);
    return v ? v->as_enum() : 0;
}

} // namespace

f32 ParticleSystem::random01() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return f32(rng_ >> 8) * (1.0f / 16777216.0f);
}

void ParticleSystem::clear() {
    emitters_.clear();
    out_.clear();
    emitter_ids_.clear();
    scanned_revision_ = ~0ull;
}

void ParticleSystem::spawn(const Instance& e, const Instance& part, Emitter& em, u32 count) {
    const vec3 origin = part.get_vec3("Position");
    const quat q = orientation_to_quat(part.get_vec3("Orientation"));
    const vec3 half = part.get_vec3("Size", {1, 1, 1}) * 0.5f;
    const vec3 dir_l = face_dir(enum_of(e, "EmissionDirection"));
    const u32 shape = enum_of(e, "Shape");
    const f32 var = clamp(e.get_float("Variation", 0.3f), 0.0f, 1.0f);
    const f32 life = max(e.get_float("Lifetime", 2.0f), 0.01f);
    const f32 speed = e.get_float("Speed", 5.0f);
    const f32 spread = clamp(e.get_float("SpreadAngle", 15.0f), 0.0f, 180.0f) * (kPi / 180.0f);
    const f32 rot_speed = e.get_float("RotSpeed") * (kPi / 180.0f);
    // Orthonormal basis around the emission direction (local space).
    const vec3 t0 = std::fabs(dir_l.y) < 0.9f ? normalize(cross(dir_l, vec3{0, 1, 0})) : vec3{1, 0, 0};
    const vec3 t1 = cross(dir_l, t0);
    for (u32 k = 0; k < count && em.ps.size() < kMaxPerEmitter; ++k) {
        auto sym = [&] { return random01() * 2.0f - 1.0f; };
        vec3 lp{};
        switch (shape) {
        case 1: { // Sphere: uniform in the ellipsoid
            vec3 r;
            do r = {sym(), sym(), sym()};
            while (dot(r, r) > 1.0f);
            lp = r * half;
            break;
        }
        case 2: { // Disc: on the plane through the centre, across the emission direction
            const f32 a = random01() * 2.0f * kPi, rr = std::sqrt(random01());
            lp = (t0 * std::cos(a) + t1 * std::sin(a)) * rr * half;
            break;
        }
        case 3: break; // Point
        default: lp = vec3{sym(), sym(), sym()} * half; break;
        }
        // Direction: uniform in the cone of half-angle `spread` around dir_l.
        const f32 cos_t = 1.0f - random01() * (1.0f - std::cos(spread));
        const f32 sin_t = std::sqrt(max(0.0f, 1.0f - cos_t * cos_t)), phi = random01() * 2.0f * kPi;
        const vec3 d = dir_l * cos_t + (t0 * std::cos(phi) + t1 * std::sin(phi)) * sin_t;
        Particle p;
        p.pos = origin + rotate(q, lp);
        p.vel = rotate(q, d) * (speed * (1.0f + var * sym()));
        p.life = life * (1.0f + var * sym());
        p.size_scale = 1.0f + var * sym();
        p.rot = random01() * 2.0f * kPi;
        p.rot_speed = rot_speed * (random01() < 0.5f ? -1.0f : 1.0f) * (0.5f + random01());
        em.ps.push_back(p);
    }
}

void ParticleSystem::update(DataModel& dm, f32 dt, vec3 camera_pos, u32 max_particles) {
    ++frame_;
    dt = clamp(dt, 0.0f, 0.1f);
    if (&dm != model_) {
        clear();
        model_ = &dm;
    }
    if (dm.revision() != scanned_revision_) {
        scanned_revision_ = dm.revision();
        emitter_ids_.clear();
        if (const Instance* ws = dm.workspace())
            for (const Instance* i : ws->descendants())
                if (i->is_a("ParticleEmitter")) emitter_ids_.push_back(i->id());
    }
    u32 total = 0;
    for (const auto& [id, em] : emitters_) total += static_cast<u32>(em.ps.size());

    for (u64 id : emitter_ids_) {
        Instance* e = dm.find(id);
        if (!e) continue;
        Emitter& em = emitters_[id];
        em.seen = frame_;
        const Instance* part = e->parent();
        while (part && !part->is_a("BasePart")) part = part->parent();
        if (!part) {
            em.ps.clear();
            continue;
        }
        const f32 ts = max(e->get_float("TimeScale", 1.0f), 0.0f);
        const f32 step = dt * ts;
        // Move with the part when locked.
        const vec3 origin = part->get_vec3("Position");
        if (e->get_bool("LockedToPart") && em.has_origin) {
            const vec3 delta = origin - em.last_origin;
            for (Particle& p : em.ps) p.pos += delta;
        }
        em.last_origin = origin;
        em.has_origin = true;
        // Integrate and age.
        const vec3 acc = e->get_vec3("Acceleration");
        const f32 drag = std::exp(-max(e->get_float("Drag"), 0.0f) * step);
        for (Particle& p : em.ps) {
            p.vel = (p.vel + acc * step) * drag;
            p.pos += p.vel * step;
            p.rot += p.rot_speed * step;
            p.age += step;
        }
        const usize before = em.ps.size();
        em.ps.erase(std::remove_if(em.ps.begin(), em.ps.end(), [](const Particle& p) { return p.age >= p.life; }),
                    em.ps.end());
        total -= static_cast<u32>(before - em.ps.size());
        // Spawn: continuous rate plus bursts requested by Emit(n).
        u32 n = 0;
        if (e->get_bool("Enabled", true)) {
            em.accum += max(e->get_float("Rate", 20.0f), 0.0f) * step;
            n = static_cast<u32>(em.accum);
            em.accum -= f32(n);
        }
        if (const Value* burst = e->get("EmitCount"); burst && burst->as_int() > 0) {
            n += static_cast<u32>(min<i64>(burst->as_int(), kMaxPerEmitter));
            e->set("EmitCount", Value::integer(0), true);
        }
        n = min(n, max_particles > total ? max_particles - total : 0u);
        if (n) {
            const usize was = em.ps.size();
            spawn(*e, *part, em, n);
            total += static_cast<u32>(em.ps.size() - was);
        }
    }
    // Emitters that left the Workspace.
    for (auto it = emitters_.begin(); it != emitters_.end();)
        it = it->second.seen == frame_ ? std::next(it) : emitters_.erase(it);

    // GPU list, back to front.
    out_.clear();
    for (u64 id : emitter_ids_) {
        auto it = emitters_.find(id);
        const Instance* e = dm.find(id);
        if (it == emitters_.end() || !e || it->second.ps.empty()) continue;
        const vec3 c0 = to_linear(e->get_vec3("Color", {1, 1, 1})), c1 = to_linear(e->get_vec3("ColorEnd", {1, 1, 1}));
        const f32 s0 = e->get_float("Size", 1.0f), s1 = e->get_float("SizeEnd", 1.0f);
        const f32 tr0 = e->get_float("Transparency"), tr1 = e->get_float("TransparencyEnd", 1.0f);
        const u32 shape = shape_of(e->get_string("Texture"));
        const f32 emission = clamp(e->get_float("LightEmission"), 0.0f, 1.0f);
        const f32 stretch = max(e->get_float("Stretch"), 0.0f), bright = max(e->get_float("Brightness", 1.0f), 0.0f);
        for (const Particle& p : it->second.ps) {
            const f32 t = clamp(p.age / p.life, 0.0f, 1.0f);
            // Quick fade-in over the first 8% so particles do not pop.
            const f32 fade_in = min(1.0f, 0.25f + t / 0.08f);
            render::ParticleGPU g;
            g.pos_size = vec4(p.pos, max(lerp(s0, s1, t) * p.size_scale, 0.001f));
            g.color = vec4(c0 + (c1 - c0) * t, clamp(1.0f - lerp(tr0, tr1, t), 0.0f, 1.0f) * fade_in);
            g.vel_rot = vec4(p.vel, p.rot);
            g.shape = shape;
            g.emission = emission;
            g.stretch = stretch;
            g.brightness = bright;
            if (g.color.w > 0.002f) out_.push_back(g);
        }
    }
    std::sort(out_.begin(), out_.end(), [camera_pos](const render::ParticleGPU& a, const render::ParticleGPU& b) {
        const vec3 da = vec3(a.pos_size.x, a.pos_size.y, a.pos_size.z) - camera_pos;
        const vec3 db = vec3(b.pos_size.x, b.pos_size.y, b.pos_size.z) - camera_pos;
        return dot(da, da) > dot(db, db);
    });
}

} // namespace tefra::world
