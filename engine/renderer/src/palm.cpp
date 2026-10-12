#include "tefra/renderer/palm.h"

#include "tefra/core/assert.h"
#include "tefra/core/jobs.h"

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

vec3 cmul(vec3 a, vec3 b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
f32 sq(f32 x) { return x * x; }

f32 smooth01(f32 e0, f32 e1, f32 x) {
    const f32 t = saturate((x - e0) / (e1 - e0));
    return t * t * (3.0f - 2.0f * t);
}

// ---- Trunk ------------------------------------------------------------------------------------------

// Trunk centre line: a quadratic bend towards the lean direction (dx, dz). The top tangent has `tilt` = 8-34 deg
// (beach palms lean out towards the light over the sand).
struct Axis {
    f32 h = 1.0f;
    f32 dx = 1.0f, dz = 0.0f; // unit horizontal lean direction
    f32 amount = 0.0f;        // horizontal offset at the top (m)
    vec3 at(f32 y) const {
        const f32 t = y / h, off = amount * t * t;
        return {dx * off, y, dz * off};
    }
};

// Radius along the trunk: slight taper, a base bulb (about 1.9 x the radius at ground level) and roots below.
f32 trunk_radius(f32 y, f32 h, f32 r0) {
    const f32 t = clamp(y / h, 0.0f, 1.0f);
    const f32 flare = 0.9f * std::exp(-y * y / (y < 0.0f ? 0.5f : 0.2f));
    return r0 * (1.0f - 0.14f * t) * (1.0f + flare);
}

struct TrunkShape {
    f32 amp[2]; // irregular section: harmonics 2 and 3
    f32 phase[2];
};

f32 section_radius(const TrunkShape& sh, f32 a, f32 y, f32 h, f32 r0) {
    f32 k = 1.0f;
    for (int i = 0; i < 2; ++i) k += sh.amp[i] * std::sin(f32(i + 2) * a + sh.phase[i]);
    return trunk_radius(y, h, r0) * k;
}

// Trunk as a ring grid along the axis. Normals come from the grid. UV: u wraps the circumference, v runs along the
// trunk at 0.9 m per tile (same texel density as the circumference), so the bark keeps its scale.
// Tangent sign -1 makes the bitangent point up (+V), like the conifer trunk.
void add_trunk(MeshData& m, const Axis& ax, const TrunkShape& sh, f32 r0, const std::vector<f32>& ys, u32 segs) {
    const u32 rings = static_cast<u32>(ys.size()), stride = segs + 1;
    const u32 base = static_cast<u32>(m.vertices.size());
    std::vector<vec3> pos(usize(rings) * stride);
    for (u32 r = 0; r < rings; ++r) {
        const f32 y = ys[r];
        const vec3 c = ax.at(y);
        for (u32 s = 0; s <= segs; ++s) {
            const f32 a = f32(s) / f32(segs) * kTwoPi;
            const f32 rad = section_radius(sh, a, y, ax.h, r0);
            pos[usize(r) * stride + s] = {c.x + std::cos(a) * rad, y, c.z + std::sin(a) * rad};
        }
    }
    const f32 u_scale = std::max(1.0f, std::round(kTwoPi * r0 / 0.9f));
    for (u32 r = 0; r < rings; ++r) {
        const u32 rp = std::min(r + 1, rings - 1), rm = r > 0 ? r - 1 : 0;
        for (u32 s = 0; s <= segs; ++s) {
            const u32 s0 = s % segs, sn = (s0 + 1) % segs, sp = (s0 + segs - 1) % segs;
            const vec3 p = pos[usize(r) * stride + s];
            const vec3 t_angle = pos[usize(r) * stride + sn] - pos[usize(r) * stride + sp];
            const vec3 t_axis = pos[usize(rp) * stride + s0] - pos[usize(rm) * stride + s0];
            vec3 n = normalize(cross(t_axis, t_angle));
            if (dot(n, p - ax.at(ys[r])) < 0.0f) n = -n;
            m.vertices.push_back(
                make_vertex(p, n, {f32(s) / f32(segs) * u_scale, ys[r] / 0.9f}, normalize(t_angle), -1.0f));
        }
    }
    for (u32 r = 0; r + 1 < rings; ++r)
        for (u32 s = 0; s < segs; ++s) {
            const u32 a = base + r * stride + s, b = a + stride, c = a + 1, d = b + 1;
            m.indices.insert(m.indices.end(), {a, b, c, c, b, d});
        }
}

// Appends a mesh translated by `off` (indices stay relative to the destination mesh).
void append_mesh(MeshData& dst, const MeshData& src, vec3 off) {
    const u32 base = static_cast<u32>(dst.vertices.size());
    for (Vertex v : src.vertices) {
        v.pos[0] += off.x;
        v.pos[1] += off.y;
        v.pos[2] += off.z;
        dst.vertices.push_back(v);
    }
    for (u32 i : src.indices) dst.indices.push_back(base + i);
}

// ---- Fronds -----------------------------------------------------------------------------------------

constexpr u32 kStations = 11; // LOD0 rachis stations (10 segments); LOD1 takes every second one.

// A pinnate frond: a rachis curve (stations along it), per-station side vector (the fold of the leaflet plane) and
// half width. Each side of the rachis is one strip of cards.
struct Frond {
    vec3 p[kStations], t[kStations], side[kStations];
    f32 s[kStations], w[kStations];
    u32 tile = 0;
};

// Gravity bends the rachis: the elevation falls with the square of the arc fraction. Young fronds rise, old ones
// hang; dead fronds (brown tile) hang the most. The leaflet fold leans young fronds up into a V.
Frond make_frond(Rng& rng, vec3 apex, f32 az, f32 age, bool dead, f32 len) {
    Frond f{};
    f.tile = dead ? 3u : rng.next() % 3u;
    // Young fronds leave the crown at ~55 deg and arch; old ones leave near horizontal and hang.
    const f32 e0 = dead ? radians(rng.range(-30.0f, -12.0f)) : radians(lerp(48.0f, 0.0f, age) + rng.range(-6.0f, 6.0f));
    const f32 bend = dead ? rng.range(1.1f, 1.4f) : lerp(0.7f, 1.6f, age) * rng.range(0.9f, 1.1f);
    const f32 phi = dead ? 0.35f : lerp(-0.45f, 0.55f, age);
    const f32 wmax = dead ? 0.7f : rng.range(1.05f, 1.25f);
    const vec3 lat{-std::sin(az), 0.0f, std::cos(az)}; // horizontal, perpendicular to the frond's heading
    const f32 ds = len / f32(kStations - 1);
    auto elev = [&](f32 s) { return e0 - bend * s * s; };
    auto dir = [&](f32 e) { return vec3{std::cos(e) * std::cos(az), std::sin(e), std::cos(e) * std::sin(az)}; };
    vec3 p = apex;
    for (u32 j = 0; j < kStations; ++j) {
        const f32 s = f32(j) / f32(kStations - 1);
        f.s[j] = s;
        f.p[j] = p;
        f.t[j] = dir(elev(s));
        const vec3 raw = lat * std::cos(phi) - vec3{0.0f, 1.0f, 0.0f} * std::sin(phi);
        f.side[j] = normalize(raw - f.t[j] * dot(raw, f.t[j]));
        f.w[j] = wmax * std::pow(1.0f - s, 0.6f) * (0.45f + 0.55f * min(1.0f, s / 0.25f));
        if (j + 1 < kStations) p = p + dir(elev(s + 0.5f / f32(kStations - 1))) * ds;
    }
    return f;
}

// UV of the frond atlas: u along the frond, v across it (0.5 = midrib). Tile layout as in make_frond_textures.
vec2 frond_uv(u32 tile, f32 u, f32 v) {
    const f32 s = 0.5f - 2.0f * 0.03f;
    return {f32(tile % 2) * 0.5f + 0.03f + u * s, f32(tile / 2) * 0.5f + 0.03f + v * s};
}

// Two cards per frond (one per side), each a strip from the midrib (v = 0.5) to the outer edge. The card's U runs
// along the rachis and its V along `D`, the direction it is folded towards, so the bitangent (cross(N, T)) matches
// +V and the atlas normal's green channel is correct on both sides. `stride` picks the stations (1 = LOD0).
void add_frond_halves(MeshData& m, const Frond& f, u32 stride, vec3 cc, f32 cr) {
    std::vector<u32> js;
    for (u32 j = 0; j < kStations; j += stride) js.push_back(j);
    const u32 mid = js[js.size() / 2];
    for (f32 sgn : {1.0f, -1.0f}) {
        // Orientation: the card's normal points up (N.y >= 0) at the middle station.
        const f32 o = cross(f.t[mid], f.side[mid] * sgn).y >= 0.0f ? 1.0f : -1.0f;
        const u32 base = static_cast<u32>(m.vertices.size());
        for (u32 j : js) {
            const vec3 t = f.t[j];
            const vec3 D = f.side[j] * (o * sgn); // direction of increasing V
            const vec3 n_card = normalize(cross(t, D));
            const vec3 pin = f.p[j], pout = f.p[j] + D * (o * f.w[j]);
            for (u32 k = 0; k < 2; ++k) {
                const vec3 p = k == 0 ? pin : pout;
                const f32 v = k == 0 ? 0.5f : 0.5f + 0.5f * o;
                vec3 out = p - cc;
                const f32 radial = std::sqrt(out.x * out.x + out.z * out.z) / max(cr, 0.1f);
                out.y *= 0.6f; // crown normals bent outward: volumetric shading, as the conifer crown
                const vec3 nrm = normalize(n_card * 0.35f + normalize(out) * 0.65f);
                const f32 ao = clamp(0.3f + 0.7f * radial, 0.3f, 1.0f);
                m.vertices.push_back(make_vertex(p, nrm, frond_uv(f.tile, f.s[j], v), t, ao));
            }
        }
        for (u32 k = 0; k + 1 < js.size(); ++k) {
            const u32 a = base + 2 * k, b = a + 1, c = a + 2, d = a + 3;
            if (o > 0.0f)
                m.indices.insert(m.indices.end(), {a, c, b, b, c, d});
            else
                m.indices.insert(m.indices.end(), {a, b, c, b, d, c});
        }
    }
}

// ---- Frond atlas (baked leaflets) -------------------------------------------------------------------

constexpr u32 kTileW = 512, kTileH = 256, kDeadTile = 3;
constexpr u32 kSS = 2; // supersampling per axis
constexpr u32 kLeafSeg = 6;
constexpr f32 kMidrib = 128.0f; // midrib row of a tile

// A leaflet: a polyline from the midrib (tile px), with a half width that tapers to the tip.
struct Leaf {
    f32 px[kLeafSeg + 1], py[kLeafSeg + 1];
    f32 w0;
};

f32 leaf_halfw(const Leaf& L, f32 tau) { return max(0.45f, L.w0 * std::pow(1.0f - tau, 0.7f) + 0.45f); }

// Leaflets leave the midrib at x0 and fan out to `side` (+1/-1) towards the tip, at 24-48 deg. The tips arch back.
Leaf make_leaf(Rng& rng, f32 x0, f32 side, bool dead) {
    Leaf L{};
    const f32 beta = radians(rng.range(30.0f, 55.0f));
    const f32 len =
        (dead ? rng.range(45.0f, 85.0f) : rng.range(90.0f, 135.0f)) * (1.0f - (dead ? 0.6f : 0.5f) * x0 / f32(kTileW));
    L.w0 = dead ? 2.8f : 5.6f;
    for (u32 i = 0; i <= kLeafSeg; ++i) {
        const f32 t = f32(i) / f32(kLeafSeg);
        L.px[i] = x0 + len * std::cos(beta) * t;
        L.py[i] = kMidrib + side * (len * std::sin(beta) * t - 0.22f * len * t * t);
    }
    return L;
}

struct LeafHit {
    f32 dn;   // distance to the leaflet centre line, in half widths (< 1 = inside)
    f32 tau;  // position along the leaflet 0..1
    f32 side; // which side of the centre line
};

LeafHit leaf_eval(const Leaf& L, f32 x, f32 y) {
    LeafHit best{1e9f, 0.0f, 1.0f};
    for (u32 i = 0; i < kLeafSeg; ++i) {
        const f32 ax = L.px[i], ay = L.py[i];
        const f32 bx = L.px[i + 1] - ax, by = L.py[i + 1] - ay;
        const f32 qx = x - ax, qy = y - ay;
        const f32 l2 = max(bx * bx + by * by, 1e-6f);
        const f32 q = clamp((qx * bx + qy * by) / l2, 0.0f, 1.0f);
        const f32 dx = qx - bx * q, dy = qy - by * q;
        const f32 tau = (f32(i) + q) / f32(kLeafSeg);
        const f32 dn = std::sqrt(dx * dx + dy * dy) / leaf_halfw(L, tau);
        if (dn < best.dn) best = {dn, tau, (bx * qy - by * qx) >= 0.0f ? 1.0f : -1.0f};
    }
    return best;
}

// Living leaflets go from dark green at the base to lighter yellow-green at the tip; the three variants tint them.
// Dead fronds are brown.
vec3 leaf_color(u32 tile, f32 tau, f32 shade, bool dead) {
    if (dead) return lerp(vec3{0.22f, 0.15f, 0.07f}, vec3{0.36f, 0.27f, 0.13f}, tau) * shade;
    const vec3 old_c{0.07f, 0.165f, 0.03f}, young_c{0.17f, 0.28f, 0.07f};
    const vec3 tint = tile == 1 ? vec3{1.06f, 1.0f, 0.86f} : tile == 2 ? vec3{0.9f, 1.02f, 1.1f} : vec3{1, 1, 1};
    return cmul(lerp(old_c, young_c, tau), tint) * shade;
}

} // namespace

PalmMeshes make_palm(u32 seed, f32 h) {
    Rng rng(seed * 2654435761u + 7u);
    PalmMeshes out;
    out.height = h;

    const f32 r0 = rng.range(0.15f, 0.20f);
    const f32 tilt = radians(rng.range(8.0f, 34.0f)), azim = radians(rng.range(-10.0f, 10.0f));
    Axis ax;
    ax.h = h;
    ax.dx = std::cos(azim);
    ax.dz = std::sin(azim);
    ax.amount = h * std::tan(tilt) * 0.5f; // slope at the top = 2 * amount / h = tan(tilt)
    TrunkShape sh{};
    for (int i = 0; i < 2; ++i) {
        sh.amp[i] = (i == 0 ? 0.03f : 0.02f) * rng.range(0.5f, 1.0f);
        sh.phase[i] = rng.range(0.0f, kTwoPi);
    }
    const f32 top = h * 0.985f;
    const vec3 apex = ax.at(top) + vec3{0.0f, 0.15f, 0.0f};

    // Trunk. LOD0 rings are dense at the base (bulb, roots); LOD1 keeps the same axis with few rings.
    std::vector<f32> ys0 = {-0.8f, -0.2f, 0.0f, 0.2f, 0.45f, 0.8f, 1.2f};
    for (f32 y = 1.2f + 0.45f; y < top - 0.05f; y += 0.45f) ys0.push_back(y);
    ys0.push_back(top);
    std::vector<f32> ys1 = {-0.8f, 0.0f, 1.0f};
    for (f32 y = 1.0f + 2.5f; y < top - 0.6f; y += 2.5f) ys1.push_back(y);
    ys1.push_back(top);
    add_trunk(out.trunk_lod0, ax, sh, r0, ys0, 12);
    add_trunk(out.trunk_lod1, ax, sh, r0, ys1, 4);

    // Fronds: 14-22 in a rosette, youngest upright, oldest hanging, the last 2-4 dead (brown).
    const u32 n = 20 + rng.next() % 7u, dead_n = 2 + rng.next() % 3u;
    const f32 base_az = rng.range(0.0f, kTwoPi);
    std::vector<Frond> fronds;
    fronds.reserve(n);
    for (u32 k = 0; k < n; ++k) {
        const bool dead = k >= n - dead_n;
        const f32 age = dead ? 1.0f : (f32(k) + rng.range(0.0f, 0.7f)) / f32(n);
        const f32 az = base_az + f32(k) * 2.39996323f + rng.range(-0.2f, 0.2f);
        const f32 len = rng.range(4.2f, 5.8f) * (dead ? 0.9f : 1.0f);
        fronds.push_back(make_frond(rng, apex, az, age, dead, len));
    }
    f32 reach = 0.0f;
    for (const Frond& f : fronds)
        for (u32 j = 0; j < kStations; ++j)
            reach = max(reach, std::sqrt(sq(f.p[j].x - apex.x) + sq(f.p[j].z - apex.z)));
    out.crown_radius = reach;
    out.canopy_radius = 0.6f * reach;
    for (const Frond& f : fronds) {
        add_frond_halves(out.crown_lod0, f, 1, apex, reach);
        add_frond_halves(out.crown_lod1, f, 2, apex, reach);
    }

    // Coconuts in a loose cluster under the crown (bark material).
    const u32 nuts = 4 + rng.next() % 4u;
    for (u32 i = 0; i < nuts; ++i) {
        const f32 a = rng.range(0.0f, kTwoPi), rad = rng.range(0.18f, 0.28f);
        const vec3 c = ax.at(top - 0.4f) + vec3{std::cos(a) * rad, -0.25f - rng.range(0.0f, 0.25f), std::sin(a) * rad};
        append_mesh(out.trunk_lod0, make_sphere(rng.range(0.085f, 0.115f), 8, 4), c);
    }
    return out;
}

void make_frond_textures(u32 seed, u32 w, u32 h, std::vector<u8>& albedo, std::vector<u8>& normal) {
    TEFRA_VERIFY(w == 2 * kTileW && h == 2 * kTileH);
    const u32 sw = kTileW * kSS, sh = kTileH * kSS;
    std::vector<f32> cov(usize(w) * h, 0.0f), ao_out(usize(w) * h, 1.0f);
    std::vector<vec3> col(usize(w) * h, vec3{}), nrm(usize(w) * h, vec3{0, 0, 1});
    std::vector<u8> has(usize(w) * h, 0);

    // The four tiles are independent: each has its own supersampled scratch and writes only its own quadrant of the
    // atlas, so they are painted in parallel (the result is the same as a serial loop).
    auto render_tile = [&](u32 tile) {
        // Supersampled scratch for one tile: last painted surface wins (leaflets overlap like real ones).
        std::vector<u8> hit(usize(sw) * sh);
        std::vector<vec3> sc(usize(sw) * sh), sn(usize(sw) * sh);
        std::vector<f32> sa(usize(sw) * sh);
        Rng rng(seed * 131u + tile * 977u + 5u);
        const bool dead = tile == kDeadTile;
        std::fill(hit.begin(), hit.end(), u8(0));

        auto paint = [&](const Leaf& L, f32 shade) {
            f32 minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f;
            for (u32 i = 0; i <= kLeafSeg; ++i) {
                minx = min(minx, L.px[i]);
                maxx = max(maxx, L.px[i]);
                miny = min(miny, L.py[i]);
                maxy = max(maxy, L.py[i]);
            }
            const f32 pad = L.w0 + 2.0f;
            const u32 x0 = u32(clamp(std::floor((minx - pad) * kSS), 0.0f, f32(sw - 1)));
            const u32 x1 = u32(clamp(std::ceil((maxx + pad) * kSS), 0.0f, f32(sw - 1)));
            const u32 y0 = u32(clamp(std::floor((miny - pad) * kSS), 0.0f, f32(sh - 1)));
            const u32 y1 = u32(clamp(std::ceil((maxy + pad) * kSS), 0.0f, f32(sh - 1)));
            for (u32 sy = y0; sy <= y1; ++sy)
                for (u32 sx = x0; sx <= x1; ++sx) {
                    const f32 x = (f32(sx) + 0.5f) / f32(kSS), y = (f32(sy) + 0.5f) / f32(kSS);
                    const LeafHit hh = leaf_eval(L, x, y);
                    if (hh.dn >= 1.0f) continue;
                    const usize i = usize(sy) * sw + sx;
                    hit[i] = 1;
                    sc[i] = leaf_color(tile, hh.tau, shade, dead);
                    // V-fold: the leaflet surface tilts away from its centre line; AO darkens the rachis side.
                    sn[i] = vec3{0.0f, 0.5f * hh.side * hh.dn, 1.0f};
                    sa[i] = (0.8f + 0.2f * hh.dn) * (0.45f + 0.55f * smooth01(0.0f, 40.0f, std::fabs(y - kMidrib)));
                }
        };

        const u32 n_leaf = dead ? 30 : 68;
        for (f32 side : {1.0f, -1.0f})
            for (u32 i = 0; i < n_leaf; ++i) {
                if (dead && rng.f() < 0.35f) continue; // torn or missing leaflets
                const f32 x0 = lerp(12.0f, 470.0f, (f32(i) + rng.range(0.15f, 0.85f)) / f32(n_leaf));
                const Leaf L = make_leaf(rng, x0, side, dead);
                paint(L, rng.range(0.85f, 1.12f));
            }

        // Midrib (rachis) on top of the leaflets, tapering to the tip.
        const vec3 rachis_c = dead ? vec3{0.30f, 0.20f, 0.09f} : vec3{0.16f, 0.23f, 0.06f};
        for (u32 sy = 0; sy < sh; ++sy)
            for (u32 sx = 0; sx < sw; ++sx) {
                const f32 x = (f32(sx) + 0.5f) / f32(kSS), y = (f32(sy) + 0.5f) / f32(kSS);
                const f32 hw = lerp(dead ? 1.6f : 2.4f, dead ? 0.6f : 0.9f, x / f32(kTileW));
                if (std::fabs(y - kMidrib) >= hw) continue;
                const usize i = usize(sy) * sw + sx;
                hit[i] = 1;
                sc[i] = rachis_c;
                sn[i] = vec3{0.0f, 0.0f, 1.0f};
                sa[i] = 0.7f;
            }

        // Box-filter the supersampled tile into its atlas quadrant.
        const u32 ox = (tile % 2) * kTileW, oy = (tile / 2) * kTileH;
        for (u32 y = 0; y < kTileH; ++y)
            for (u32 x = 0; x < kTileW; ++x) {
                vec3 c_acc{}, n_acc{};
                f32 a_acc = 0.0f;
                u32 hits = 0;
                for (u32 sy = 0; sy < kSS; ++sy)
                    for (u32 sx = 0; sx < kSS; ++sx) {
                        const usize i = usize(y * kSS + sy) * sw + (x * kSS + sx);
                        if (!hit[i]) continue;
                        ++hits;
                        c_acc += sc[i];
                        n_acc += sn[i];
                        a_acc += sa[i];
                    }
                const usize ai = usize(oy + y) * w + (ox + x);
                cov[ai] = f32(hits) / f32(kSS * kSS);
                if (hits > 0) {
                    has[ai] = 1;
                    col[ai] = c_acc * (1.0f / f32(hits));
                    nrm[ai] = normalize(n_acc);
                    ao_out[ai] = a_acc / f32(hits);
                }
            }
    };
    global_jobs().parallel_for(4, 1, [&](u32 begin, u32 end) {
        for (u32 t = begin; t < end; ++t) render_tile(t);
    });

    // Texels without leaflets take the mean leaflet colour, so filtering never pulls in black.
    vec3 mean{};
    u32 cnt = 0;
    for (usize i = 0; i < cov.size(); ++i)
        if (has[i]) {
            mean += col[i];
            ++cnt;
        }
    mean = cnt ? mean * (1.0f / f32(cnt)) : vec3{0.1f, 0.2f, 0.05f};

    albedo.resize(usize(w) * h * 4);
    normal.resize(usize(w) * h * 4);
    auto srgb = [](f32 v) {
        v = saturate(v);
        const f32 e = v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
        return static_cast<u8>(std::lround(e * 255.0f));
    };
    auto unorm = [](f32 v) { return static_cast<u8>(std::lround(saturate(v) * 255.0f)); };
    for (usize i = 0; i < cov.size(); ++i) {
        const vec3 c = has[i] ? col[i] : mean;
        albedo[i * 4 + 0] = srgb(c.x);
        albedo[i * 4 + 1] = srgb(c.y);
        albedo[i * 4 + 2] = srgb(c.z);
        albedo[i * 4 + 3] = unorm(cov[i]);
        // Tangent-space normal. G is along +V: the frond cards' bitangent (see add_frond_halves).
        const vec3 n = has[i] ? nrm[i] : vec3{0.0f, 0.0f, 1.0f};
        normal[i * 4 + 0] = unorm(n.x * 0.5f + 0.5f);
        normal[i * 4 + 1] = unorm(n.y * 0.5f + 0.5f);
        normal[i * 4 + 2] = unorm(n.z * 0.5f + 0.5f);
        normal[i * 4 + 3] = unorm(ao_out[i]); // depth AO (material flag 8)
    }
}

} // namespace tefra::render
