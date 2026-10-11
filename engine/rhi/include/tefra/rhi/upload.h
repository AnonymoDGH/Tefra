#pragma once

// Blocking uploads for load time (textures with mip generation, static buffers). Streaming uploads use a
// different path (M3).

#include "tefra/rhi/resources.h"

namespace tefra::rhi {

class Device;

struct TextureUpload {
    const void* pixels = nullptr; // tightly packed, top row first
    u32 width = 0, height = 0;
    VkFormat format = VK_FORMAT_R8G8B8A8_SRGB; // R8G8B8A8_{SRGB,UNORM}, R16G16B16A16_SFLOAT, R32G32B32A32_SFLOAT
    bool generate_mips = true;
    // > 0: RGBA8 only. Mips are built on the CPU and their alpha is rescaled so the fraction of texels passing
    // this alpha-test cutoff matches level 0 (Castano 2010). Keeps alpha-tested foliage from thinning out.
    f32 alpha_coverage_cutoff = 0.0f;
    const char* name = nullptr;
};

u32 bytes_per_pixel(VkFormat f);
u32 mip_count_for(u32 width, u32 height);

// Creates a sampled image (+ transfer usages), uploads level 0, generates the mip chain with linear blits,
// and leaves it in SHADER_READ_ONLY_OPTIMAL.
Image upload_texture(const Device& dev, const TextureUpload& up);

// Device-local buffer initialised with `data` (usage gets TRANSFER_DST added).
Buffer upload_buffer(const Device& dev, const void* data, VkDeviceSize size, VkBufferUsageFlags usage,
                     const char* name = nullptr);

} // namespace tefra::rhi
