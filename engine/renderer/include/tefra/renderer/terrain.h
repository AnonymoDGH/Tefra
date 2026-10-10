#pragma once

// Procedural valley terrain (spec 2 §3): CPU-generated heightmap (R16_UNORM), clipmap-style instanced patches
// with skirts, layered materials blended by slope, wetness and noise in the G-buffer shader.

#include "tefra/core/math.h"
#include "tefra/rhi/resources.h"

#include <string>
#include <vector>

namespace tefra::rhi {
class Device;
class BindlessHeap;
} // namespace tefra::rhi

namespace tefra::render {

class Scene;

struct TerrainDesc {
    u32 resolution = 2048; // heightmap texels per side
    f32 texel_size = 1.0f; // meters per texel (2048 x 1 m = 2 x 2 km)
    u32 seed = 1337;
    std::string texture_dir; // assets/cache/textures (empty = flat colored layers)
};

// GPU per-patch instance.
struct TerrainPatchGPU {
    vec4 origin_size; // xy = world xz origin relative to camera, z = size (m), w = LOD level
};
static_assert(sizeof(TerrainPatchGPU) == 16);

struct TerrainGPU { // in FrameConstants-independent push data
    vec4 extent;    // x = world min x, y = world min z, z = world size, w = 1 / world size
    vec4 height;    // x = min height, y = height range, z = texel size, w = resolution
    u32 heightmap = 0;
    u32 mask = 0;
    u32 layer_albedo[4] = {};
    u32 layer_normal[4] = {};
    u32 layer_arm[4] = {};
    u32 flags = 0; // bit 0: detail path (set per frame from RenderSettings::terrain_detail)
    u32 pad = 0;
};

class Terrain {
  public:
    static constexpr u32 kPatchQuads = 32;
    static constexpr u32 kLevels = 6;

    Terrain(const rhi::Device& dev, rhi::BindlessHeap& heap, const TerrainDesc& desc);
    ~Terrain();
    Terrain(const Terrain&) = delete;
    Terrain& operator=(const Terrain&) = delete;

    // World height / normal (bilinear), for gameplay and placement. Outside the map: clamped to the edge.
    f32 height_at(f32 x, f32 z) const;
    vec3 normal_at(f32 x, f32 z) const;
    // Signed distance (m) to the river centerline, and the river water level at that point.
    f32 river_distance(f32 x, f32 z) const;
    f32 world_size() const { return f32(desc_.resolution) * desc_.texel_size; }

    // Patch instances around the camera for this frame (clipmap rings, frustum-culled by the caller).
    void build_patches(dvec3 camera, std::vector<TerrainPatchGPU>& out) const;

    const TerrainGPU& gpu() const { return gpu_; }
    const rhi::Buffer& grid_vertices() const { return grid_vb_; }
    const rhi::Buffer& grid_indices() const { return grid_ib_; }
    u32 grid_index_count() const { return grid_index_count_; }
    f32 min_height() const { return min_h_; }
    f32 max_height() const { return max_h_; }

  private:
    void generate();
    void upload_textures();
    void build_grid();
    f32 river_center_x(f32 z) const;

    const rhi::Device& dev_;
    rhi::BindlessHeap& heap_;
    TerrainDesc desc_;
    std::vector<f32> heights_;
    std::vector<u8> mask_;
    f32 min_h_ = 0, max_h_ = 0;
    TerrainGPU gpu_{};
    std::vector<rhi::Image> images_;
    std::vector<u32> image_indices_;
    rhi::Buffer grid_vb_, grid_ib_;
    u32 grid_index_count_ = 0;
};

} // namespace tefra::render
