#pragma once

#include "tefra/rhi/vk.h"

namespace tefra::rhi {

class Device;

struct Buffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
    void* mapped = nullptr; // persistent mapping for host-visible buffers
    VkDeviceAddress address = 0;
};

enum class MemoryUsage : u8 {
    GpuOnly,  // device-local
    Upload,   // host-visible, sequential writes (staging, per-frame constants)
    Readback, // host-visible, cached reads
};

Buffer create_buffer(const Device& dev, VkDeviceSize size, VkBufferUsageFlags usage, MemoryUsage mem,
                     const char* name = nullptr);
void destroy_buffer(const Device& dev, Buffer& b);

struct Image {
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent3D extent{};
    u32 mip_levels = 1;
    u32 layers = 1;
};

struct ImageDesc {
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    u32 width = 1, height = 1, depth = 1;
    u32 mip_levels = 1;
    u32 layers = 1;
    VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    VkImageViewType view_type = VK_IMAGE_VIEW_TYPE_2D;
    VkImageCreateFlags flags = 0;
    const char* name = nullptr;
};

Image create_image(const Device& dev, const ImageDesc& desc);
void destroy_image(const Device& dev, Image& img);

bool is_depth_format(VkFormat f);
VkImageAspectFlags aspect_of(VkFormat f);

} // namespace tefra::rhi
