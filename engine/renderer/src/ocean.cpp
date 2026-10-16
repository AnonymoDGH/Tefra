#include "tefra/renderer/ocean.h"

#include "tefra/core/log.h"
#include "tefra/rhi/upload.h"

#include <cmath>

namespace tefra::render {
namespace {
constexpr u32 kColumnsPerSector = 8;
constexpr u32 kColumns = Ocean::kSectors * kColumnsPerSector; // radial lines
constexpr f32 kInnerRadius = 0.35f;                           // m
constexpr f32 kOuterRadius = 40000.0f;                        // m: past the horizon from beach height
constexpr f32 kGravity = 9.81f;

u32 hash(u32 x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}
f32 rnd(u32 a, u32 b) { return f32(hash(a * 9781u + b * 6271u + 17u) >> 8) / 16777216.0f; }
} // namespace

Ocean::Ocean(const rhi::Device& dev, const OceanDesc& desc) : dev_(dev), desc_(desc) {
    build_grid();
    build_waves();
}

Ocean::~Ocean() {
    rhi::destroy_buffer(dev_, vb_);
    rhi::destroy_buffer(dev_, ib_);
    rhi::destroy_buffer(dev_, waves_);
}

void Ocean::build_grid() {
    // Rings spaced geometrically so cells stay roughly square: dr = r * dtheta.
    const f32 growth = 1.0f + kTwoPi / f32(kColumns);
    std::vector<f32> radii;
    for (f32 r = kInnerRadius; r < kOuterRadius; r *= growth) radii.push_back(r);
    radii.push_back(kOuterRadius);
    const u32 rings = static_cast<u32>(radii.size()), cols = kColumns + 1;
    std::vector<vec4> v;
    v.reserve(usize(rings) * cols + 1);
    v.push_back({0.0f, 0.0f, 0.0f, 0.0f}); // centre
    for (u32 r = 0; r < rings; ++r)
        for (u32 c = 0; c < cols; ++c) {
            const f32 a = f32(c) / f32(kColumns) * kTwoPi;
            v.push_back({std::cos(a) * radii[r], std::sin(a) * radii[r], radii[r], 0.0f});
        }
    auto vid = [&](u32 r, u32 c) { return 1 + r * cols + c; };
    std::vector<u32> idx;
    for (u32 s = 0; s < kSectors; ++s) {
        const usize start = idx.size();
        for (u32 c = s * kColumnsPerSector; c < (s + 1) * kColumnsPerSector; ++c) {
            idx.insert(idx.end(), {0u, vid(0, c + 1), vid(0, c)});
            for (u32 r = 0; r + 1 < rings; ++r) {
                const u32 a = vid(r, c), b = vid(r, c + 1), cc = vid(r + 1, c), d = vid(r + 1, c + 1);
                idx.insert(idx.end(), {a, b, cc, b, d, cc});
            }
        }
        if (s == 0) sector_indices_ = static_cast<u32>(idx.size() - start);
    }
    vb_ = rhi::upload_buffer(dev_, v.data(), v.size() * sizeof(vec4), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "ocean_vb");
    ib_ = rhi::upload_buffer(dev_, idx.data(), idx.size() * sizeof(u32), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, "ocean_ib");
    TEFRA_LOG_INFO("Ocean grid: %u rings x %u columns, %u vertices, %u triangles", rings, kColumns, u32(v.size()),
                   u32(idx.size() / 3));
}

void Ocean::build_waves() {
    std::vector<OceanWaveGPU> w(kGeometryWaves + kDetailWaves);
    const f32 wind_a = std::atan2(desc_.wind_dir.y, desc_.wind_dir.x);
    // Geometry: swell (long, aligned with the wind) to wind chop (short, spread). Equal steepness, then the set is
    // scaled to the requested significant height Hs = 4 * sqrt(sum A^2 / 2).
    f32 var = 0.0f;
    for (u32 i = 0; i < kGeometryWaves; ++i) {
        const f32 t = f32(i) / f32(kGeometryWaves - 1);
        const f32 lambda = 75.0f * std::pow(3.2f / 75.0f, t) * (0.9f + 0.2f * rnd(i, 1));
        const f32 k = kTwoPi / lambda;
        const f32 spread = (0.15f + 1.05f * t) * (i % 2 ? 1.0f : -1.0f) * (0.5f + 0.5f * rnd(i, 2));
        const f32 a = wind_a + spread;
        const f32 amp = 0.07f / k * (0.6f + 0.8f * rnd(i, 4)); // jittered: no clean spectral comb
        w[i].dir_k = {std::cos(a), std::sin(a), k, std::sqrt(kGravity * k)};
        w[i].amp = {amp, 0.0f, rnd(i, 3) * kTwoPi, 0.0f};
        var += amp * amp * 0.5f;
    }
    const f32 scale = desc_.swell_height / (4.0f * std::sqrt(var));
    for (u32 i = 0; i < kGeometryWaves; ++i) {
        w[i].amp.x *= scale;
        // Gerstner steepness: the set together stays below the loop limit (sum Q k A < 1).
        w[i].amp.y = 0.75f / (w[i].dir_k.z * w[i].amp.x * f32(kGeometryWaves));
        w[i].amp.y = min(w[i].amp.y, 1.0f);
    }
    // Detail (fragment normals only): capillary-gravity ripples, wide spread around the wind.
    for (u32 i = 0; i < kDetailWaves; ++i) {
        const f32 t = f32(i) / f32(kDetailWaves - 1);
        const f32 lambda = 2.8f * std::pow(0.3f / 2.8f, t) * (0.85f + 0.3f * rnd(i, 11));
        const f32 k = kTwoPi / lambda;
        const f32 a = wind_a + (rnd(i, 12) * 2.0f - 1.0f) * 1.8f;
        const f32 omega = std::sqrt(kGravity * k + 0.074f / 1000.0f * k * k * k); // + surface tension
        w[kGeometryWaves + i].dir_k = {std::cos(a), std::sin(a), k, omega};
        w[kGeometryWaves + i].amp = {(0.042f - 0.018f * t) / k, 0.0f, rnd(i, 13) * kTwoPi, 0.0f};
    }
    waves_ = rhi::upload_buffer(dev_, w.data(), w.size() * sizeof(OceanWaveGPU), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                "ocean_waves");
    host_waves_.assign(w.begin(), w.begin() + kGeometryWaves);
}

vec3 Ocean::displacement(vec2 xz, f32 t, f32 depth) const {
    // Same as ocean_eval() in shaders/common/ocean.glsl (geometry waves, no footprint filter).
    auto smooth = [](f32 a, f32 b, f32 x) {
        const f32 u = clamp((x - a) / (b - a), 0.0f, 1.0f);
        return u * u * (3.0f - 2.0f * u);
    };
    const f32 deep = smooth(0.5f, 12.0f, depth) * 0.8f + 0.2f * smooth(-0.3f, 1.0f, depth);
    vec3 d{0.0f, 0.0f, 0.0f};
    for (const OceanWaveGPU& w : host_waves_) {
        const f32 k = w.dir_k.z, Q = w.amp.y, A = w.amp.x * deep;
        const f32 th = k * (w.dir_k.x * xz.x + w.dir_k.y * xz.y) - w.dir_k.w * t + w.amp.z;
        const f32 c = std::cos(th), s = std::sin(th);
        d = d + vec3{Q * A * w.dir_k.x * c, A * s, Q * A * w.dir_k.y * c};
    }
    return d;
}

f32 Ocean::height_at(f32 x, f32 z, f32 t, f32 depth) const {
    // A surface point that ends up over (x, z) started at p - disp(p): a few fixed-point steps find it.
    vec2 p{x, z};
    for (int i = 0; i < 3; ++i) {
        const vec3 d = displacement(p, t, depth);
        p = vec2{x - d.x, z - d.z};
    }
    return desc_.level + displacement(p, t, depth).y;
}

} // namespace tefra::render
