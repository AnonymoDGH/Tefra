#include "tefra/renderer/vegetation.h"
#include "tefra/renderer/scene.h"

#include "tefra/core/log.h"
#include "tefra/rhi/device.h"
#include "tefra/rhi/upload.h"

#include <algorithm>
#include <atomic>
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

// ---- Needle atlas (baked twigs) ---------------------------------------------------------------------
// The atlas is 2x2 tiles of 512x256 texels (2:1, like the cards it decorates). Tiles 0-2 hold living twigs of
// three variants, tile 3 a dead twig (brown, sparse). A card maps its UV [0,1]^2 into its tile, inset so that
// mip levels never bleed into the neighbouring tile.
constexpr u32 kDeadTwigTile = 3;
constexpr u32 kTileW = 512, kTileH = 256;
constexpr f32 kTileInset = 0.03f;
constexpr u32 kSupersample = 2;

vec2 tile_uv(u32 tile, f32 u, f32 v) {
    const f32 s = 0.5f - 2.0f * kTileInset;
    return {f32(tile % 2) * 0.5f + kTileInset + u * s, f32(tile / 2) * 0.5f + kTileInset + v * s};
}

f32 smooth01(f32 e0, f32 e1, f32 x) {
    const f32 t = saturate((x - e0) / (e1 - e0));
    return t * t * (3.0f - 2.0f * t);
}

// A finite cylinder (needle or twig segment) in tile space: x along the card, y across it (+y = image row
// down = texture v), z towards the viewer. Units are texels of the final tile.
struct Needle {
    vec3 a, u; // base and unit direction
    f32 len, r;
    vec3 c; // linear albedo
};

// Orthographic front hit: ray from z = +kRayZ along -z. Returns the surface depth and normal.
bool hit_needle(const Needle& nd, f32 px, f32 py, f32& z_out, vec3& n_out) {
    constexpr f32 kRayZ = 1000.0f;
    const vec3 o{px, py, kRayZ};
    const vec3 d{0.0f, 0.0f, -1.0f};
    const vec3 oc = o - nd.a;
    const vec3 dp = d - nd.u * dot(d, nd.u);
    const vec3 ocp = oc - nd.u * dot(oc, nd.u);
    const f32 qa = dot(dp, dp);
    if (qa < 1e-6f) return false;
    const f32 qb = 2.0f * dot(dp, ocp);
    const f32 qc = dot(ocp, ocp) - nd.r * nd.r;
    const f32 disc = qb * qb - 4.0f * qa * qc;
    if (disc < 0.0f) return false;
    const f32 t = (-qb - std::sqrt(disc)) / (2.0f * qa); // nearest root: the surface closest to the viewer
    const vec3 p = o + d * t;
    const f32 s = dot(p - nd.a, nd.u);
    if (s < 0.0f || s > nd.len) return false;
    z_out = p.z;
    n_out = normalize((p - nd.a) - nd.u * s);
    return true;
}

// Builds one twig: a curved stem with alternating branchlets, each carrying combed needles that age from dark
// green at the base to pale yellow-green at the tip. Dead twigs keep brown stems and a few needle stubs.
void build_twig(u32 variant, Rng& rng, std::vector<Needle>& out) {
    const bool dead = variant == kDeadTwigTile;
    const vec3 tint = variant == 1 ? vec3{1.08f, 1.0f, 0.85f} : variant == 2 ? vec3{0.9f, 1.04f, 1.12f} : vec3{1, 1, 1};
    const vec3 stem_c = dead ? vec3{0.22f, 0.16f, 0.10f} : vec3{0.07f, 0.055f, 0.04f};
    const f32 cy = f32(kTileH) * 0.5f;
    const f32 ph = rng.range(0.0f, kTwoPi), bow = rng.range(8.0f, 13.0f);
    auto stem_pt = [&](f32 x) {
        return vec3{x, cy + bow * 0.6f * std::sin(x * 0.011f + ph), 14.0f * std::sin(x / f32(kTileW) * kPi)};
    };
    auto push_seg = [&](vec3 p0, vec3 p1, f32 r, vec3 c) {
        const vec3 d = p1 - p0;
        const f32 l = std::sqrt(dot(d, d));
        if (l > 1e-4f) out.push_back({p0, d * (1.0f / l), l, r, c});
    };
    for (f32 x = 4.0f; x < f32(kTileW) - 10.0f; x += 6.0f)
        push_seg(stem_pt(x), stem_pt(x + 6.0f), dead ? 4.5f : 1.5f, stem_c);

    const u32 nb = dead ? 10 : 23;
    for (u32 k = 0; k < nb; ++k) {
        const f32 x = 18.0f + f32(k) * (480.0f / f32(nb)) + rng.range(-4.0f, 4.0f);
        const f32 side = (k % 2 == 0) ? 1.0f : -1.0f;
        const vec3 base = stem_pt(x);
        const f32 theta = dead ? rng.range(0.8f, 1.2f) : rng.range(0.55f, 0.85f);
        const f32 len = dead ? rng.range(55.0f, 85.0f) : rng.range(80.0f, 112.0f) * (1.0f - 0.25f * x / f32(kTileW));
        const vec3 d = normalize(vec3{std::cos(theta), side * std::sin(theta), rng.range(-0.45f, 0.45f)});
        push_seg(base, base + d * len, dead ? 2.4f : 0.8f, stem_c);

        const vec3 ref = std::fabs(d.z) < 0.9f ? vec3{0, 0, 1} : vec3{1, 0, 0};
        const vec3 p1 = normalize(cross(d, ref)), p2 = cross(d, p1);
        const f32 spacing = dead ? 4.5f : rng.range(1.6f, 2.2f);
        for (f32 sp = 2.5f; sp < len - 1.0f; sp += spacing) {
            const f32 f = sp / len; // 0 at the base, 1 at the tip
            for (int s = 0; s < 2; ++s) {
                if (dead && rng.f() > 0.35f) continue;
                const f32 phi = dead ? rng.range(0.6f, 1.0f) : rng.range(0.35f, 0.7f);
                // Living twigs comb their needles into two flanks (Douglas fir); variant 2 is radial (spruce-like).
                const f32 beta = variant == 2 ? rng.range(0.0f, kTwoPi)
                                              : (s == 0 ? rng.range(-0.3f, 0.3f) : kPi + rng.range(-0.3f, 0.3f));
                const vec3 lat = p1 * std::cos(beta) + p2 * std::sin(beta);
                const vec3 u =
                    normalize(d * std::cos(phi) + lat * std::sin(phi) + vec3{0, 0, rng.range(-0.25f, 0.25f)});
                const f32 nlen = (dead ? 0.6f : 1.0f) * rng.range(18.0f, 26.0f);
                vec3 col;
                if (dead) {
                    col = vec3{0.17f, 0.12f, 0.07f} * rng.range(0.7f, 1.1f);
                } else {
                    const f32 age = smooth01(0.45f, 1.0f, f) * rng.range(0.6f, 1.0f);
                    const vec3 old_c{0.020f, 0.052f, 0.040f}, young_c{0.085f, 0.150f, 0.060f};
                    col = (old_c + (young_c - old_c) * age) * rng.range(0.85f, 1.15f) * tint;
                }
                out.push_back({base + d * sp, u, nlen, dead ? 1.2f : 1.0f, col});
            }
        }
    }
}

// ---- Cards and crown --------------------------------------------------------------------------------

struct CardSpec {
    vec3 root, dir, side; // cross(side, dir) is the card normal (front side, the one the viewer sees)
    f32 length, width, droop;
    vec3 crown_center;
    f32 crown_radius;
    u32 tile, segs;
};

// Card with its normal leaning towards `outward` (orthogonalised to dir) and rotated by psi about dir.
vec3 card_side(vec3 dir, vec3 outward, f32 psi) {
    vec3 nt = outward - dir * dot(outward, dir);
    nt = normalize(nt);
    nt = nt * std::cos(psi) + cross(dir, nt) * std::sin(psi);
    return normalize(cross(dir, nt));
}

// Two-sided foliage card. Vertex AO travels in tangent_sign (the shader reads abs()); the tangent is dir.
void add_card(MeshData& m, const CardSpec& c) {
    const u32 base = static_cast<u32>(m.vertices.size());
    const vec3 card_n = normalize(cross(c.side, c.dir));
    for (u32 i = 0; i <= c.segs; ++i) {
        const f32 t = f32(i) / f32(c.segs);
        const vec3 ctr = c.root + c.dir * (c.length * t) - vec3{0.0f, c.droop * c.length * t * t, 0.0f};
        const f32 w = c.width * (1.0f - 0.35f * t);
        for (int k = 0; k < 2; ++k) {
            const vec3 p = ctr + c.side * (w * (k == 0 ? -0.5f : 0.5f));
            // Normals bent towards the crown's outward direction: soft, volumetric foliage shading.
            vec3 out = p - c.crown_center;
            out.y *= 0.6f;
            const vec3 n = normalize(card_n * 0.35f + normalize(out) * (0.65f + 0.35f * c.length / c.crown_radius));
            // Inner parts of the crown (near the trunk, low) receive less sky light.
            const f32 radial =
                length(vec3{p.x - c.crown_center.x, 0.0f, p.z - c.crown_center.z}) / max(c.crown_radius, 0.1f);
            const f32 ao = clamp(0.25f + 0.75f * radial, 0.25f, 1.0f);
            m.vertices.push_back(make_vertex(p, n, tile_uv(c.tile, t, k == 0 ? 0.0f : 1.0f), c.dir, ao));
        }
    }
    for (u32 i = 0; i < c.segs; ++i) {
        const u32 a = base + i * 2, b = a + 1, cc = a + 2, d = a + 3;
        m.indices.insert(m.indices.end(), {a, cc, b, b, cc, d});
    }
}

// A drooping limb: four stations along its axis, each with two single-segment sub-branch cards on both flanks.
void add_limb(MeshData& m, Rng& rng, vec3 root, vec3 dir, f32 len, vec3 crown_c, f32 crown_r) {
    constexpr f32 kDroop = 0.32f;
    auto axis = [&](f32 t) { return root + dir * (len * t) - vec3{0.0f, kDroop * len * t * t, 0.0f}; };
    const f32 stations[3] = {0.25f, 0.50f, 0.76f};
    for (const f32 ts : stations) {
        const vec3 p = axis(ts);
        const vec3 tang = normalize(axis(ts + 0.03f) - axis(ts - 0.03f));
        const vec3 flank = normalize(cross(tang, vec3{0.0f, 1.0f, 0.0f}));
        for (int k = 0; k < 2; ++k) {
            const f32 sg = k == 0 ? 1.0f : -1.0f;
            const f32 theta = rng.range(0.6f, 0.9f);
            const vec3 sdir =
                normalize(tang * std::cos(theta) + flank * (sg * std::sin(theta)) + vec3{0.0f, -0.15f, 0.0f});
            const f32 sl = len * 0.52f * (1.0f - 0.30f * ts) * rng.range(0.85f, 1.15f);
            // Normals lean outward so cards stay visible from the side; the random roll varies the planes.
            const vec3 side = card_side(sdir, flank * (sg * 0.8f) + vec3{0.0f, 0.4f, 0.0f}, rng.range(-0.9f, 0.9f));
            add_card(m, {p, sdir, side, sl, sl * 0.8f, 0.25f, crown_c, crown_r, rng.next() % 3u, 1u});
        }
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

    // Dead lower branches: sparse brown cards hanging from the bare trunk.
    const u32 dead_n = 9 + rng.next() % 4;
    for (u32 d = 0; d < dead_n; ++d) {
        const f32 y = rng.range(1.0f, crown_start + 1.0f);
        const f32 a = rng.range(0.0f, kTwoPi);
        const vec3 hd{std::cos(a), 0.0f, std::sin(a)};
        const vec3 dir = normalize(hd + vec3{0.0f, rng.range(-0.5f, -0.15f), 0.0f});
        const vec3 root = hd * trunk_radius_at(y, h, r0) + axis_point(st, y, h);
        const vec3 flank = normalize(cross(dir, vec3{0.0f, 1.0f, 0.0f}));
        const f32 len = rng.range(1.4f, 2.6f);
        for (int k = 0; k < 2; ++k) {
            const f32 sg = k == 0 ? 1.0f : -1.0f;
            const vec3 side = card_side(dir, flank * (sg * 0.7f) + vec3{0.0f, 0.4f, 0.0f}, rng.range(-0.6f, 0.6f));
            add_card(out.crown_lod0,
                     {root, dir, side, len, len * 0.5f, 0.3f, axis_point(st, y, h), crown_r, kDeadTwigTile, 1u});
        }
    }

    // LOD0 crown: whorls of drooping limbs. Spacing, count, length and missing limbs leave irregular gaps.
    f32 y = crown_start + 0.8f;
    u32 whorl = 0;
    while (y < h - 1.2f) {
        const f32 t = (y - crown_start) / (h - crown_start);
        const u32 n = 7 + (rng.next() % 2);
        const f32 base_len = std::max(0.6f, (h - y) * 0.27f * (1.0f - 0.2f * t));
        const f32 R = trunk_radius_at(y, h, r0);
        for (u32 i = 0; i < n; ++i) {
            if (rng.f() < 0.1f) continue;
            const f32 a = f32(whorl) * 2.399963f + f32(i) / f32(n) * kTwoPi + rng.range(-0.35f, 0.35f);
            const vec3 hd{std::cos(a), 0.0f, std::sin(a)};
            const vec3 dir = normalize(hd + vec3{0.0f, rng.range(-0.25f, 0.02f), 0.0f});
            const vec3 axis = axis_point(st, y, h);
            add_limb(out.crown_lod0, rng, hd * R + axis, dir, base_len * rng.range(0.6f, 1.15f), axis, crown_r);
        }
        y += rng.range(0.55f, 0.75f);
        ++whorl;
    }
    // Leader: a few short upright cards at the top.
    for (int k = 0; k < 3; ++k) {
        const f32 a = f32(k) / 3.0f * kTwoPi + rng.range(0.0f, 0.5f);
        const vec3 dir = normalize(vec3{std::cos(a) * 0.3f, 1.0f, std::sin(a) * 0.3f});
        const vec3 side = card_side(dir, vec3{std::cos(a), 0.0f, std::sin(a)}, 0.0f);
        add_card(out.crown_lod0, {axis_point(st, h - 1.6f, h), dir, side, 1.4f, 0.7f, 0.0f, axis_point(st, h - 2.0f, h),
                                  crown_r, rng.next() % 3u, 2u});
    }

    // LOD1 crown: stacked skirts (cone shell) sampling a living twig tile.
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
            const f32 u = std::fmod(f32(i) / f32(segs) * 3.0f, 1.0f);
            out.crown_lod1.vertices.push_back(
                make_vertex(radial * rt + axis_point(st, std::min(yt, h), h), n, tile_uv(1, u, 0.0f), tg, 1.0f));
            out.crown_lod1.vertices.push_back(
                make_vertex(radial * rb + axis_point(st, yb, h), n, tile_uv(1, u, 1.0f), tg, 1.0f));
        }
        for (u32 i = 0; i < segs; ++i) {
            const u32 a = base + i * 2, b = a + 1, c = a + 2, d = a + 3;
            out.crown_lod1.indices.insert(out.crown_lod1.indices.end(), {a, b, c, c, b, d});
        }
    }
    return out;
}

void make_needle_textures(u32 seed, u32 w, u32 h, std::vector<u8>& albedo, std::vector<u8>& normal) {
    TEFRA_VERIFY(w == 2 * kTileW && h == 2 * kTileH);
    const u32 ss = kSupersample;
    const u32 tw = kTileW * ss, th = kTileH * ss; // supersampled tile
    std::vector<f32> cov(usize(w) * h, 0.0f);
    std::vector<vec3> col(usize(w) * h, vec3{});
    std::vector<vec3> nrm(usize(w) * h, vec3{0, 0, 1});
    std::vector<f32> ao(usize(w) * h, 1.0f);

    std::vector<f32> z(usize(tw) * th), sao(usize(tw) * th);
    std::vector<vec3> sn(usize(tw) * th), sc(usize(tw) * th);
    std::vector<u8> hit(usize(tw) * th);
    for (u32 tile = 0; tile < 4; ++tile) {
        Rng rng(seed * 131u + tile * 977u + 1u);
        std::vector<Needle> needles;
        build_twig(tile, rng, needles);
        std::fill(z.begin(), z.end(), -1e30f);
        std::fill(hit.begin(), hit.end(), u8(0));
        for (const Needle& nd : needles) {
            const vec3 e = nd.a + nd.u * nd.len;
            const f32 fx0 = std::min(nd.a.x, e.x) - nd.r, fx1 = std::max(nd.a.x, e.x) + nd.r;
            const f32 fy0 = std::min(nd.a.y, e.y) - nd.r, fy1 = std::max(nd.a.y, e.y) + nd.r;
            const u32 x0 = u32(std::max(0.0f, std::floor(fx0 * ss))),
                      x1 = u32(std::min(f32(tw - 1), std::ceil(fx1 * ss)));
            const u32 y0 = u32(std::max(0.0f, std::floor(fy0 * ss))),
                      y1 = u32(std::min(f32(th - 1), std::ceil(fy1 * ss)));
            for (u32 y = y0; y <= y1; ++y)
                for (u32 x = x0; x <= x1; ++x) {
                    const f32 px = (f32(x) + 0.5f) / f32(ss), py = (f32(y) + 0.5f) / f32(ss);
                    f32 zh;
                    vec3 nh;
                    if (!hit_needle(nd, px, py, zh, nh)) continue;
                    const usize i = usize(y) * tw + x;
                    if (zh > z[i]) {
                        z[i] = zh;
                        sn[i] = nh;
                        sc[i] = nd.c;
                        hit[i] = 1;
                    }
                }
        }
        // Depth AO: fraction of a disc of neighbours that sit clearly in front of each hit surface.
        constexpr int kAoSamples = 16;
        const f32 ao_radius = 4.0f * f32(ss);
        for (u32 y = 0; y < th; ++y)
            for (u32 x = 0; x < tw; ++x) {
                const usize i = usize(y) * tw + x;
                sao[i] = 1.0f;
                if (!hit[i]) continue;
                int front = 0;
                for (int k = 0; k < kAoSamples; ++k) {
                    const f32 ang = f32(k) * 2.3999632f, rr = ao_radius * std::sqrt((f32(k) + 0.5f) / f32(kAoSamples));
                    const int nx = int(x) + int(std::cos(ang) * rr), ny = int(y) + int(std::sin(ang) * rr);
                    if (nx < 0 || ny < 0 || nx >= int(tw) || ny >= int(th)) continue;
                    const usize j = usize(ny) * tw + usize(nx);
                    if (hit[j] && z[j] > z[i] + 0.6f) ++front;
                }
                sao[i] = 1.0f - 0.7f * f32(front) / f32(kAoSamples);
            }
        // Box-filter the supersampled tile into its atlas quadrant.
        const u32 ox = (tile % 2) * kTileW, oy = (tile / 2) * kTileH;
        for (u32 y = 0; y < kTileH; ++y)
            for (u32 x = 0; x < kTileW; ++x) {
                vec3 c_acc{}, n_acc{};
                f32 ao_acc = 0.0f;
                int hits = 0;
                for (u32 sy = 0; sy < ss; ++sy)
                    for (u32 sx = 0; sx < ss; ++sx) {
                        const usize i = usize(y * ss + sy) * tw + (x * ss + sx);
                        if (!hit[i]) continue;
                        ++hits;
                        c_acc += sc[i];
                        n_acc += sn[i];
                        ao_acc += sao[i];
                    }
                const usize ai = usize(oy + y) * w + (ox + x);
                cov[ai] = f32(hits) / f32(ss * ss);
                if (hits > 0) {
                    col[ai] = c_acc * (1.0f / f32(hits));
                    nrm[ai] = normalize(n_acc);
                    ao[ai] = ao_acc / f32(hits);
                }
            }
    }

    // Dilate colour into transparent texels so filtering never pulls in black.
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
    albedo.resize(usize(w) * h * 4);
    normal.resize(usize(w) * h * 4);
    auto srgb = [](f32 v) {
        v = saturate(v);
        const f32 e = v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
        return static_cast<u8>(std::lround(e * 255.0f));
    };
    auto unorm = [](f32 v) { return static_cast<u8>(std::lround(saturate(v) * 255.0f)); };
    for (usize i = 0; i < cov.size(); ++i) {
        const vec3 c = length(dil[i]) > 0.0f ? dil[i] : vec3{0.030f, 0.065f, 0.050f};
        albedo[i * 4 + 0] = srgb(c.x);
        albedo[i * 4 + 1] = srgb(c.y);
        albedo[i * 4 + 2] = srgb(c.z);
        albedo[i * 4 + 3] = unorm(cov[i]);
        // Tangent-space normal. The engine's bitangent is cross(N, T) = -side, so G carries -v.
        vec3 n = nrm[i];
        n.z = std::max(n.z, 0.05f);
        n = normalize(n);
        normal[i * 4 + 0] = unorm(n.x * 0.5f + 0.5f);
        normal[i * 4 + 1] = unorm(-n.y * 0.5f + 0.5f);
        normal[i * 4 + 2] = unorm(n.z * 0.5f + 0.5f);
        normal[i * 4 + 3] = unorm(ao[i]); // depth AO (material flag 4)
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
    protos_gpu_ = protos;
    if (protos.empty()) protos.push_back({});
    protos_ = rhi::upload_buffer(dev_, protos.data(), protos.size() * sizeof(ProtoGPU),
                                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "veg_protos");
    std::vector<DrawGPU> dr = draws_;
    if (dr.empty()) dr.push_back({});
    draws_buf_ = rhi::upload_buffer(dev_, dr.data(), dr.size() * sizeof(DrawGPU), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                    "veg_draws");
    static std::atomic<u64> next_uid{1};
    uid_ = next_uid.fetch_add(1);
    TEFRA_LOG_INFO("Vegetation: %u prototypes, %zu instances, %zu draws", static_cast<u32>(protos_cpu_.size()),
                   cpu_.size(), draws_.size());
}

} // namespace tefra::render
