#pragma once

// Static scene for M1: meshes in one global vertex/index buffer, materials and textures in bindless storage,
// instances with double-precision transforms (camera-relative matrices are built every frame).

#include "tefra/renderer/mesh.h"
#include "tefra/rhi/resources.h"

#include <map>
#include <string>
#include <vector>

namespace tefra::rhi {
class Device;
class BindlessHeap;
} // namespace tefra::rhi

namespace tefra::render {

constexpr u32 kNoSkin = 0xFFFFFFFFu;

struct MeshRange {
    u32 first_index = 0;
    u32 index_count = 0;
    i32 vertex_offset = 0;
    u32 vertex_count = 0;
    Bounds bounds{};
    bool skinned = false; // has SkinAttr entries (skin_offset indexes Scene::skin_attributes())
    u32 skin_offset = 0;
};

struct Instance {
    dvec3 position{};
    quat rotation{};
    vec3 scale{1.0f};
    u32 mesh = 0;
    u32 material = 0;
    bool cast_shadows = true;
    u32 skin = kNoSkin; // index into Scene::skins(): the mesh is drawn from the skinned copy of its vertices
};

// A local light (PointLight / SpotLight): photometric intensity in candela, windowed inverse-square falloff to range.
struct LocalLight {
    dvec3 position{};
    vec3 direction{0.0f, 0.0f, -1.0f}; // spot axis (unit)
    vec3 color{1.0f, 1.0f, 1.0f};      // linear rgb
    f32 intensity = 800.0f;            // cd
    f32 range = 16.0f;                 // m
    f32 cos_outer = -2.0f;             // spot cone (cos of the half angle); < -1 = point light
    f32 cos_inner = -2.0f;
    f32 spill = 0.0f;     // fraction of the intensity outside the cone (fixture glass, reflections)
    bool shadows = false; // gets a shadow map when it is among the nearest shadowed lights
};

// A skeleton's palette (joint matrices in the mesh's own space: joint_global * inverse_bind). The renderer keeps the
// previous frame's palette too; set_skin_palette() rotates them.
struct SkinState {
    u32 joint_count = 0;
    std::vector<mat4> palette;
    std::vector<mat4> prev_palette;
    bool has_palette = false;
};

class Scene {
  public:
    Scene(const rhi::Device& dev, rhi::BindlessHeap& heap);
    ~Scene();
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    u32 add_mesh(const MeshData& mesh);
    u32 add_material(const MaterialGPU& material);
    // Takes ownership of an uploaded texture; returns its bindless index.
    u32 add_texture(rhi::Image&& image);
    // Textures shared between files (external glTF images referenced by several models): key -> bindless index.
    // find_texture returns ~0u when the key is unknown.
    u32 find_texture(const std::string& key) const {
        auto it = shared_textures_.find(key);
        return it == shared_textures_.end() ? ~0u : it->second;
    }
    void share_texture(const std::string& key, u32 index) { shared_textures_[key] = index; }
    u32 add_instance(const Instance& instance);
    // Adds a skin with joint_count joints; its palette starts as identity. Instances refer to it with Instance::skin.
    u32 add_skin(u32 joint_count);
    // Sets the current palette (size must equal the joint count). The previous current palette becomes the previous
    // one.
    void set_skin_palette(u32 skin, const std::vector<mat4>& palette);
    const std::vector<SkinState>& skins() const { return skins_; }
    const std::vector<SkinAttr>& skin_attributes() const { return skin_attrs_; }
    const rhi::Buffer& skin_buffer() const { return sbuf_; }

    // Uploads geometry and materials. Call once after adding content (and again after changes).
    void upload();

    const std::vector<MeshRange>& meshes() const { return meshes_; }
    const std::vector<Instance>& instances() const { return instances_; }
    std::vector<Instance>& instances() { return instances_; }
    // Local lights, rewritten by the owner whenever they change (no upload needed: the renderer copies them).
    const std::vector<LocalLight>& lights() const { return lights_; }
    std::vector<LocalLight>& lights() { return lights_; }
    const std::vector<MaterialGPU>& materials() const { return materials_; }
    const rhi::Buffer& vertex_buffer() const { return vbuf_; }
    const rhi::Buffer& index_buffer() const { return ibuf_; }
    const rhi::Buffer& material_buffer() const { return mbuf_; }
    const rhi::Buffer& skin_attribute_buffer() const { return sbuf_; }
    u32 error_texture() const { return error_tex_; }
    u32 flat_normal_texture() const { return flat_normal_tex_; }
    u32 white_texture() const { return white_tex_; }
    const rhi::Device& device() const { return dev_; }

  private:
    const rhi::Device& dev_;
    rhi::BindlessHeap& heap_;
    std::vector<Vertex> vertices_;
    std::vector<u32> indices_;
    std::vector<MeshRange> meshes_;
    std::vector<MaterialGPU> materials_;
    std::vector<Instance> instances_;
    std::vector<LocalLight> lights_;
    std::vector<SkinAttr> skin_attrs_;
    std::vector<SkinState> skins_;
    std::vector<rhi::Image> textures_;
    std::vector<u32> texture_indices_;
    std::map<std::string, u32> shared_textures_;
    rhi::Buffer vbuf_, ibuf_, mbuf_, sbuf_;
    u32 error_tex_ = 0, flat_normal_tex_ = 0, white_tex_ = 0;
};

} // namespace tefra::render
