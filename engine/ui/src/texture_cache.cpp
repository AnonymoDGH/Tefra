#include "tefra/ui/texture_cache.h"

#include "tefra/core/fs.h"
#include "tefra/core/image.h"
#include "tefra/rhi/device.h"
#include "tefra/rhi/resources.h"
#include "tefra/rhi/upload.h"

#include <volk.h>

#include <imgui.h>
#include <imgui_impl_vulkan.h>

#include <unordered_map>

namespace tefra::ui {

struct TextureCache::Impl {
    struct Entry {
        rhi::Image image{};
        VkDescriptorSet set = VK_NULL_HANDLE;
        vec2 size{};
        i64 stamp = 0;
        bool failed = false;
    };
    const rhi::Device& device;
    std::unordered_map<std::string, Entry> entries;

    explicit Impl(const rhi::Device& d) : device(d) {}

    // ImGui_ImplVulkan_RemoveTexture needs the ImGui context; the GPU must be idle before the image is destroyed.
    void release(Entry& e) {
        if (e.set != VK_NULL_HANDLE && ImGui::GetCurrentContext()) ImGui_ImplVulkan_RemoveTexture(e.set);
        e.set = VK_NULL_HANDLE;
        if (e.image.image) rhi::destroy_image(device, e.image);
    }
    void release_all() {
        for (auto& kv : entries) release(kv.second);
        entries.clear();
    }
};

TextureCache::TextureCache(const rhi::Device& device) : impl_(std::make_unique<Impl>(device)) {}

TextureCache::~TextureCache() {
    if (!impl_) return;
    vkDeviceWaitIdle(impl_->device.device());
    impl_->release_all();
}

void* TextureCache::get(const std::string& path, vec2* size) {
    if (path.empty()) return nullptr;
    const i64 stamp = file_mtime(path);
    auto it = impl_->entries.find(path);
    if (it != impl_->entries.end() && it->second.stamp == stamp) {
        if (size) *size = it->second.size;
        return it->second.failed ? nullptr : reinterpret_cast<void*>(it->second.set);
    }
    if (it != impl_->entries.end()) { // the file changed: free the old texture (rare; wait for the GPU)
        vkDeviceWaitIdle(impl_->device.device());
        impl_->release(it->second);
        impl_->entries.erase(it);
    }
    Impl::Entry e;
    e.stamp = stamp;
    ImageRGBA8 img;
    if (stamp == 0 || !image_read(path.c_str(), img) || img.width == 0 || img.height == 0) {
        e.failed = true;
        impl_->entries.emplace(path, e);
        return nullptr;
    }
    rhi::TextureUpload up;
    up.pixels = img.pixels.data();
    up.width = img.width;
    up.height = img.height;
    up.format = VK_FORMAT_R8G8B8A8_SRGB;
    up.generate_mips = false;
    up.name = "ui_image";
    e.image = rhi::upload_texture(impl_->device, up);
    e.set = ImGui_ImplVulkan_AddTexture(VK_NULL_HANDLE, e.image.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    e.size = {static_cast<f32>(img.width), static_cast<f32>(img.height)};
    if (size) *size = e.size;
    void* id = reinterpret_cast<void*>(e.set);
    impl_->entries.emplace(path, e);
    return id;
}

void TextureCache::clear() {
    vkDeviceWaitIdle(impl_->device.device());
    impl_->release_all();
}

usize TextureCache::size() const { return impl_->entries.size(); }

} // namespace tefra::ui
