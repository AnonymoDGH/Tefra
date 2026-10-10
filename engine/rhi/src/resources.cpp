#include "tefra/rhi/resources.h"
#include "tefra/rhi/device.h"

#include "tefra/core/memory.h"

namespace tefra::rhi {

Buffer create_buffer(const Device& dev, VkDeviceSize size, VkBufferUsageFlags usage, MemoryUsage mem,
                     const char* name) {
    Buffer b;
    b.size = size;
    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size = size;
    bci.usage = usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo aci{};
    aci.usage = VMA_MEMORY_USAGE_AUTO;
    switch (mem) {
    case MemoryUsage::GpuOnly: aci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE; break;
    case MemoryUsage::Upload:
        aci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        break;
    case MemoryUsage::Readback:
        aci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
        aci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        break;
    }
    VmaAllocationInfo info{};
    TEFRA_VK_CHECK(vmaCreateBuffer(dev.allocator(), &bci, &aci, &b.buffer, &b.allocation, &info));
    b.mapped = info.pMappedData;
    VkBufferDeviceAddressInfo bdai{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
    bdai.buffer = b.buffer;
    b.address = vkGetBufferDeviceAddress(dev.device(), &bdai);
    if (name) dev.set_name(VK_OBJECT_TYPE_BUFFER, reinterpret_cast<u64>(b.buffer), name);
    mem_track(MemTag::Render, static_cast<i64>(size));
    return b;
}

void destroy_buffer(const Device& dev, Buffer& b) {
    if (!b.buffer) return;
    vmaDestroyBuffer(dev.allocator(), b.buffer, b.allocation);
    mem_track(MemTag::Render, -static_cast<i64>(b.size));
    b = {};
}

bool is_depth_format(VkFormat f) {
    switch (f) {
    case VK_FORMAT_D16_UNORM:
    case VK_FORMAT_D32_SFLOAT:
    case VK_FORMAT_D24_UNORM_S8_UINT:
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
    case VK_FORMAT_X8_D24_UNORM_PACK32: return true;
    default: return false;
    }
}

VkImageAspectFlags aspect_of(VkFormat f) {
    switch (f) {
    case VK_FORMAT_D24_UNORM_S8_UINT:
    case VK_FORMAT_D32_SFLOAT_S8_UINT: return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    default: return is_depth_format(f) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    }
}

Image create_image(const Device& dev, const ImageDesc& desc) {
    Image img;
    img.format = desc.format;
    img.extent = {desc.width, desc.height, desc.depth};
    img.mip_levels = desc.mip_levels;
    img.layers = desc.layers;

    VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ici.flags = desc.flags;
    ici.imageType = desc.depth > 1 ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
    ici.format = desc.format;
    ici.extent = img.extent;
    ici.mipLevels = desc.mip_levels;
    ici.arrayLayers = desc.layers;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = desc.usage;
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo aci{};
    aci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    if (desc.usage & (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT))
        aci.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
    TEFRA_VK_CHECK(vmaCreateImage(dev.allocator(), &ici, &aci, &img.image, &img.allocation, nullptr));

    VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vci.image = img.image;
    vci.viewType = desc.view_type;
    vci.format = desc.format;
    vci.subresourceRange = {aspect_of(desc.format), 0, desc.mip_levels, 0, desc.layers};
    if (is_depth_format(desc.format)) vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    TEFRA_VK_CHECK(vkCreateImageView(dev.device(), &vci, nullptr, &img.view));
    if (desc.name) {
        dev.set_name(VK_OBJECT_TYPE_IMAGE, reinterpret_cast<u64>(img.image), desc.name);
        dev.set_name(VK_OBJECT_TYPE_IMAGE_VIEW, reinterpret_cast<u64>(img.view), desc.name);
    }
    return img;
}

void destroy_image(const Device& dev, Image& img) {
    if (img.view) vkDestroyImageView(dev.device(), img.view, nullptr);
    if (img.image) vmaDestroyImage(dev.allocator(), img.image, img.allocation);
    img = {};
}

} // namespace tefra::rhi
