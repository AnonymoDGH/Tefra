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

void add_trunk(MeshData& m, f32 h, f32 r0, u32 segments, f32 ring_step, f32 top) {
    const u32 base = static_cast<u32>(m.vertices.size());
    const u32 rings = static_cast<u32>(std::ceil(top / ring_step)) + 1;
    const f32 circumference = kTwoPi * r0;
    const f32 u_scale = std::max(1.0f, std::round(circumference / 0.9f)); // bark texel density ~0.9 m per tile
    for (u32 r = 0; r < rings; ++r) {
        const f32 y = std::min(f32(r) * ring_step, top);
        const f32 rad = trunk_radius_at(y, h, r0);
        for (u32 s = 0; s <= segments; ++s) {
            const f32 a = f32(s) / f32(segments) * kTwoPi;
            const vec3 n{std::cos(a), 0.15f, std::sin(a)};
            const vec3 p{std::cos(a) * rad, y, std::sin(a) * rad};
            const vec3 t{-std::sin(a), 0.0f, std::cos(a)};
            m.vertices.push_back(make_vertex(p, normalize(n), {f32(s) / f32(segments) * u_scale, y / 1.6f}, t, 1.0f));
        }
    }
    const u32 stride = segments + 1;
    for (u32 r = 0; r + 1 < rings; ++r)
        for (u32 s = 0; s < segments; ++s) {
            const u32 a = base + r * stride + s, b = a + stride, c = a + 1, d = b + 1;
            m.indices.insert(m.indices.end(), {a, b, c, c, b, d});
        }
}

// Branch card: a curved quad strip from `root` along `dir`, drooping, oriented by `side`.
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
            m.vertices.push_back(make_vertex(p, n, {t, k == 0 ? 0.0f : 1.0f}, dir, 1.0f));
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

    add_trunk(out.trunk_lod0, h, r0, 12, 0.9f, h * 0.97f);
    add_trunk(out.trunk_lod1, h, r0, 5, 4.0f, crown_start + 2.0f);

    // LOD0 crown: whorls of drooping branch cards.
    f32 y = crown_start;
    u32 whorl = 0;
    while (y < h - 0.4f) {
        const f32 t = (y - crown_start) / (h - crown_start);
        const u32 n = 5 + (rng.next() % 2);
        const f32 len = std::max(0.45f, (h - y) * 0.27f * rng.range(0.8f, 1.15f) * (1.0f - 0.2f * t));
        for (u32 i = 0; i < n; ++i) {
            const f32 a = f32(whorl) * 2.399963f + f32(i) / f32(n) * kTwoPi + rng.range(-0.3f, 0.3f);
            const vec3 dir = normalize(vec3{std::cos(a), rng.range(-0.15f, 0.05f), std::sin(a)});
            vec3 side = normalize(cross(vec3{0, 1, 0}, dir));
            const f32 tilt = rng.range(-0.5f, 0.5f);
            side = normalize(side * std::cos(tilt) + vec3{0, 1, 0} * std::sin(tilt));
            const vec3 root{dir.x * trunk_radius_at(y, h, r0), y, dir.z * trunk_radius_at(y, h, r0)};
            add_card(out.crown_lod0, root, dir, side, len, len * 0.55f + 0.3f, 0.35f, {0, y, 0}, crown_r);
        }
        y += rng.range(0.42f, 0.62f);
        ++whorl;
    }
    // Leader at the top.
    for (int k = 0; k < 3; ++k) {
        const f32 a = f32(k) / 3.0f * kTwoPi;
        add_card(out.crown_lod0, {0, h - 1.2f, 0}, normalize(vec3{std::cos(a) * 0.25f, 1.0f, std::sin(a) * 0.25f}),
                 normalize(vec3{-std::sin(a), 0, std::cos(a)}), 1.3f, 0.5f, 0.0f, {0, h - 2.0f, 0}, crown_r);
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
                make_vertex(radial * rt + vec3{0, std::min(yt, h), 0}, n, {u, 0.0f}, tg, 1.0f));
            out.crown_lod1.vertices.push_back(make_vertex(radial * rb + vec3{0, yb, 0}, n, {u, 1.0f}, tg, 1.0f));
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
    for (int i = 0; i < 9; ++i) {
        const f32 x = f32(w) * rng.range(0.08f, 0.75f);
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
        for (f32 s = 2.0f; s < len; s += 1.6f) {
            const f32 t = s / len;
            const vec2 p = tw.a + dn * s;
            for (int k = 0; k < 2; ++k) {
                const f32 sign = k == 0 ? 1.0f : -1.0f;
                const f32 ang = rng.range(0.6f, 1.1f);
                const f32 nl = f32(h) * rng.range(0.10f, 0.15f) * (1.0f - 0.45f * t);
                const vec2 nd = dn * std::cos(ang) + perp * (std::sin(ang) * sign);
                const f32 g = rng.range(0.85f, 1.15f);
                const vec3 c = vec3{0.11f, 0.20f, 0.09f} * g + vec3{0.02f, 0.03f, 0.0f} * rng.f();
                plot_line(p, p + nd * nl, 1.6f, c);
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
        const vec3 c = length(dil[i]) > 0.0f ? dil[i] : vec3{0.11f, 0.19f, 0.09f};
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
