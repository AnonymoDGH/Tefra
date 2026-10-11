#pragma once

// Images for in-game UI (ImageLabel / ImageButton): PNG and JPEG files are decoded with the core image loader,
// uploaded as sRGB textures and registered with the ImGui Vulkan backend (ImGui_ImplVulkan_AddTexture). Each file is
// loaded once; it is reloaded when its modification time changes.

#include "tefra/core/math.h"

#include <memory>
#include <string>

namespace tefra::rhi {
class Device;
}

namespace tefra::ui {

class TextureCache {
  public:
    explicit TextureCache(const rhi::Device& device);
    ~TextureCache(); // waits for the GPU, then frees every texture
    TextureCache(const TextureCache&) = delete;
    TextureCache& operator=(const TextureCache&) = delete;

    // The ImTextureID for `path` (absolute) or nullptr when the file cannot be read. `size` receives the image size in
    // pixels. Failed files are retried when they change.
    void* get(const std::string& path, vec2* size);

    // Frees every texture now (waits for the GPU). Textures requested again are reloaded.
    void clear();

    usize size() const; // number of cached files (loaded or failed)

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace tefra::ui
