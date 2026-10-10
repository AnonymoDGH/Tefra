#include "tefra/renderer/water.h"
#include "tefra/renderer/terrain.h"

#include "tefra/core/log.h"
#include "tefra/rhi/upload.h"

#include <cmath>
#include <vector>

namespace tefra::render {

namespace {
constexpr f32 kLakeZ = 520.0f;
constexpr f32 kLakeLevel = 16.6f;
} // namespace

f32 Water::level_at(f32 x, f32 z) const {
    (void)x;
    // River surface: 1.3 m above the channel bottom at the centerline, smoothed along the flow.
    f32 acc = 0.0f;
    int n = 0;
    for (f32 dz = -12.0f; dz <= 12.0f; dz += 4.0f) {
        const f32 zz = z + dz;
        const f32 cx = x - terrain_.river_distance(x, zz);
        acc += terrain_.height_at(cx, zz);
        ++n;
    }
    return max(acc / f32(n) + 1.3f, kLakeLevel - 0.05f * max(0.0f, std::fabs(z - kLakeZ) - 150.0f));
}

Water::Water(const rhi::Device& dev, const Terrain& terrain) : dev_(dev), terrain_(terrain) {
    std::vector<vec4> v;
    std::vector<u32> idx;
    const f32 half = terrain.world_size() * 0.5f;
    // River ribbon.
    const f32 step = 3.0f, width = 16.0f;
    const u32 across = 6;
    u32 rows = 0;
    for (f32 z = -half; z <= half; z += step, ++rows) {
        const f32 cx = -terrain.river_distance(0.0f, z); // river_distance(0,z) = -center_x
        const f32 level = level_at(cx, z);
        for (u32 i = 0; i <= across; ++i) {
            const f32 t = f32(i) / f32(across) * 2.0f - 1.0f;
            v.push_back({cx + t * width, level, z, 0.6f * (1.0f - t * t)});
        }
    }
    for (u32 r = 0; r + 1 < rows; ++r)
        for (u32 i = 0; i < across; ++i) {
            const u32 a = r * (across + 1) + i, b = a + 1, c = a + across + 1, d = c + 1;
            idx.insert(idx.end(), {a, c, b, b, c, d});
        }
    // Lake disc.
    const u32 base = static_cast<u32>(v.size());
    const f32 lx = -terrain.river_distance(0.0f, kLakeZ);
    v.push_back({lx, kLakeLevel, kLakeZ, 0.0f});
    const u32 seg = 64;
    for (u32 i = 0; i <= seg; ++i) {
        const f32 a = f32(i) / f32(seg) * kTwoPi;
        v.push_back({lx + std::cos(a) * 190.0f, kLakeLevel, kLakeZ + std::sin(a) * 260.0f, 0.0f});
    }
    for (u32 i = 0; i < seg; ++i) idx.insert(idx.end(), {base, base + 2 + i, base + 1 + i});
    vb_ = rhi::upload_buffer(dev_, v.data(), v.size() * sizeof(vec4), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "water_vb");
    ib_ = rhi::upload_buffer(dev_, idx.data(), idx.size() * sizeof(u32), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, "water_ib");
    index_count_ = static_cast<u32>(idx.size());
}

Water::~Water() {
    rhi::destroy_buffer(dev_, vb_);
    rhi::destroy_buffer(dev_, ib_);
}

} // namespace tefra::render
