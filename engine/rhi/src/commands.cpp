#include "tefra/rhi/commands.h"
#include "tefra/rhi/device.h"

namespace tefra::rhi {

void cmd_image_barrier(VkCommandBuffer cmd, const ImageBarrier& b) {
    VkImageMemoryBarrier2 ib{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    ib.srcStageMask = b.src_stage;
    ib.srcAccessMask = b.src_access;
    ib.dstStageMask = b.dst_stage;
    ib.dstAccessMask = b.dst_access;
    ib.oldLayout = b.old_layout;
    ib.newLayout = b.new_layout;
    ib.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ib.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ib.image = b.image;
    ib.subresourceRange = {b.aspect, b.base_mip, b.mip_count, b.base_layer, b.layer_count};
    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &ib;
    vkCmdPipelineBarrier2KHR(cmd, &dep);
}

void cmd_memory_barrier(VkCommandBuffer cmd, VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
                        VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access) {
    VkMemoryBarrier2 mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    mb.srcStageMask = src_stage;
    mb.srcAccessMask = src_access;
    mb.dstStageMask = dst_stage;
    mb.dstAccessMask = dst_access;
    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.memoryBarrierCount = 1;
    dep.pMemoryBarriers = &mb;
    vkCmdPipelineBarrier2KHR(cmd, &dep);
}

void cmd_begin_label(VkCommandBuffer cmd, const char* name) {
    if (!vkCmdBeginDebugUtilsLabelEXT) return;
    VkDebugUtilsLabelEXT label{VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT};
    label.pLabelName = name;
    vkCmdBeginDebugUtilsLabelEXT(cmd, &label);
}

void cmd_end_label(VkCommandBuffer cmd) {
    if (!vkCmdEndDebugUtilsLabelEXT) return;
    vkCmdEndDebugUtilsLabelEXT(cmd);
}

ImmediateContext::ImmediateContext(const Device& dev) : dev_(dev) {
    VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    pci.queueFamilyIndex = dev.queue_family();
    TEFRA_VK_CHECK(vkCreateCommandPool(dev.device(), &pci, nullptr, &pool_));
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = pool_;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    TEFRA_VK_CHECK(vkAllocateCommandBuffers(dev.device(), &ai, &cmd_));
    VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    TEFRA_VK_CHECK(vkCreateFence(dev.device(), &fci, nullptr, &fence_));
}

ImmediateContext::~ImmediateContext() {
    vkDestroyFence(dev_.device(), fence_, nullptr);
    vkDestroyCommandPool(dev_.device(), pool_, nullptr);
}

VkCommandBuffer ImmediateContext::begin() {
    TEFRA_VK_CHECK(vkResetCommandBuffer(cmd_, 0));
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    TEFRA_VK_CHECK(vkBeginCommandBuffer(cmd_, &bi));
    return cmd_;
}

void ImmediateContext::submit_and_wait() {
    TEFRA_VK_CHECK(vkEndCommandBuffer(cmd_));
    VkCommandBufferSubmitInfo cbi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    cbi.commandBuffer = cmd_;
    VkSubmitInfo2 si{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    si.commandBufferInfoCount = 1;
    si.pCommandBufferInfos = &cbi;
    TEFRA_VK_CHECK(vkQueueSubmit2KHR(dev_.queue(), 1, &si, fence_));
    TEFRA_VK_CHECK(vkWaitForFences(dev_.device(), 1, &fence_, VK_TRUE, UINT64_MAX));
    TEFRA_VK_CHECK(vkResetFences(dev_.device(), 1, &fence_));
}

} // namespace tefra::rhi
