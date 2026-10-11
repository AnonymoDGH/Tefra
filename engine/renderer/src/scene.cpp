#include "tefra/renderer/scene.h"

#include "tefra/rhi/bindless.h"
#include "tefra/rhi/device.h"
#include "tefra/rhi/upload.h"

namespace tefra::render {

namespace {
rhi::Image solid_texture(const rhi::Device& dev, u8 r, u8 g, u8 b, u8 a, VkFormat format, const char* name) {
    u8 pixels[4 * 4 * 4];
    for (int i = 0; i < 16; ++i) {
        pixels[i * 4 + 0] = r;
        pixels[i * 4 + 1] = g;
        pixels[i * 4 + 2] = b;
        pixels[i * 4 + 3] = a;
    }
    rhi::TextureUpload up;
    up.pixels = pixels;
    up.width = 4;
    up.height = 4;
    up.format = format;
    up.generate_mips = false;
    up.name = name;
    return rhi::upload_texture(dev, up);
}
} // namespace

Scene::Scene(const rhi::Device& dev, rhi::BindlessHeap& heap) : dev_(dev), heap_(heap) {
    // Index 0 must be the error texture (BindlessHeap::kErrorTexture): magenta/black 8x8 checker.
    u8 checker[8 * 8 * 4];
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) {
            const bool on = ((x / 2) + (y / 2)) % 2 == 0;
            u8* p = &checker[(y * 8 + x) * 4];
            p[0] = on ? 255 : 0;
            p[1] = 0;
            p[2] = on ? 255 : 0;
            p[3] = 255;
        }
    rhi::TextureUpload up;
    up.pixels = checker;
    up.width = 8;
    up.height = 8;
    up.format = VK_FORMAT_R8G8B8A8_SRGB;
    up.generate_mips = false;
    up.name = "error";
    error_tex_ = add_texture(rhi::upload_texture(dev_, up));
    TEFRA_VERIFY(error_tex_ == rhi::BindlessHeap::kErrorTexture);
    white_tex_ = add_texture(solid_texture(dev_, 255, 255, 255, 255, VK_FORMAT_R8G8B8A8_UNORM, "white"));
    flat_normal_tex_ = add_texture(solid_texture(dev_, 128, 128, 255, 255, VK_FORMAT_R8G8B8A8_UNORM, "flat_normal"));
}

Scene::~Scene() {
    rhi::destroy_buffer(dev_, vbuf_);
    rhi::destroy_buffer(dev_, ibuf_);
    rhi::destroy_buffer(dev_, mbuf_);
    rhi::destroy_buffer(dev_, sbuf_);
    for (usize i = 0; i < textures_.size(); ++i) {
        heap_.remove_texture(texture_indices_[i]);
        rhi::destroy_image(dev_, textures_[i]);
    }
}

u32 Scene::add_mesh(const MeshData& mesh) {
    MeshRange r;
    r.first_index = static_cast<u32>(indices_.size());
    r.index_count = static_cast<u32>(mesh.indices.size());
    r.vertex_offset = static_cast<i32>(vertices_.size());
    r.vertex_count = static_cast<u32>(mesh.vertices.size());
    r.bounds = compute_bounds(mesh);
    vertices_.insert(vertices_.end(), mesh.vertices.begin(), mesh.vertices.end());
    indices_.insert(indices_.end(), mesh.indices.begin(), mesh.indices.end());
    if (!mesh.skin.empty()) {
        TEFRA_VERIFY(mesh.skin.size() == mesh.vertices.size());
        r.skinned = true;
        r.skin_offset = static_cast<u32>(skin_attrs_.size());
        skin_attrs_.insert(skin_attrs_.end(), mesh.skin.begin(), mesh.skin.end());
    }
    meshes_.push_back(r);
    return static_cast<u32>(meshes_.size() - 1);
}

u32 Scene::add_skin(u32 joint_count) {
    SkinState s;
    s.joint_count = joint_count;
    s.palette.assign(joint_count, mat4{});
    s.prev_palette = s.palette;
    skins_.push_back(std::move(s));
    return static_cast<u32>(skins_.size() - 1);
}

void Scene::set_skin_palette(u32 skin, const std::vector<mat4>& palette) {
    TEFRA_VERIFY(skin < skins_.size());
    SkinState& s = skins_[skin];
    TEFRA_VERIFY(palette.size() == s.joint_count);
    s.prev_palette = s.has_palette ? s.palette : palette;
    s.palette = palette;
    s.has_palette = true;
}

u32 Scene::add_material(const MaterialGPU& material) {
    materials_.push_back(material);
    return static_cast<u32>(materials_.size() - 1);
}

u32 Scene::add_texture(rhi::Image&& image) {
    const u32 index = heap_.add_texture(image.view);
    textures_.push_back(image);
    texture_indices_.push_back(index);
    image = {};
    return index;
}

u32 Scene::add_instance(const Instance& instance) {
    instances_.push_back(instance);
    return static_cast<u32>(instances_.size() - 1);
}

void Scene::upload() {
    dev_.wait_idle();
    rhi::destroy_buffer(dev_, vbuf_);
    rhi::destroy_buffer(dev_, ibuf_);
    rhi::destroy_buffer(dev_, mbuf_);
    rhi::destroy_buffer(dev_, sbuf_);
    if (!vertices_.empty())
        vbuf_ = rhi::upload_buffer(dev_, vertices_.data(), vertices_.size() * sizeof(Vertex),
                                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "vertices");
    if (!indices_.empty())
        ibuf_ = rhi::upload_buffer(dev_, indices_.data(), indices_.size() * sizeof(u32),
                                   VK_BUFFER_USAGE_INDEX_BUFFER_BIT, "indices");
    if (!skin_attrs_.empty())
        sbuf_ = rhi::upload_buffer(dev_, skin_attrs_.data(), skin_attrs_.size() * sizeof(SkinAttr),
                                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "skin_attributes");
    if (materials_.empty()) add_material(MaterialGPU{});
    mbuf_ = rhi::upload_buffer(dev_, materials_.data(), materials_.size() * sizeof(MaterialGPU),
                               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "materials");
    heap_.flush_all_idle();
}

} // namespace tefra::render
