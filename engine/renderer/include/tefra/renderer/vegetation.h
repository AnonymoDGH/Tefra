#pragma once

// GPU-driven vegetation (spec 2 §7). Static world-space instances; a compute pass culls them per frame
// (frustum + distance + LOD selection) and writes indirect draw commands. Prototypes have up to 2 LODs with up
// to 2 submeshes each (e.g. bark + needles).

#include "tefra/renderer/mesh.h"
#include "tefra/rhi/resources.h"

#include <vector>

namespace tefra::rhi {
class Device;
class BindlessHeap;
} // namespace tefra::rhi

namespace tefra::render {

class Scene;
class Terrain;

struct VegetationLod {
    u32 mesh[2] = {~0u, ~0u}; // scene mesh indices (second optional)
    u32 material[2] = {0, 0};
    f32 max_distance = 100.0f; // this LOD is used up to this distance (m)
};

struct VegetationPrototype {
    VegetationLod lods[2];
    u32 lod_count = 1;
    f32 radius = 1.0f; // bounding sphere radius at scale 1 (m), centered at radius_center_y
    f32 center_y = 0.0f;
    f32 wind = 0.0f; // bending strength (0 = rigid)
    bool cast_shadows = true;
    f32 shadow_distance = 150.0f;
};

struct VegInstanceGPU {
    vec4 pos_scale; // world xyz, w = uniform scale
    vec4 rot_proto; // x = sin(yaw), y = cos(yaw), z = prototype (as float), w = random [0,1)
};
static_assert(sizeof(VegInstanceGPU) == 32);

class Vegetation {
  public:
    static constexpr u32 kMaxDraws = 64; // prototype x lod x submesh slots

    Vegetation(const rhi::Device& dev, const Scene& scene);
    ~Vegetation();
    Vegetation(const Vegetation&) = delete;
    Vegetation& operator=(const Vegetation&) = delete;

    u32 add_prototype(const VegetationPrototype& p);
    void add_instance(u32 prototype, vec3 world_pos, f32 yaw, f32 scale, f32 random);
    // Uploads instances (grouped by prototype) and draw tables. Call once after adding.
    void upload();

    u32 instance_count() const { return static_cast<u32>(cpu_.size()); }
    u32 draw_count() const { return static_cast<u32>(draws_.size()); }
    f32 prototype_wind(u32 p) const { return protos_cpu_[p].wind; }

    // GPU tables used by the renderer.
    struct DrawGPU { // one per (prototype, lod, submesh)
        u32 first_index, index_count;
        i32 vertex_offset;
        u32 material;
        u32 visible_base; // offset into the visible-index list (region size = prototype instance count)
        u32 prototype, lod, submesh;
    };
    struct ProtoGPU {
        vec4 bounds;    // xyz unused, w radius ; y = center_y
        vec4 lod_dist;  // x lod0 max, y lod1 max, z shadow distance, w lod count
        u32 draw[2][2]; // draw indices per lod/submesh (~0u = none)
        f32 wind;
        u32 instance_base, instance_count;
        u32 casts_shadows;
    };
    const rhi::Buffer& instances() const { return instances_; }
    const rhi::Buffer& protos() const { return protos_; }
    const rhi::Buffer& draws_table() const { return draws_buf_; }
    const std::vector<DrawGPU>& draws() const { return draws_; }
    u32 visible_capacity() const { return visible_capacity_; }

  private:
    const rhi::Device& dev_;
    const Scene& scene_;
    std::vector<VegetationPrototype> protos_cpu_;
    std::vector<VegInstanceGPU> cpu_;
    std::vector<DrawGPU> draws_;
    rhi::Buffer instances_, protos_, draws_buf_;
    u32 visible_capacity_ = 0;
};

// Procedural assets (clean-room): conifer meshes and the baked needle atlas for their foliage cards.
// LOD0 crown: drooping limbs of small two-sided cards. LOD1: skirts. Dead lower branches use the dead twig tile.
struct ConiferMeshes {
    MeshData trunk_lod0, crown_lod0, trunk_lod1, crown_lod1;
    f32 height = 0.0f, crown_radius = 0.0f;
};
ConiferMeshes make_conifer(u32 seed, f32 height, f32 trunk_radius);
// Baked needle atlas, width x height = 1024 x 512: 2x2 tiles of 512x256 (tile = row * 2 + column). Tiles 0-2 are
// living twigs, tile 3 a dead one. Albedo RGBA8 sRGB (alpha = coverage, alpha-test it with a cutoff of ~0.4).
// Normal RGBA8: tangent-space normal in RGB (engine convention), depth AO in A (material flag 4).
void make_needle_textures(u32 seed, u32 width, u32 height, std::vector<u8>& albedo, std::vector<u8>& normal);

// Rule-based scatter of the valley forest and undergrowth (deterministic).
struct ScatterStats {
    u32 trees = 0, undergrowth = 0;
};

} // namespace tefra::render
