#pragma once

// Static scene for M1: meshes in one global vertex/index buffer, materials and textures in bindless storage,
// instances with double-precision transforms (camera-relative matrices are built every frame).

#include "tefra/renderer/mesh.h"
#include "tefra/rhi/resources.h"

#include <string>
#include <vector>

namespace tefra::rhi {
class Device;
class BindlessHeap;
} // namespace tefra::rhi

namespace tefra::render {

struct MeshRange {
    u32 first_index = 0;
    u32 index_count = 0;
    i32 vertex_offset = 0;
    Bounds bounds{};
};

struct Instance {
    dvec3 position{};
    quat rotation{};
    vec3 scale{1.0f};
    u32 mesh = 0;
    u32 material = 0;
    bool cast_shadows = true;
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
    u32 add_instance(const Instance& instance);

    // Uploads geometry and materials. Call once after adding content (and again after changes).
    void upload();

    const std::vector<MeshRange>& meshes() const { return meshes_; }
    const std::vector<Instance>& instances() const { return instances_; }
    std::vector<Instance>& instances() { return instances_; }
    const std::vector<MaterialGPU>& materials() const { return materials_; }
    const rhi::Buffer& vertex_buffer() const { return vbuf_; }
    const rhi::Buffer& index_buffer() const { return ibuf_; }
    const rhi::Buffer& material_buffer() const { return mbuf_; }
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
    std::vector<rhi::Image> textures_;
    std::vector<u32> texture_indices_;
    rhi::Buffer vbuf_, ibuf_, mbuf_;
    u32 error_tex_ = 0, flat_normal_tex_ = 0, white_tex_ = 0;
};

} // namespace tefra::render
