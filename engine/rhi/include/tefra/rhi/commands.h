#pragma once

// Command-buffer helpers on top of VK_KHR_synchronization2.

#include "tefra/rhi/vk.h"

namespace tefra::rhi {

struct ImageBarrier {
    VkImage image = VK_NULL_HANDLE;
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    VkImageLayout old_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageLayout new_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkPipelineStageFlags2 src_stage = VK_PIPELINE_STAGE_2_NONE;
    VkAccessFlags2 src_access = VK_ACCESS_2_NONE;
    VkPipelineStageFlags2 dst_stage = VK_PIPELINE_STAGE_2_NONE;
    VkAccessFlags2 dst_access = VK_ACCESS_2_NONE;
    u32 base_mip = 0, mip_count = VK_REMAINING_MIP_LEVELS;
    u32 base_layer = 0, layer_count = VK_REMAINING_ARRAY_LAYERS;
};

void cmd_image_barrier(VkCommandBuffer cmd, const ImageBarrier& b);
void cmd_memory_barrier(VkCommandBuffer cmd, VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
                        VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access);

void cmd_begin_label(VkCommandBuffer cmd, const char* name);
void cmd_end_label(VkCommandBuffer cmd);

// One-shot command buffer for uploads/readbacks during initialisation. Blocks until done.
class Device;
class ImmediateContext {
  public:
    explicit ImmediateContext(const Device& dev);
    ~ImmediateContext();
    ImmediateContext(const ImmediateContext&) = delete;
    ImmediateContext& operator=(const ImmediateContext&) = delete;

    VkCommandBuffer begin();
    void submit_and_wait();

  private:
    const Device& dev_;
    VkCommandPool pool_ = VK_NULL_HANDLE;
    VkCommandBuffer cmd_ = VK_NULL_HANDLE;
    VkFence fence_ = VK_NULL_HANDLE;
};

} // namespace tefra::rhi
