#include "tefra/renderer/terrain.h"

#include "tefra/core/fs.h"
#include "tefra/core/image.h"
#include "tefra/core/log.h"
#include "tefra/core/time.h"
#include "tefra/rhi/bindless.h"
#include "tefra/rhi/device.h"
#include "tefra/rhi/upload.h"

#include <cmath>
#include <thread>

namespace tefra::render {
namespace {

u32 hash2(i32 x, i32 y, u32 seed) {
    u32 h = static_cast<u32>(x) * 0x8da6b343u ^ static_cast<u32>(y) * 0xd8163841u ^ seed * 0xcb1ab31fu;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return h;
}

// 2D gradient noise in [-1, 1] (quintic fade).
f32 gnoise(f32 x, f32 y, u32 seed) {
    const f32 fx = std::floor(x), fy = std::floor(y);
    const i32 ix = static_cast<i32>(fx), iy = static_cast<i32>(fy);
    const f32 tx = x - fx, ty = y - fy;
    auto grad = [&](i32 cx, i32 cy, f32 dx, f32 dy) {
        const u32 h = hash2(cx, cy, seed);
        const f32 a = static_cast<f32>(h & 0xFFFFu) * (kTwoPi / 65536.0f);
        return std::cos(a) * dx + std::sin(a) * dy;
    };
    const f32 n00 = grad(ix, iy, tx, ty), n10 = grad(ix + 1, iy, tx - 1, ty);
    const f32 n01 = grad(ix, iy + 1, tx, ty - 1), n11 = grad(ix + 1, iy + 1, tx - 1, ty - 1);
    auto fade = [](f32 t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); };
    const f32 u = fade(tx), v = fade(ty);
    return 1.41f * lerp(lerp(n00, n10, u), lerp(n01, n11, u), v);
}

f32 fbm(f32 x, f32 y, u32 seed, int octaves, f32 lacunarity = 2.03f, f32 gain = 0.5f) {
    f32 sum = 0.0f, amp = 0.5f, freq = 1.0f;
    for (int i = 0; i < octaves; ++i) {
        sum += amp * gnoise(x * freq, y * freq, seed + static_cast<u32>(i) * 101u);
        freq *= lacunarity;
        amp *= gain;
    }
    return sum;
}

f32 ridged(f32 x, f32 y, u32 seed, int octaves) {
    f32 sum = 0.0f, amp = 0.5f, freq = 1.0f, prev = 1.0f;
    for (int i = 0; i < octaves; ++i) {
        f32 n = 1.0f - std::fabs(gnoise(x * freq, y * freq, seed + static_cast<u32>(i) * 57u));
        n *= n;
        sum += n * amp * prev;
        prev = n;
        freq *= 2.1f;
        amp *= 0.5f;
    }
    return sum;
}

f32 smoothstep(f32 a, f32 b, f32 x) {
    const f32 t = saturate((x - a) / (b - a));
    return t * t * (3.0f - 2.0f * t);
}

} // namespace

Terrain::Terrain(const rhi::Device& dev, rhi::BindlessHeap& heap, const TerrainDesc& desc)
    : dev_(dev), heap_(heap), desc_(desc) {
    const u64 t0 = time_now_ns();
    generate();
    upload_textures();
    build_grid();
    TEFRA_LOG_INFO("Terrain %ux%u @ %.2f m: heights %.1f..%.1f m, built in %.0f ms", desc_.resolution, desc_.resolution,
                   static_cast<f64>(desc_.texel_size), static_cast<f64>(min_h_), static_cast<f64>(max_h_),
                   ns_to_ms(time_now_ns() - t0));
}

Terrain::~Terrain() {
    for (usize i = 0; i < images_.size(); ++i) {
        heap_.remove_texture(image_indices_[i]);
        rhi::destroy_image(dev_, images_[i]);
    }
    rhi::destroy_buffer(dev_, grid_vb_);
    rhi::destroy_buffer(dev_, grid_ib_);
}

f32 Terrain::river_center_x(f32 z) const {
    return 110.0f * std::sin(z / 420.0f) + 45.0f * std::sin(z / 160.0f + 1.3f) + 12.0f * std::sin(z / 47.0f + 0.4f);
}

f32 Terrain::river_distance(f32 x, f32 z) const { return x - river_center_x(z); }

f32 Terrain::valley_height(f32 x, f32 z) const {
    const u32 seed = desc_.seed;
    // Domain warp for organic shapes.
    const f32 wx = x + 60.0f * fbm(x / 700.0f, z / 700.0f, seed + 7, 3);
    const f32 wz = z + 60.0f * fbm(x / 700.0f + 5.2f, z / 700.0f + 1.3f, seed + 9, 3);
    const f32 d = std::fabs(wx - river_center_x(wz));
    // Valley walls: flat floor (~60 m wide), steepening slopes, crest ridges near the map edge.
    const f32 dv = min(d, std::fabs(x - river_center_x(z)) + 25.0f); // keep the floor around the channel
    const f32 wall = std::pow(max(dv - 30.0f, 0.0f) / 900.0f, 1.55f) * 260.0f;
    const f32 ridges = ridged(wx / 520.0f, wz / 520.0f, seed + 3, 5) * 70.0f * smoothstep(60.0f, 400.0f, d);
    const f32 hills = fbm(wx / 230.0f, wz / 230.0f, seed + 11, 5) * 14.0f * smoothstep(20.0f, 200.0f, d);
    const f32 floor_bumps = fbm(x / 35.0f, z / 35.0f, seed + 13, 4) * 1.6f;
    f32 h = 20.0f + wall + ridges + hills + floor_bumps;
    // River channel and banks follow the unwarped centerline (water and gameplay use it).
    const f32 dc = std::fabs(x - river_center_x(z));
    const f32 channel = 1.0f - smoothstep(3.0f, 9.0f, dc);
    h -= channel * 2.6f;
    // Lake basin downstream.
    const f32 lx = x - river_center_x(520.0f), lz = (z - 520.0f) * 0.7f;
    const f32 lake_d = std::sqrt(lx * lx + lz * lz);
    const f32 lake = 1.0f - smoothstep(60.0f, 160.0f, lake_d);
    h = lerp(h, 15.5f + floor_bumps * 0.3f, lake);
    return h;
}

// Shoreline of the cove (world z where the beach meets the still sea): bowed inland at the centre, two rocky
// headlands reaching out to sea at |x| ~ 450 m.
f32 Terrain::coast_shore_z(f32 x) const {
    const f32 u = x / 400.0f;
    const f32 ax = std::fabs(x);
    return -70.0f * u * u - 260.0f * smoothstep(320.0f, 560.0f, ax) + 10.0f * fbm(x / 140.0f, 0.5f, desc_.seed + 5, 3);
}

f32 Terrain::coast_shore_z_dist(f32 x, f32 z) const { return z - coast_shore_z(x); }

f32 Terrain::coast_height(f32 x, f32 z) const {
    const u32 seed = desc_.seed;
    const f32 sea = desc_.sea_level;
    // Gentle domain warp so the beach and dunes are not ruler-straight.
    const f32 wx = x + 18.0f * fbm(x / 260.0f, z / 260.0f, seed + 7, 3);
    const f32 wz = z + 18.0f * fbm(x / 260.0f + 3.1f, z / 260.0f + 8.4f, seed + 9, 3);
    const f32 s = wz - coast_shore_z(wx); // > 0 on land, < 0 at sea
    const f32 ax = std::fabs(wx);
    const f32 headland = smoothstep(300.0f, 470.0f, ax);
    f32 h;
    if (s >= 0.0f) {
        // Beach face (~2.6 deg) up to the berm, a flat backshore, dunes, then the rising hinterland.
        const f32 face = min(s, 35.0f) * 0.045f;
        const f32 back = smoothstep(35.0f, 75.0f, s) * 0.8f;
        const f32 dunes =
            smoothstep(60.0f, 120.0f, s) * (2.5f + 4.0f * (0.5f + 0.5f * fbm(wx / 55.0f, wz / 40.0f, seed + 3, 4)));
        const f32 inland = max(s - 150.0f, 0.0f);
        const f32 hills = std::pow(inland / 600.0f, 1.3f) * 90.0f +
                          fbm(wx / 240.0f, wz / 240.0f, seed + 11, 5) * 22.0f * smoothstep(150.0f, 400.0f, s);
        h = face + back + dunes + hills;
    } else {
        const f32 d = -s;
        // Foreshore, a longshore sand bar, the shelf and a steeper drop to deep water.
        h = -min(d, 40.0f) * 0.04f - max(d - 40.0f, 0.0f) * 0.022f - max(d - 320.0f, 0.0f) * 0.045f;
        h += 0.9f * std::exp(-((d - 78.0f) / 20.0f) * ((d - 78.0f) / 20.0f)) * (1.0f - headland);
        h = max(h, -42.0f);
    }
    // Rocky headlands: cliffs rising steeply from the waterline, reefs below it.
    if (headland > 0.0f) {
        const f32 r = ridged(wx / 160.0f, wz / 160.0f, seed + 17, 5);
        const f32 cliff = smoothstep(-6.0f, 22.0f, s) * (22.0f + 38.0f * r);
        const f32 reef = (1.0f - smoothstep(0.0f, 70.0f, -s)) * 2.5f * r;
        h += headland * (s > 0.0f ? cliff : reef + cliff);
    }
    // Small undulations: wind ripples are in the textures, this is the metre scale.
    h += fbm(x / 9.0f, z / 9.0f, seed + 13, 3) * 0.08f +
         fbm(x / 40.0f, z / 40.0f, seed + 19, 3) * 0.25f * smoothstep(-5.0f, 5.0f, s);
    return sea + h;
}

void Terrain::generate() {
    const u32 n = desc_.resolution;
    const f32 half = world_size() * 0.5f;
    heights_.resize(usize(n) * n);
    mask_.resize(usize(n / 2) * (n / 2) * 4);
    const u32 seed = desc_.seed;
    const bool coast = desc_.kind == TerrainKind::Coast;
    auto height_fn = [&](f32 x, f32 z) { return coast ? coast_height(x, z) : valley_height(x, z); };
    // Parallel over rows (the reference PC has 4 threads; the cloud has 4).
    const u32 threads = max(1u, std::thread::hardware_concurrency());
    std::vector<std::thread> pool;
    for (u32 t = 0; t < threads; ++t) {
        pool.emplace_back([&, t] {
            for (u32 y = t; y < n; y += threads)
                for (u32 x = 0; x < n; ++x) {
                    const f32 wx = (f32(x) + 0.5f) * desc_.texel_size - half;
                    const f32 wz = (f32(y) + 0.5f) * desc_.texel_size - half;
                    heights_[usize(y) * n + x] = height_fn(wx, wz);
                }
        });
    }
    for (auto& th : pool) th.join();
    min_h_ = 1e30f;
    max_h_ = -1e30f;
    for (f32 h : heights_) {
        min_h_ = min(min_h_, h);
        max_h_ = max(max_h_, h);
    }
    // Mask (half resolution): r = wetness (river/lake proximity), g = patchiness, b = path, a = unused.
    const u32 m = n / 2;
    for (u32 y = 0; y < m; ++y)
        for (u32 x = 0; x < m; ++x) {
            const f32 wx = (f32(x) + 0.5f) * desc_.texel_size * 2.0f - half;
            const f32 wz = (f32(y) + 0.5f) * desc_.texel_size * 2.0f - half;
            const f32 h = height_at(wx, wz);
            f32 wet;
            if (coast) {
                // Wet sand below the swash limit (~0.9 m above the still sea); the sea bed is fully wet.
                wet = 1.0f - smoothstep(desc_.sea_level + 0.15f, desc_.sea_level + 0.9f, h);
            } else {
                const f32 d = std::fabs(river_distance(wx, wz));
                wet = max(1.0f - smoothstep(4.0f, 22.0f, d), 1.0f - smoothstep(15.6f, 17.5f, h));
            }
            const f32 patch = 0.5f + 0.5f * fbm(wx / 9.0f, wz / 9.0f, seed + 21, 4);
            u8* p = &mask_[(usize(y) * m + x) * 4];
            p[0] = static_cast<u8>(saturate(wet) * 255.0f);
            p[1] = static_cast<u8>(saturate(patch) * 255.0f);
            // b: vegetation cover (coast: scrub and grass behind the dunes).
            f32 cover = 0.0f;
            if (coast) {
                const f32 land = coast_shore_z_dist(wx, wz);
                cover = smoothstep(90.0f, 170.0f, land + 40.0f * fbm(wx / 80.0f, wz / 80.0f, seed + 23, 3));
                cover = max(cover, smoothstep(9.0f, 16.0f, h - desc_.sea_level)); // headland tops and hills
            }
            p[2] = static_cast<u8>(saturate(cover) * 255.0f);
            p[3] = 255;
        }
    if (coast) build_shore_distance();
}

namespace {
// Felzenszwalb & Huttenlocher 1D squared distance transform (f: 0 at sites, large elsewhere).
void edt_1d(const f32* f, f32* d, u32 n, std::vector<i32>& v, std::vector<f32>& z) {
    v.resize(n);
    z.resize(n + 1);
    u32 k = 0;
    v[0] = 0;
    z[0] = -1e30f;
    z[1] = 1e30f;
    for (u32 q = 1; q < n; ++q) {
        f32 sq = 0;
        for (;;) {
            const i32 p = v[k];
            sq = ((f[q] + f32(q) * f32(q)) - (f[p] + f32(p) * f32(p))) / (2.0f * f32(q) - 2.0f * f32(p));
            if (sq <= z[k] && k > 0) {
                --k;
                continue;
            }
            break;
        }
        ++k;
        v[k] = i32(q);
        z[k] = sq;
        z[k + 1] = 1e30f;
    }
    k = 0;
    for (u32 q = 0; q < n; ++q) {
        while (z[k + 1] < f32(q)) ++k;
        const f32 dq = f32(q) - f32(v[k]);
        d[q] = dq * dq + f[v[k]];
    }
}

// 2D Euclidean distance (texels) to the nearest site (sites[i] != 0).
std::vector<f32> edt_2d(const std::vector<u8>& sites, u32 n) {
    std::vector<f32> g(usize(n) * n), col(n), out(n);
    std::vector<i32> v;
    std::vector<f32> z;
    for (usize i = 0; i < g.size(); ++i) g[i] = sites[i] ? 0.0f : 1e20f;
    for (u32 x = 0; x < n; ++x) {
        for (u32 y = 0; y < n; ++y) col[y] = g[usize(y) * n + x];
        edt_1d(col.data(), out.data(), n, v, z);
        for (u32 y = 0; y < n; ++y) g[usize(y) * n + x] = out[y];
    }
    for (u32 y = 0; y < n; ++y) {
        edt_1d(&g[usize(y) * n], out.data(), n, v, z);
        for (u32 x = 0; x < n; ++x) g[usize(y) * n + x] = std::sqrt(out[x]);
    }
    return g;
}
} // namespace

void Terrain::build_shore_distance() {
    const u32 m = desc_.resolution / 2;
    const f32 texel = desc_.texel_size * 2.0f, half = world_size() * 0.5f;
    std::vector<u8> land(usize(m) * m), sea(usize(m) * m);
    for (u32 y = 0; y < m; ++y)
        for (u32 x = 0; x < m; ++x) {
            const f32 h = height_at((f32(x) + 0.5f) * texel - half, (f32(y) + 0.5f) * texel - half);
            land[usize(y) * m + x] = h > desc_.sea_level ? 1 : 0;
            sea[usize(y) * m + x] = h > desc_.sea_level ? 0 : 1;
        }
    const std::vector<f32> to_land = edt_2d(land, m), to_sea = edt_2d(sea, m);
    shore_.resize(usize(m) * m);
    for (usize i = 0; i < shore_.size(); ++i) shore_[i] = (land[i] ? -(to_sea[i] - 0.5f) : (to_land[i] - 0.5f)) * texel;
    // Two box passes: the medial-axis kinks of the distance field would show up as kinks in the surf.
    std::vector<f32> tmp(shore_.size());
    for (int pass = 0; pass < 2; ++pass) {
        for (u32 y = 0; y < m; ++y)
            for (u32 x = 0; x < m; ++x) {
                f32 acc = 0;
                for (i32 k = -2; k <= 2; ++k) acc += shore_[usize(y) * m + u32(clamp(i32(x) + k, 0, i32(m) - 1))];
                tmp[usize(y) * m + x] = acc / 5.0f;
            }
        for (u32 y = 0; y < m; ++y)
            for (u32 x = 0; x < m; ++x) {
                f32 acc = 0;
                for (i32 k = -2; k <= 2; ++k) acc += tmp[usize(u32(clamp(i32(y) + k, 0, i32(m) - 1))) * m + x];
                shore_[usize(y) * m + x] = acc / 5.0f;
            }
    }
}

f32 Terrain::shore_distance(f32 x, f32 z) const {
    if (shore_.empty()) return 1e4f;
    const u32 m = desc_.resolution / 2;
    const f32 texel = desc_.texel_size * 2.0f, half = world_size() * 0.5f;
    const f32 fx = clamp((x + half) / texel - 0.5f, 0.0f, f32(m - 1));
    const f32 fz = clamp((z + half) / texel - 0.5f, 0.0f, f32(m - 1));
    const u32 x0 = static_cast<u32>(fx), z0 = static_cast<u32>(fz);
    const u32 x1 = min(x0 + 1, m - 1), z1 = min(z0 + 1, m - 1);
    const f32 tx = fx - f32(x0), tz = fz - f32(z0);
    const f32 a = lerp(shore_[usize(z0) * m + x0], shore_[usize(z0) * m + x1], tx);
    const f32 b = lerp(shore_[usize(z1) * m + x0], shore_[usize(z1) * m + x1], tx);
    return lerp(a, b, tz);
}

f32 Terrain::height_at(f32 x, f32 z) const {
    const u32 n = desc_.resolution;
    const f32 half = world_size() * 0.5f;
    const f32 fx = clamp((x + half) / desc_.texel_size - 0.5f, 0.0f, f32(n - 1));
    const f32 fz = clamp((z + half) / desc_.texel_size - 0.5f, 0.0f, f32(n - 1));
    const u32 x0 = static_cast<u32>(fx), z0 = static_cast<u32>(fz);
    const u32 x1 = min(x0 + 1, n - 1), z1 = min(z0 + 1, n - 1);
    const f32 tx = fx - f32(x0), tz = fz - f32(z0);
    const f32 a = lerp(heights_[usize(z0) * n + x0], heights_[usize(z0) * n + x1], tx);
    const f32 b = lerp(heights_[usize(z1) * n + x0], heights_[usize(z1) * n + x1], tx);
    return lerp(a, b, tz);
}

vec3 Terrain::normal_at(f32 x, f32 z) const {
    const f32 e = desc_.texel_size;
    const f32 hx = height_at(x + e, z) - height_at(x - e, z);
    const f32 hz = height_at(x, z + e) - height_at(x, z - e);
    return normalize(vec3{-hx, 2.0f * e, -hz});
}

void Terrain::upload_textures() {
    const u32 n = desc_.resolution;
    std::vector<u16> h16(heights_.size());
    const f32 range = max(max_h_ - min_h_, 1e-3f);
    for (usize i = 0; i < heights_.size(); ++i)
        h16[i] = static_cast<u16>(std::lround(saturate((heights_[i] - min_h_) / range) * 65535.0f));
    auto add = [&](rhi::Image&& img) {
        const u32 idx = heap_.add_texture(img.view);
        images_.push_back(img);
        image_indices_.push_back(idx);
        return idx;
    };
    rhi::TextureUpload up;
    up.pixels = h16.data();
    up.width = n;
    up.height = n;
    up.format = VK_FORMAT_R16_UNORM;
    up.generate_mips = false;
    up.name = "terrain_height";
    gpu_.heightmap = add(rhi::upload_texture(dev_, up));
    up.pixels = mask_.data();
    up.width = n / 2;
    up.height = n / 2;
    up.format = VK_FORMAT_R8G8B8A8_UNORM;
    up.generate_mips = true;
    up.name = "terrain_mask";
    gpu_.mask = add(rhi::upload_texture(dev_, up));

    if (!shore_.empty()) {
        std::vector<u16> s16(shore_.size());
        for (usize i = 0; i < shore_.size(); ++i)
            s16[i] = static_cast<u16>(std::lround(saturate(shore_[i] / kShoreRange + 0.5f) * 65535.0f));
        up.pixels = s16.data();
        up.width = desc_.resolution / 2;
        up.height = desc_.resolution / 2;
        up.format = VK_FORMAT_R16_UNORM;
        up.generate_mips = false;
        up.name = "terrain_shore";
        gpu_.shore = add(rhi::upload_texture(dev_, up));
    }
    // Layer order (terrain.frag): 0 = default ground, 1 = patches, 2 = rock (triplanar), 3 = wet ground.
    const bool coast = desc_.kind == TerrainKind::Coast;
    const char* valley_layers[4] = {"forest_leaves_02", "brown_mud_leaves_01", "aerial_rocks_02", "brown_mud_02"};
    const char* coast_layers[4] = {"coast_sand_01", "aerial_grass_rock", "coast_land_rocks_01", "damp_beach_sand"};
    const char* const* layers = coast ? coast_layers : valley_layers;
    const u8 valley_col[4][3] = {{78, 58, 38}, {70, 60, 45}, {95, 98, 88}, {60, 48, 38}};
    const u8 coast_col[4][3] = {{196, 180, 150}, {170, 158, 135}, {110, 104, 96}, {140, 125, 100}};
    const u8(*fallback_col)[3] = coast ? coast_col : valley_col;
    gpu_.flags = coast ? 2u : 0u;
    for (int l = 0; l < 4; ++l) {
        const std::string base = path_join(path_join(desc_.texture_dir, layers[l]), layers[l]);
        const char* suffix[3] = {"_diff.jpg", "_nor_gl.jpg", "_arm.jpg"};
        u32* targets[3] = {&gpu_.layer_albedo[l], &gpu_.layer_normal[l], &gpu_.layer_arm[l]};
        for (int k = 0; k < 3; ++k) {
            ImageRGBA8 img;
            const bool ok = !desc_.texture_dir.empty() && image_read((base + suffix[k]).c_str(), img);
            if (!ok) {
                img.width = img.height = 4;
                img.pixels.assign(64, 255);
                for (int p = 0; p < 16; ++p) {
                    u8* px = &img.pixels[usize(p) * 4];
                    if (k == 0) px[0] = fallback_col[l][0], px[1] = fallback_col[l][1], px[2] = fallback_col[l][2];
                    if (k == 1) px[0] = 128, px[1] = 128, px[2] = 255;
                    if (k == 2) px[0] = 255, px[1] = 230, px[2] = 0;
                }
            }
            // Height (Poly Haven Displacement, white = high) goes into the albedo alpha, which is otherwise unused.
            // Flat (1.0) when the map is missing: the layer then has no relief and no height-blend bias.
            if (k == 0) {
                ImageRGBA8 disp;
                if (ok && image_read((base + "_disp.jpg").c_str(), disp) && disp.width == img.width &&
                    disp.height == img.height) {
                    for (usize p = 0; p < usize(img.width) * img.height; ++p)
                        img.pixels[p * 4 + 3] = disp.pixels[p * 4];
                } else {
                    for (usize p = 0; p < usize(img.width) * img.height; ++p) img.pixels[p * 4 + 3] = 255;
                }
            }
            rhi::TextureUpload tu;
            tu.pixels = img.pixels.data();
            tu.width = img.width;
            tu.height = img.height;
            tu.format = k == 0 ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
            tu.name = layers[l];
            *targets[k] = add(rhi::upload_texture(dev_, tu));
        }
    }
    const f32 size = world_size();
    gpu_.extent = {-size * 0.5f, -size * 0.5f, size, 1.0f / size};
    gpu_.height = {min_h_, range, desc_.texel_size, f32(n)};
    heap_.flush_all_idle();
}

void Terrain::build_grid() {
    // (N+1)^2 grid vertices + a skirt ring; vertex = (u, v, skirt, 0).
    const u32 q = kPatchQuads, v = q + 1;
    std::vector<vec4> verts;
    std::vector<u32> idx;
    for (u32 y = 0; y < v; ++y)
        for (u32 x = 0; x < v; ++x) verts.push_back({f32(x) / f32(q), f32(y) / f32(q), 0.0f, 0.0f});
    for (u32 y = 0; y < q; ++y)
        for (u32 x = 0; x < q; ++x) {
            const u32 a = y * v + x, b = a + 1, c = a + v, d = c + 1;
            idx.insert(idx.end(), {a, c, b, b, c, d});
        }
    // Skirts along the 4 edges: duplicate edge vertices with skirt=1 and stitch.
    auto edge = [&](auto vertex_at) {
        const u32 base = static_cast<u32>(verts.size());
        for (u32 i = 0; i < v; ++i) {
            vec4 p = verts[vertex_at(i)];
            p.z = 1.0f;
            verts.push_back(p);
        }
        for (u32 i = 0; i < q; ++i) {
            const u32 a = vertex_at(i), b = vertex_at(i + 1), c = base + i, d = base + i + 1;
            idx.insert(idx.end(), {a, b, c, b, d, c});
        }
    };
    edge([&](u32 i) { return i; });               // top    (y = 0)
    edge([&](u32 i) { return (v - 1) * v + i; }); // bottom (y = q)
    edge([&](u32 i) { return i * v; });           // left   (x = 0)
    edge([&](u32 i) { return i * v + (v - 1); }); // right  (x = q)
    grid_vb_ = rhi::upload_buffer(dev_, verts.data(), verts.size() * sizeof(vec4), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                  "terrain_grid_vb");
    grid_ib_ = rhi::upload_buffer(dev_, idx.data(), idx.size() * sizeof(u32), VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                  "terrain_grid_ib");
    grid_index_count_ = static_cast<u32>(idx.size());
}

void Terrain::build_patches(dvec3 cam, std::vector<TerrainPatchGPU>& out) const {
    out.clear();
    const f32 base_patch = f32(kPatchQuads) * desc_.texel_size * 0.5f; // level 0: 0.5 m quads near the camera
    const f32 half_world = world_size() * 0.5f;
    f64 prev_lo_x = 0, prev_lo_z = 0, prev_hi_x = 0, prev_hi_z = 0;
    for (u32 level = 0; level < kLevels; ++level) {
        const f32 ps = base_patch * f32(1u << level);
        // Ring origin snapped to twice the patch size: the finer ring's bounds then fall on this level's grid.
        const f64 snap = f64(ps) * 2.0;
        const f64 cx = std::floor(cam.x / snap) * snap, cz = std::floor(cam.z / snap) * snap;
        for (i32 j = -4; j < 4; ++j)
            for (i32 i = -4; i < 4; ++i) {
                const f64 ox = cx + f64(i) * ps, oz = cz + f64(j) * ps;
                // Skip patches already covered by the finer ring.
                if (level > 0 && ox >= prev_lo_x - 1e-3 && ox + ps <= prev_hi_x + 1e-3 && oz >= prev_lo_z - 1e-3 &&
                    oz + ps <= prev_hi_z + 1e-3)
                    continue;
                if (ox > half_world || oz > half_world || ox + ps < -half_world || oz + ps < -half_world) continue;
                out.push_back({{static_cast<f32>(ox - cam.x), static_cast<f32>(oz - cam.z), ps, f32(level)}});
            }
        prev_lo_x = cx - 4.0 * ps;
        prev_hi_x = cx + 4.0 * ps;
        prev_lo_z = cz - 4.0 * ps;
        prev_hi_z = cz + 4.0 * ps;
    }
}

} // namespace tefra::render
