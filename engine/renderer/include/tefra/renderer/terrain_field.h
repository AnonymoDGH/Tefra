#pragma once

// CPU side of the procedural terrain (spec 2 §3, M3b landscape): the valley / coast heightfield, the wetness and
// cover mask and the coast's shore distance field. No GPU here: the GPU Terrain (terrain.h), the vegetation scatter,
// the physics heightfield and editor templates all read the same samples, so they agree to the millimetre.
//
// Generation takes ~1-4 s for 2048^2 samples on 4 threads. TerrainField::acquire() shares one generation between
// Tefra Studio, the game runtime and templates: the last fields are kept in a small process-wide cache.

#include "tefra/core/math.h"

#include <memory>
#include <string>
#include <vector>

namespace tefra::render {

enum class TerrainKind : u32 {
    Valley, // forest valley with a river and a lake (spec 2)
    Coast,  // sandy cove between two rocky headlands; the sea (y < sea_level) lies towards -Z
};

struct TerrainDesc {
    u32 resolution = 2048; // heightmap texels per side
    f32 texel_size = 1.0f; // meters per texel (2048 x 1 m = 2 x 2 km)
    u32 seed = 1337;
    std::string texture_dir; // GPU Terrain only: assets/cache/textures (empty = flat colored layers)
    TerrainKind kind = TerrainKind::Valley;
    f32 sea_level = 0.0f; // Coast: world y of the still sea surface
};

class TerrainField {
  public:
    // Generates the field now (blocking, parallel over rows).
    explicit TerrainField(const TerrainDesc& desc);
    TerrainField(const TerrainField&) = delete;
    TerrainField& operator=(const TerrainField&) = delete;

    // Shared field for desc, generated on first use and cached (the last kCacheSize fields stay alive). A coast that
    // only differs in sea level is derived from a cached one by shifting its heights (the shape is relative to the
    // sea). Thread-safe; concurrent requests for the same field may generate it twice, never inconsistently.
    static constexpr u32 kCacheSize = 2;
    static std::shared_ptr<const TerrainField> acquire(const TerrainDesc& desc);
    // Cached field for desc, or null (never generates).
    static std::shared_ptr<const TerrainField> find_cached(const TerrainDesc& desc);
    static void clear_cache();
    // True when a and b generate the same heights (texture_dir and, for the valley, sea_level are ignored).
    static bool same_field(const TerrainDesc& a, const TerrainDesc& b);

    const TerrainDesc& desc() const { return desc_; }
    u32 resolution() const { return desc_.resolution; }
    f32 texel_size() const { return desc_.texel_size; }
    f32 world_size() const { return f32(desc_.resolution) * desc_.texel_size; }
    TerrainKind kind() const { return desc_.kind; }
    f32 sea_level() const { return desc_.sea_level; }

    // World height / normal (bilinear between samples). Outside the map: clamped to the edge.
    f32 height_at(f32 x, f32 z) const;
    vec3 normal_at(f32 x, f32 z) const;
    // Valley: signed distance (m) to the river centerline (x - centre).
    f32 river_distance(f32 x, f32 z) const;
    // Coast: signed distance (m) to the shoreline, > 0 at sea (bilinear in the half-resolution field). 1e4 elsewhere.
    f32 shore_distance(f32 x, f32 z) const;
    // Closest hit of the ray (dir need not be unit) with the surface within max_distance (m): t = distance (m).
    bool raycast(vec3 origin, vec3 dir, f32 max_distance, f32& t) const;

    // Raw samples: heights()[z * n + x] is the height at sample_origin() + (x, z) * texel_size.
    const std::vector<f32>& heights() const { return heights_; }
    vec2 sample_origin() const;
    // Half resolution RGBA8: r = wetness, g = patchiness, b = vegetation cover, a = 255.
    const std::vector<u8>& mask() const { return mask_; }
    // Half resolution signed metres (> 0 at sea); empty for the valley.
    const std::vector<f32>& shore() const { return shore_; }
    f32 min_height() const { return min_h_; }
    f32 max_height() const { return max_h_; }
    // Milliseconds the generation took (0 for a field derived from a cached one).
    f64 build_ms() const { return build_ms_; }

  private:
    struct Shifted {};
    TerrainField(const TerrainField& src, f32 sea_level, Shifted);
    void generate();
    f32 valley_height(f32 x, f32 z) const;
    f32 coast_height(f32 x, f32 z) const;
    f32 coast_shore_z(f32 x) const;
    f32 coast_shore_z_dist(f32 x, f32 z) const; // > 0 inland (unwarped, approximate)
    void build_shore_distance();
    f32 river_center_x(f32 z) const;

    TerrainDesc desc_;
    std::vector<f32> heights_;
    std::vector<u8> mask_;
    std::vector<f32> shore_;
    f32 min_h_ = 0, max_h_ = 0;
    f64 build_ms_ = 0.0;
};

} // namespace tefra::render
