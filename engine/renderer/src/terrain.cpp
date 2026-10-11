#include "tefra/renderer/terrain.h"

#include "tefra/core/fs.h"
#include "tefra/core/image.h"
#include "tefra/core/log.h"
#include "tefra/core/time.h"
#include "tefra/rhi/bindless.h"
#include "tefra/rhi/device.h"
#include "tefra/rhi/upload.h"

#include <cmath>

namespace tefra::render {

Terrain::Terrain(const rhi::Device& dev, rhi::BindlessHeap& heap, const TerrainDesc& desc)
    : Terrain(dev, heap, TerrainField::acquire(desc), desc.texture_dir) {}

Terrain::Terrain(const rhi::Device& dev, rhi::BindlessHeap& heap, std::shared_ptr<const TerrainField> field,
                 std::string texture_dir)
    : dev_(dev), heap_(heap), field_(std::move(field)) {
    TEFRA_VERIFY(field_ != nullptr);
    const u64 t0 = time_now_ns();
    upload_textures(texture_dir);
    build_grid();
    const u32 n = field_->resolution();
    TEFRA_LOG_INFO("Terrain %ux%u @ %.2f m: heights %.1f..%.1f m, generated in %.0f ms, uploaded in %.0f ms", n, n,
                   static_cast<f64>(field_->texel_size()), static_cast<f64>(field_->min_height()),
                   static_cast<f64>(field_->max_height()), field_->build_ms(), ns_to_ms(time_now_ns() - t0));
}

Terrain::~Terrain() {
    for (usize i = 0; i < images_.size(); ++i) {
        heap_.remove_texture(image_indices_[i]);
        rhi::destroy_image(dev_, images_[i]);
    }
    rhi::destroy_buffer(dev_, grid_vb_);
    rhi::destroy_buffer(dev_, grid_ib_);
}

void Terrain::upload_textures(const std::string& texture_dir) {
    const TerrainField& f = *field_;
    const u32 n = f.resolution();
    const std::vector<f32>& heights = f.heights();
    const f32 min_h = f.min_height();
    std::vector<u16> h16(heights.size());
    const f32 range = max(f.max_height() - min_h, 1e-3f);
    for (usize i = 0; i < heights.size(); ++i)
        h16[i] = static_cast<u16>(std::lround(saturate((heights[i] - min_h) / range) * 65535.0f));
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
    up.pixels = f.mask().data();
    up.width = n / 2;
    up.height = n / 2;
    up.format = VK_FORMAT_R8G8B8A8_UNORM;
    up.generate_mips = true;
    up.name = "terrain_mask";
    gpu_.mask = add(rhi::upload_texture(dev_, up));

    if (!f.shore().empty()) {
        const std::vector<f32>& shore = f.shore();
        std::vector<u16> s16(shore.size());
        for (usize i = 0; i < shore.size(); ++i)
            s16[i] = static_cast<u16>(std::lround(saturate(shore[i] / kShoreRange + 0.5f) * 65535.0f));
        up.pixels = s16.data();
        up.width = n / 2;
        up.height = n / 2;
        up.format = VK_FORMAT_R16_UNORM;
        up.generate_mips = false;
        up.name = "terrain_shore";
        gpu_.shore = add(rhi::upload_texture(dev_, up));
    }
    // Layer order (terrain.frag): 0 = default ground, 1 = patches, 2 = rock (triplanar), 3 = wet ground.
    const bool coast = f.kind() == TerrainKind::Coast;
    const char* valley_layers[4] = {"forest_leaves_02", "brown_mud_leaves_01", "aerial_rocks_02", "brown_mud_02"};
    const char* coast_layers[4] = {"coast_sand_01", "aerial_grass_rock", "coast_land_rocks_01", "damp_beach_sand"};
    const char* const* layers = coast ? coast_layers : valley_layers;
    const u8 valley_col[4][3] = {{78, 58, 38}, {70, 60, 45}, {95, 98, 88}, {60, 48, 38}};
    const u8 coast_col[4][3] = {{196, 180, 150}, {170, 158, 135}, {110, 104, 96}, {140, 125, 100}};
    const u8(*fallback_col)[3] = coast ? coast_col : valley_col;
    gpu_.flags = coast ? 2u : 0u;
    for (int l = 0; l < 4; ++l) {
        const std::string base = path_join(path_join(texture_dir, layers[l]), layers[l]);
        const char* suffix[3] = {"_diff.jpg", "_nor_gl.jpg", "_arm.jpg"};
        u32* targets[3] = {&gpu_.layer_albedo[l], &gpu_.layer_normal[l], &gpu_.layer_arm[l]};
        for (int k = 0; k < 3; ++k) {
            ImageRGBA8 img;
            const bool ok = !texture_dir.empty() && image_read((base + suffix[k]).c_str(), img);
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
    gpu_.height = {min_h, range, f.texel_size(), f32(n)};
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
    const f32 base_patch = f32(kPatchQuads) * field_->texel_size() * 0.5f; // level 0: 0.5 m quads near the camera
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
