#pragma once

// Procedural valley terrain (spec 2 §3): CPU-generated heightmap (TerrainField, uploaded as R16_UNORM), clipmap-style
// instanced patches with skirts, layered materials blended by slope, wetness and noise in the G-buffer shader.

#include "tefra/core/math.h"
#include "tefra/renderer/terrain_field.h"
#include "tefra/rhi/resources.h"

#include <memory>
#include <string>
#include <vector>

namespace tefra::rhi {
class Device;
class BindlessHeap;
} // namespace tefra::rhi

namespace tefra::render {

class Scene;

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
    u32 flags = 0; // bit 0: detail path (set per frame from RenderSettings::terrain_detail), bit 1: coast look
    u32 shore = 0; // Coast: R16 signed distance to the shoreline (m = (v - 0.5) * kShoreRange, > 0 at sea)
};

class Terrain {
  public:
    static constexpr u32 kPatchQuads = 32;
    static constexpr u32 kLevels = 6;
    static constexpr f32 kShoreRange = 2048.0f; // metres covered by the shore distance texture (+-1024 m)

    // Generates (or takes from TerrainField's cache) the field for desc and uploads it.
    Terrain(const rhi::Device& dev, rhi::BindlessHeap& heap, const TerrainDesc& desc);
    // Uploads an already generated field; texture_dir: layer textures (empty = flat colored layers).
    Terrain(const rhi::Device& dev, rhi::BindlessHeap& heap, std::shared_ptr<const TerrainField> field,
            std::string texture_dir);
    ~Terrain();
    Terrain(const Terrain&) = delete;
    Terrain& operator=(const Terrain&) = delete;

    // World height / normal (bilinear), for gameplay and placement. Outside the map: clamped to the edge.
    f32 height_at(f32 x, f32 z) const { return field_->height_at(x, z); }
    vec3 normal_at(f32 x, f32 z) const { return field_->normal_at(x, z); }
    // Signed distance (m) to the river centerline.
    f32 river_distance(f32 x, f32 z) const { return field_->river_distance(x, z); }
    f32 world_size() const { return field_->world_size(); }
    TerrainKind kind() const { return field_->kind(); }
    f32 sea_level() const { return field_->sea_level(); }
    // Coast: signed distance (m) to the shoreline, > 0 at sea (bilinear in the half-resolution field).
    f32 shore_distance(f32 x, f32 z) const { return field_->shore_distance(x, z); }
    const TerrainField& field() const { return *field_; }
    const std::shared_ptr<const TerrainField>& field_ptr() const { return field_; }

    // Patch instances around the camera for this frame (clipmap rings, frustum-culled by the caller).
    void build_patches(dvec3 camera, std::vector<TerrainPatchGPU>& out) const;

    const TerrainGPU& gpu() const { return gpu_; }
    const rhi::Buffer& grid_vertices() const { return grid_vb_; }
    const rhi::Buffer& grid_indices() const { return grid_ib_; }
    u32 grid_index_count() const { return grid_index_count_; }
    f32 min_height() const { return field_->min_height(); }
    f32 max_height() const { return field_->max_height(); }

  private:
    void upload_textures(const std::string& texture_dir);
    void build_grid();

    const rhi::Device& dev_;
    rhi::BindlessHeap& heap_;
    std::shared_ptr<const TerrainField> field_;
    TerrainGPU gpu_{};
    std::vector<rhi::Image> images_;
    std::vector<u32> image_indices_;
    rhi::Buffer grid_vb_, grid_ib_;
    u32 grid_index_count_ = 0;
};

} // namespace tefra::render
