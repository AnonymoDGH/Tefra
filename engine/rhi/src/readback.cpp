#include "tefra/rhi/readback.h"
#include "tefra/rhi/commands.h"
#include "tefra/rhi/device.h"

#include <cstring>

namespace tefra::rhi {

ReadbackRequest cmd_readback_image(const Device& dev, VkCommandBuffer cmd, VkImage image, VkFormat format,
                                   VkExtent2D extent, VkImageLayout current_layout, VkPipelineStageFlags2 src_stage,
                                   VkAccessFlags2 src_access) {
    ReadbackRequest req;
    req.width = extent.width;
    req.height = extent.height;
    req.format = format;
    req.buffer = create_buffer(dev, VkDeviceSize(extent.width) * extent.height * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                               MemoryUsage::Readback, "readback");

    ImageBarrier b;
    b.image = image;
    b.old_layout = current_layout;
    b.new_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    b.src_stage = src_stage;
    b.src_access = src_access;
    b.dst_stage = VK_PIPELINE_STAGE_2_COPY_BIT;
    b.dst_access = VK_ACCESS_2_TRANSFER_READ_BIT;
    cmd_image_barrier(cmd, b);

    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {extent.width, extent.height, 1};
    vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, req.buffer.buffer, 1, &region);
    cmd_memory_barrier(cmd, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_HOST_BIT,
                       VK_ACCESS_2_HOST_READ_BIT);
    return req;
}

bool finish_readback(const Device& dev, ReadbackRequest& req, ImageRGBA8& out) {
    if (!req.buffer.buffer) return false;
    vmaInvalidateAllocation(dev.allocator(), req.buffer.allocation, 0, VK_WHOLE_SIZE);
    out.width = req.width;
    out.height = req.height;
    out.pixels.resize(usize(req.width) * req.height * 4);
    std::memcpy(out.pixels.data(), req.buffer.mapped, out.pixels.size());
    const bool bgra = req.format == VK_FORMAT_B8G8R8A8_UNORM || req.format == VK_FORMAT_B8G8R8A8_SRGB;
    if (bgra)
        for (usize i = 0; i < out.pixels.size(); i += 4) std::swap(out.pixels[i], out.pixels[i + 2]);
    destroy_buffer(dev, req.buffer);
    return true;
}

} // namespace tefra::rhi
