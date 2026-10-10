#include "tefra/renderer/vegetation.h"
#include "tefra/renderer/scene.h"

#include "tefra/core/log.h"
#include "tefra/rhi/device.h"
#include "tefra/rhi/upload.h"

#include <algorithm>
#include <cmath>

namespace tefra::render {
namespace {

struct Rng {
    u32 s;
    explicit Rng(u32 seed) : s(seed * 747796405u + 2891336453u) {}
    u32 next() {
        s = s * 747796405u + 2891336453u;
        u32 w = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u;
        return (w >> 22u) ^ w;
    }
    f32 f() { return static_cast<f32>(next() >> 8) * (1.0f / 16777216.0f); }
    f32 range(f32 a, f32 b) { return a + (b - a) * f(); }
};

f32 trunk_radius_at(f32 y, f32 h, f32 r0) {
    const f32 t = clamp(y / h, 0.0f, 1.0f);
    return r0 * (1.0f - 0.9f * t) * (1.0f + 0.55f * std::exp(-y * 2.5f)) + 0.01f;
}

// Per-tree trunk shape: irregular section, root buttresses, lean and bend. It has its own RNG so the crown
// layout of a seed does not change when the trunk does.
struct TrunkStyle {
    vec2 lean;       // horizontal drift per metre of height (x, z), about 0.7 deg at most
    vec2 bend;       // extra horizontal offset at the top (quadratic in height)
    f32 harm_amp[3]; // irregular section: amplitudes of angular harmonics 2, 3 and 4
    f32 harm_phase[3];
    u32 buttress_count; // 4..6 root buttresses
    f32 buttress_angle[6];
    f32 buttress_size[6]; // radial size relative to the trunk radius
    f32 buttress_height;  // metres
};

TrunkStyle make_trunk_style(u32 seed, f32 h) {
    Rng rng(seed * 2654435761u + 99u);
    TrunkStyle st{};
    const f32 lean_angle = rng.range(0.0f, 0.012f), lean_dir = rng.range(0.0f, kTwoPi);
    st.lean = {std::cos(lean_dir) * lean_angle, std::sin(lean_dir) * lean_angle};
    const f32 bend_dir = rng.range(0.0f, kTwoPi), bend_amt = h * rng.range(0.003f, 0.012f);
    st.bend = {std::cos(bend_dir) * bend_amt, std::sin(bend_dir) * bend_amt};
    const f32 amps[3] = {0.06f, 0.04f, 0.025f};
    for (int k = 0; k < 3; ++k) {
        st.harm_amp[k] = amps[k] * rng.range(0.5f, 1.0f);
        st.harm_phase[k] = rng.range(0.0f, kTwoPi);
    }
    st.buttress_count = 4 + rng.next() % 3;
    const f32 spacing = kTwoPi / f32(st.buttress_count);
    for (u32 i = 0; i < st.buttress_count; ++i) {
        st.buttress_angle[i] = f32(i) * spacing + rng.range(-0.3f, 0.3f) * spacing;
        st.buttress_size[i] = rng.range(0.7f, 1.05f);
    }
    st.buttress_height = h * rng.range(0.12f, 0.16f);
    return st;
}

// Horizontal position (x, z) of the trunk axis at height y.
vec2 trunk_axis(const TrunkStyle& st, f32 y, f32 h) {
    const f32 t = y / h;
    return st.lean * y + st.bend * (t * t);
}
vec3 axis_point(const TrunkStyle& st, f32 y, f32 h) {
    const vec2 a = trunk_axis(st, y, h);
    return {a.x, y, a.y};
}

// Trunk radius at height y and angle a. Below ground (y < 0) the flare keeps widening, so roots sink in.
f32 section_radius(const TrunkStyle& st, f32 a, f32 y, f32 h, f32 r0) {
    f32 irregular = 1.0f;
    for (int k = 0; k < 3; ++k) irregular += st.harm_amp[k] * std::sin(f32(k + 2) * a + st.harm_phase[k]);
    f32 r = trunk_radius_at(max(y, 0.0f), h, r0) * irregular;
    const f32 fall = y >= 0.0f ? std::pow(std::max(0.0f, 1.0f - y / st.buttress_height), 2.0f) : 1.0f;
    if (fall > 0.0f) {
        const f32 spacing = kTwoPi / f32(st.buttress_count), w = spacing * 0.28f;
        f32 bump = 0.0f;
        for (u32 i = 0; i < st.buttress_count; ++i) {
            const f32 d = a - st.buttress_angle[i];
            const f32 dw = d - kTwoPi * std::round(d / kTwoPi);
            bump += st.buttress_size[i] * std::exp(-(dw * dw) / (w * w));
        }
        r += r0 * bump * fall;
    }
    return r;
}

// Trunk as a ring grid along the axis. Normals come from the grid itself (the section is not a circle).
// Tangent sign -1 makes the bitangent point up (+V), so the "_gl" normal map's green channel faces up.
void add_trunk(MeshData& m, const TrunkStyle& st, f32 h, f32 r0, u32 segments, f32 ring_step, f32 top,
               bool dense_base) {
    std::vector<f32> ys = {-1.2f, 0.0f}; // start below ground: the buttresses sink into the soil
    if (dense_base) ys.insert(ys.end(), {0.25f, 0.6f, 1.1f, 1.8f, 2.8f});
    for (f32 y = ring_step; y < top - 0.05f; y += ring_step) ys.push_back(y);
    ys.push_back(top);
    std::sort(ys.begin(), ys.end());
    std::vector<f32> rings_y;
    for (f32 y : ys)
        if (rings_y.empty() || y - rings_y.back() > 0.08f) rings_y.push_back(y);

    const u32 rings = static_cast<u32>(rings_y.size()), stride = segments + 1;
    const u32 base = static_cast<u32>(m.vertices.size());
    std::vector<vec3> pos(usize(rings) * stride);
    for (u32 r = 0; r < rings; ++r) {
        const f32 y = rings_y[r];
        const vec3 c = axis_point(st, y, h);
        for (u32 s = 0; s <= segments; ++s) {
            const f32 a = f32(s) / f32(segments) * kTwoPi;
            const f32 rad = section_radius(st, a, y, h, r0);
            pos[usize(r) * stride + s] = {c.x + std::cos(a) * rad, y, c.z + std::sin(a) * rad};
        }
    }
    const f32 circumference = kTwoPi * r0;
    const f32 u_scale = std::max(1.0f, std::round(circumference / 0.9f)); // bark texel density ~0.9 m per tile
    for (u32 r = 0; r < rings; ++r) {
        const u32 rp = std::min(r + 1, rings - 1), rm = r > 0 ? r - 1 : 0;
        for (u32 s = 0; s <= segments; ++s) {
            const u32 s0 = s % segments, sn = (s0 + 1) % segments, sp = (s0 + segments - 1) % segments;
            const vec3 p = pos[usize(r) * stride + s];
            const vec3 t_angle = pos[usize(r) * stride + sn] - pos[usize(r) * stride + sp];
            const vec3 t_axis = pos[usize(rp) * stride + s0] - pos[usize(rm) * stride + s0];
            vec3 n = normalize(cross(t_axis, t_angle));
            const vec3 c = axis_point(st, rings_y[r], h);
            if (dot(n, p - c) < 0.0f) n = -n;
            const f32 y = rings_y[r];
            m.vertices.push_back(
                make_vertex(p, n, {f32(s) / f32(segments) * u_scale, y / 1.6f}, normalize(t_angle), -1.0f));
        }
    }
    for (u32 r = 0; r + 1 < rings; ++r)
        for (u32 s = 0; s < segments; ++s) {
            const u32 a = base + r * stride + s, b = a + stride, c = a + 1, d = b + 1;
            m.indices.insert(m.indices.end(), {a, b, c, c, b, d});
        }
}

// Branch card: a curved quad strip from `root` along `dir`, drooping, oriented by `side`.
// Foliage cards store a crown-occlusion factor in tangent_sign (their normal maps are flat).
void add_card(MeshData& m, vec3 root, vec3 dir, vec3 side, f32 length, f32 width, f32 droop, vec3 crown_center,
              f32 crown_radius) {
    const u32 segs = 3;
    const u32 base = static_cast<u32>(m.vertices.size());
    const vec3 card_n = normalize(cross(side, dir));
    for (u32 i = 0; i <= segs; ++i) {
        const f32 t = f32(i) / f32(segs);
        const vec3 c = root + dir * (length * t) - vec3{0.0f, droop * length * t * t, 0.0f};
        const f32 w = width * (1.0f - 0.35f * t);
        for (int k = 0; k < 2; ++k) {
            const vec3 p = c + side * (w * (k == 0 ? -0.5f : 0.5f));
            // Normals bent towards the crown's outward direction: soft, volumetric foliage shading.
            vec3 out = p - crown_center;
            out.y *= 0.6f;
            const vec3 n = normalize(card_n * 0.35f + normalize(out) * (0.65f + 0.35f * length / crown_radius));
            // Inner parts of the crown (near the trunk, low) receive less sky light.
            const f32 radial = tefra::length(vec3{p.x, 0.0f, p.z}) / max(crown_radius, 0.1f);
            const f32 ao = clamp(0.25f + 0.75f * radial, 0.25f, 1.0f);
            m.vertices.push_back(make_vertex(p, n, {t, k == 0 ? 0.0f : 1.0f}, dir, ao));
        }
    }
    for (u32 i = 0; i < segs; ++i) {
        const u32 a = base + i * 2, b = a + 1, c = a + 2, d = a + 3;
        m.indices.insert(m.indices.end(), {a, c, b, b, c, d});
    }
}

} // namespace

ConiferMeshes make_conifer(u32 seed, f32 h, f32 r0) {
    Rng rng(seed);
    ConiferMeshes out;
    out.height = h;
    const f32 crown_start = h * rng.range(0.28f, 0.40f);
    const f32 crown_r = h * 0.17f;
    out.crown_radius = crown_r;
    out.crown_base = crown_start;
    out.canopy_radius = 0.27f * (h - crown_start); // lowest whorl length, see the crown loop below

    const TrunkStyle st = make_trunk_style(seed, h);
    add_trunk(out.trunk_lod0, st, h, r0, 20, 0.9f, h * 0.97f, true);
    add_trunk(out.trunk_lod1, st, h, r0, 5, 4.0f, crown_start + 2.0f, false);

    // LOD0 crown: whorls of drooping branch cards.
    f32 y = crown_start;
    u32 whorl = 0;
    while (y < h - 0.4f) {
        const f32 t = (y - crown_start) / (h - crown_start);
        const u32 n = 6 + (rng.next() % 3);
        const f32 len = std::max(0.45f, (h - y) * 0.27f * rng.range(0.8f, 1.15f) * (1.0f - 0.2f * t));
        for (u32 i = 0; i < n; ++i) {
            const f32 a = f32(whorl) * 2.399963f + f32(i) / f32(n) * kTwoPi + rng.range(-0.3f, 0.3f);
            const vec3 dir = normalize(vec3{std::cos(a), rng.range(-0.15f, 0.05f), std::sin(a)});
            vec3 side = normalize(cross(vec3{0, 1, 0}, dir));
            const f32 tilt = rng.range(-0.5f, 0.5f);
            side = normalize(side * std::cos(tilt) + vec3{0, 1, 0} * std::sin(tilt));
            const vec3 axis = axis_point(st, y, h);
            const vec3 root{axis.x + dir.x * trunk_radius_at(y, h, r0), y, axis.z + dir.z * trunk_radius_at(y, h, r0)};
            add_card(out.crown_lod0, root, dir, side, len, len * 0.55f + 0.3f, 0.35f, axis, crown_r);
            // Second card crossed at ~70 degrees around the branch axis gives volume from every angle.
            const vec3 side2 = normalize(side * 0.34f + normalize(cross(dir, side)) * 0.94f);
            add_card(out.crown_lod0, root, dir, side2, len * 0.9f, len * 0.45f + 0.25f, 0.3f, axis, crown_r);
        }
        y += rng.range(0.32f, 0.48f);
        ++whorl;
    }
    // Leader at the top.
    for (int k = 0; k < 3; ++k) {
        const f32 a = f32(k) / 3.0f * kTwoPi;
        add_card(out.crown_lod0, axis_point(st, h - 1.2f, h),
                 normalize(vec3{std::cos(a) * 0.25f, 1.0f, std::sin(a) * 0.25f}),
                 normalize(vec3{-std::sin(a), 0, std::cos(a)}), 1.3f, 0.5f, 0.0f, axis_point(st, h - 2.0f, h), crown_r);
    }

    // LOD1 crown: stacked skirts (cone shell) sharing the needle texture.
    const u32 skirts = 7, segs = 8;
    for (u32 s = 0; s < skirts; ++s) {
        const f32 t0 = f32(s) / f32(skirts);
        const f32 yt = crown_start + (h - crown_start) * (t0 + 1.0f / f32(skirts) * 1.6f);
        const f32 yb = crown_start + (h - crown_start) * t0 - 0.3f;
        const f32 rb = (h - yb) * 0.27f + 0.2f;
        const f32 rt = std::max(0.15f, (h - yt) * 0.10f);
        const u32 base = static_cast<u32>(out.crown_lod1.vertices.size());
        for (u32 i = 0; i <= segs; ++i) {
            const f32 a = f32(i) / f32(segs) * kTwoPi + f32(s) * 0.4f;
            const vec3 radial{std::cos(a), 0, std::sin(a)};
            const vec3 n = normalize(radial + vec3{0, 0.55f, 0});
            const vec3 tg{-std::sin(a), 0, std::cos(a)};
            const f32 u = f32(i) / f32(segs) * 3.0f;
            out.crown_lod1.vertices.push_back(
                make_vertex(radial * rt + axis_point(st, std::min(yt, h), h), n, {u, 0.0f}, tg, 1.0f));
            out.crown_lod1.vertices.push_back(make_vertex(radial * rb + axis_point(st, yb, h), n, {u, 1.0f}, tg, 1.0f));
        }
        for (u32 i = 0; i < segs; ++i) {
            const u32 a = base + i * 2, b = a + 1, c = a + 2, d = a + 3;
            out.crown_lod1.indices.insert(out.crown_lod1.indices.end(), {a, b, c, c, b, d});
        }
    }
    return out;
}

void make_needle_textures(u32 seed, u32 w, u32 h, std::vector<u8>& albedo, std::vector<u8>& normal) {
    Rng rng(seed);
    std::vector<f32> cov(usize(w) * h, 0.0f);
    std::vector<vec3> col(usize(w) * h, vec3{});
    auto plot_line = [&](vec2 a, vec2 b, f32 thick, vec3 c) {
        const vec2 d = b - a;
        const f32 len = std::sqrt(dot(d, d));
        const int steps = static_cast<int>(len * 2.0f) + 1;
        for (int i = 0; i <= steps; ++i) {
            const f32 t = f32(i) / f32(steps);
            const vec2 p = a + d * t;
            const f32 r = thick * 0.5f * (1.0f - 0.5f * t);
            const int x0 = static_cast<int>(p.x - r - 1), x1 = static_cast<int>(p.x + r + 1);
            const int y0 = static_cast<int>(p.y - r - 1), y1 = static_cast<int>(p.y + r + 1);
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x) {
                    if (x < 0 || y < 0 || x >= int(w) || y >= int(h)) continue;
                    const f32 dx = f32(x) + 0.5f - p.x, dy = f32(y) + 0.5f - p.y;
                    const f32 cv = saturate(r + 0.5f - std::sqrt(dx * dx + dy * dy));
                    const usize idx = usize(y) * w + usize(x);
                    if (cv > cov[idx]) {
                        cov[idx] = cv;
                        col[idx] = c;
                    }
                }
        }
    };
    const f32 cy = f32(h) * 0.5f;
    // Side twigs then needles along main twig and side twigs.
    struct Twig {
        vec2 a, b;
    };
    std::vector<Twig> twigs = {{{2.0f, cy}, {f32(w) - 4.0f, cy + rng.range(-6.0f, 6.0f)}}};
    for (int i = 0; i < 14; ++i) {
        const f32 x = f32(w) * rng.range(0.06f, 0.78f);
        const f32 side = (i % 2) ? 1.0f : -1.0f;
        const f32 len = f32(w) * 0.28f * (1.0f - x / f32(w) * 0.7f);
        twigs.push_back({{x, cy}, {x + len * 0.8f, cy + side * len * rng.range(0.45f, 0.7f)}});
    }
    for (const Twig& tw : twigs) plot_line(tw.a, tw.b, 3.0f, {0.16f, 0.11f, 0.07f});
    for (const Twig& tw : twigs) {
        const vec2 d = tw.b - tw.a;
        const f32 len = std::sqrt(dot(d, d));
        const vec2 dn = d * (1.0f / len);
        const vec2 perp{-dn.y, dn.x};
        for (f32 s = 2.0f; s < len; s += 1.1f) {
            const f32 t = s / len;
            const vec2 p = tw.a + dn * s;
            for (int k = 0; k < 2; ++k) {
                const f32 sign = k == 0 ? 1.0f : -1.0f;
                const f32 ang = rng.range(0.6f, 1.1f);
                const f32 nl = f32(h) * rng.range(0.13f, 0.19f) * (1.0f - 0.4f * t);
                const vec2 nd = dn * std::cos(ang) + perp * (std::sin(ang) * sign);
                const f32 g = rng.range(0.85f, 1.15f);
                const vec3 c = vec3{0.045f, 0.085f, 0.035f} * g + vec3{0.01f, 0.015f, 0.0f} * rng.f();
                plot_line(p, p + nd * nl, 2.0f, c);
            }
        }
    }
    albedo.resize(usize(w) * h * 4);
    normal.resize(usize(w) * h * 4);
    auto srgb = [](f32 v) {
        v = saturate(v);
        const f32 e = v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
        return static_cast<u8>(std::lround(e * 255.0f));
    };
    // Dilate color into transparent texels so filtering never pulls in black.
    std::vector<vec3> dil = col;
    for (int pass = 0; pass < 4; ++pass) {
        std::vector<vec3> next = dil;
        for (u32 y = 1; y + 1 < h; ++y)
            for (u32 x = 1; x + 1 < w; ++x) {
                const usize i = usize(y) * w + x;
                if (cov[i] > 0.0f || length(dil[i]) > 0.0f) continue;
                vec3 acc{};
                int n = 0;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const vec3 v = dil[usize(int(y) + dy) * w + usize(int(x) + dx)];
                        if (length(v) > 0.0f) {
                            acc += v;
                            ++n;
                        }
                    }
                if (n) next[i] = acc * (1.0f / f32(n));
            }
        dil = next;
    }
    for (usize i = 0; i < cov.size(); ++i) {
        const vec3 c = length(dil[i]) > 0.0f ? dil[i] : vec3{0.045f, 0.08f, 0.035f};
        albedo[i * 4 + 0] = srgb(c.x);
        albedo[i * 4 + 1] = srgb(c.y);
        albedo[i * 4 + 2] = srgb(c.z);
        albedo[i * 4 + 3] = static_cast<u8>(std::lround(cov[i] * 255.0f));
        normal[i * 4 + 0] = 128;
        normal[i * 4 + 1] = 128;
        normal[i * 4 + 2] = 255;
        normal[i * 4 + 3] = 255;
    }
}

// ---- Vegetation -------------------------------------------------------------------------------------

Vegetation::Vegetation(const rhi::Device& dev, const Scene& scene) : dev_(dev), scene_(scene) {}

Vegetation::~Vegetation() {
    rhi::destroy_buffer(dev_, instances_);
    rhi::destroy_buffer(dev_, protos_);
    rhi::destroy_buffer(dev_, draws_buf_);
}

u32 Vegetation::add_prototype(const VegetationPrototype& p) {
    protos_cpu_.push_back(p);
    return static_cast<u32>(protos_cpu_.size() - 1);
}

void Vegetation::add_instance(u32 prototype, vec3 pos, f32 yaw, f32 scale, f32 random) {
    cpu_.push_back({vec4(pos, scale), {std::sin(yaw), std::cos(yaw), static_cast<f32>(prototype), random}});
}

void Vegetation::collect_canopy(vec2 c, f32 half, std::vector<CanopyInstance>& out) const {
    for (const VegInstanceGPU& i : cpu_) {
        const VegetationPrototype& p = protos_cpu_[static_cast<u32>(i.rot_proto.z)];
        if (p.canopy_radius <= 0.0f) continue;
        const f32 s = i.pos_scale.w, r = p.canopy_radius * s;
        if (std::fabs(i.pos_scale.x - c.x) > half + r || std::fabs(i.pos_scale.z - c.y) > half + r) continue;
        out.push_back({i.pos_scale.x, i.pos_scale.z, i.pos_scale.y + p.canopy_top * s, r, p.canopy_density});
    }
}

void Vegetation::upload() {
    std::stable_sort(cpu_.begin(), cpu_.end(),
                     [](const VegInstanceGPU& a, const VegInstanceGPU& b) { return a.rot_proto.z < b.rot_proto.z; });
    std::vector<ProtoGPU> protos(protos_cpu_.size());
    std::vector<u32> counts(protos_cpu_.size(), 0);
    for (const auto& i : cpu_) ++counts[static_cast<u32>(i.rot_proto.z)];
    u32 base = 0;
    draws_.clear();
    u32 visible_base = 0;
    for (u32 p = 0; p < protos_cpu_.size(); ++p) {
        const VegetationPrototype& src = protos_cpu_[p];
        ProtoGPU& g = protos[p];
        g.bounds = {0.0f, src.center_y, 0.0f, src.radius};
        g.lod_dist = {src.lods[0].max_distance, src.lod_count > 1 ? src.lods[1].max_distance : 0.0f,
                      src.shadow_distance, f32(src.lod_count)};
        g.wind = src.wind;
        g.instance_base = base;
        g.instance_count = counts[p];
        g.casts_shadows = src.cast_shadows ? 1u : 0u;
        for (u32 l = 0; l < 2; ++l)
            for (u32 s = 0; s < 2; ++s) g.draw[l][s] = ~0u;
        for (u32 l = 0; l < src.lod_count; ++l) {
            for (u32 s = 0; s < 2; ++s) {
                if (src.lods[l].mesh[s] == ~0u) continue;
                const MeshRange& mr = scene_.meshes()[src.lods[l].mesh[s]];
                DrawGPU d{
                    mr.first_index, mr.index_count, mr.vertex_offset, src.lods[l].material[s], visible_base, p, l, s};
                g.draw[l][s] = static_cast<u32>(draws_.size());
                draws_.push_back(d);
            }
            visible_base += counts[p]; // each LOD gets its own visible region (submeshes share it)
        }
        base += counts[p];
    }
    TEFRA_VERIFY(draws_.size() <= kMaxDraws);
    // Submeshes of one LOD must share the visible region: fix bases so submesh 1 uses submesh 0's region.
    for (auto& d : draws_)
        if (d.submesh == 1) d.visible_base = draws_[protos[d.prototype].draw[d.lod][0]].visible_base;
    visible_capacity_ = std::max(visible_base, 1u);
    rhi::destroy_buffer(dev_, instances_);
    rhi::destroy_buffer(dev_, protos_);
    rhi::destroy_buffer(dev_, draws_buf_);
    if (cpu_.empty()) cpu_.push_back({});
    instances_ = rhi::upload_buffer(dev_, cpu_.data(), cpu_.size() * sizeof(VegInstanceGPU),
                                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "veg_instances");
    if (protos.empty()) protos.push_back({});
    protos_ = rhi::upload_buffer(dev_, protos.data(), protos.size() * sizeof(ProtoGPU),
                                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "veg_protos");
    std::vector<DrawGPU> dr = draws_;
    if (dr.empty()) dr.push_back({});
    draws_buf_ = rhi::upload_buffer(dev_, dr.data(), dr.size() * sizeof(DrawGPU), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                    "veg_draws");
    TEFRA_LOG_INFO("Vegetation: %u prototypes, %zu instances, %zu draws", static_cast<u32>(protos_cpu_.size()),
                   cpu_.size(), draws_.size());
}

} // namespace tefra::render
