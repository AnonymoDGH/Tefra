#pragma once

#include "tefra/rhi/resources.h"

#include "tefra/core/image.h"

namespace tefra::rhi {

struct ReadbackRequest {
    Buffer buffer;
    u32 width = 0;
    u32 height = 0;
    VkFormat format = VK_FORMAT_UNDEFINED;
};

// Records a copy of a color image into a host-visible buffer. The image ends in TRANSFER_SRC_OPTIMAL.
// Supported formats: RGBA8/BGRA8 (UNORM or SRGB; bytes are copied as stored).
ReadbackRequest cmd_readback_image(const Device& dev, VkCommandBuffer cmd, VkImage image, VkFormat format,
                                   VkExtent2D extent, VkImageLayout current_layout, VkPipelineStageFlags2 src_stage,
                                   VkAccessFlags2 src_access);

// Call after the GPU finished the command buffer. Releases the staging buffer.
bool finish_readback(const Device& dev, ReadbackRequest& req, ImageRGBA8& out);

} // namespace tefra::rhi
