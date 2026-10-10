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

void Terrain::generate() {
    const u32 n = desc_.resolution;
    const f32 half = world_size() * 0.5f;
    heights_.resize(usize(n) * n);
    mask_.resize(usize(n / 2) * (n / 2) * 4);
    const u32 seed = desc_.seed;
    auto height_fn = [&](f32 x, f32 z) {
        // Domain warp for organic shapes.
        const f32 wx = x + 60.0f * fbm(x / 700.0f, z / 700.0f, seed + 7, 3);
        const f32 wz = z + 60.0f * fbm(x / 700.0f + 5.2f, z / 700.0f + 1.3f, seed + 9, 3);
        const f32 d = std::fabs(wx - river_center_x(wz));
        // Valley walls: flat floor (~60 m wide), steepening slopes, crest ridges near the map edge.
        const f32 wall = std::pow(max(d - 30.0f, 0.0f) / 900.0f, 1.55f) * 260.0f;
        const f32 ridges = ridged(wx / 520.0f, wz / 520.0f, seed + 3, 5) * 70.0f * smoothstep(60.0f, 400.0f, d);
        const f32 hills = fbm(wx / 230.0f, wz / 230.0f, seed + 11, 5) * 14.0f * smoothstep(20.0f, 200.0f, d);
        const f32 floor_bumps = fbm(x / 35.0f, z / 35.0f, seed + 13, 4) * 1.6f;
        f32 h = 20.0f + wall + ridges + hills + floor_bumps;
        // River channel and banks.
        const f32 channel = 1.0f - smoothstep(3.0f, 9.0f, d);
        h -= channel * 2.6f;
        // Lake basin downstream.
        const f32 lx = x - river_center_x(520.0f), lz = (z - 520.0f) * 0.7f;
        const f32 lake_d = std::sqrt(lx * lx + lz * lz);
        const f32 lake = 1.0f - smoothstep(60.0f, 160.0f, lake_d);
        h = lerp(h, 15.5f + floor_bumps * 0.3f, lake);
        return h;
    };
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
            const f32 d = std::fabs(river_distance(wx, wz));
            const f32 h = height_at(wx, wz);
            const f32 wet = max(1.0f - smoothstep(4.0f, 22.0f, d), 1.0f - smoothstep(15.6f, 17.5f, h));
            const f32 patch = 0.5f + 0.5f * fbm(wx / 9.0f, wz / 9.0f, seed + 21, 4);
            u8* p = &mask_[(usize(y) * m + x) * 4];
            p[0] = static_cast<u8>(saturate(wet) * 255.0f);
            p[1] = static_cast<u8>(saturate(patch) * 255.0f);
            p[2] = 0;
            p[3] = 255;
        }
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

    const char* layers[4] = {"forest_leaves_02", "brown_mud_leaves_01", "aerial_rocks_02", "brown_mud_02"};
    const u8 fallback_col[4][3] = {{78, 58, 38}, {70, 60, 45}, {95, 98, 88}, {60, 48, 38}};
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
