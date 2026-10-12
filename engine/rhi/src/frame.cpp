#include "tefra/rhi/frame.h"
#include "tefra/rhi/device.h"
#include "tefra/rhi/pipeline.h"

#include "tefra/core/log.h"
#include "tefra/core/time.h"

namespace tefra::rhi {

FrameLoop::FrameLoop(const Device& dev, u32 frames_in_flight) : dev_(dev), count_(frames_in_flight) {
    TEFRA_VERIFY(count_ >= 1 && count_ <= kMaxFrames);
    VkSemaphoreTypeCreateInfo tci{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
    tci.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    tci.initialValue = 0;
    VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    sci.pNext = &tci;
    TEFRA_VK_CHECK(vkCreateSemaphore(dev_.device(), &sci, nullptr, &timeline_));
    dev_.set_name(VK_OBJECT_TYPE_SEMAPHORE, reinterpret_cast<u64>(timeline_), "frame_timeline");

    for (u32 i = 0; i < count_; ++i) {
        VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        pci.queueFamilyIndex = dev_.queue_family();
        TEFRA_VK_CHECK(vkCreateCommandPool(dev_.device(), &pci, nullptr, &pools_[i]));
        VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ai.commandPool = pools_[i];
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        TEFRA_VK_CHECK(vkAllocateCommandBuffers(dev_.device(), &ai, &cmds_[i]));
    }
}

FrameLoop::~FrameLoop() {
    wait_all();
    for (u32 i = 0; i < count_; ++i) vkDestroyCommandPool(dev_.device(), pools_[i], nullptr);
    vkDestroySemaphore(dev_.device(), timeline_, nullptr);
}

void FrameLoop::wait_next() {
    const u32 slot = static_cast<u32>(frame_number_ % count_);
    if (slot_value_[slot] == 0) return;
    VkSemaphoreWaitInfo wi{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
    wi.semaphoreCount = 1;
    wi.pSemaphores = &timeline_;
    wi.pValues = &slot_value_[slot];
    TEFRA_VK_CHECK(vkWaitSemaphores(dev_.device(), &wi, UINT64_MAX));
}

FrameContext& FrameLoop::begin() {
    framebuffer_cache_next_frame();
    const u32 slot = static_cast<u32>(frame_number_ % count_);
    const u64 t0 = time_now_ns();
    if (slot_value_[slot] > 0) {
        VkSemaphoreWaitInfo wi{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
        wi.semaphoreCount = 1;
        wi.pSemaphores = &timeline_;
        wi.pValues = &slot_value_[slot];
        TEFRA_VK_CHECK(vkWaitSemaphores(dev_.device(), &wi, UINT64_MAX));
    }
    last_wait_ns_ = time_now_ns() - t0;

    TEFRA_VK_CHECK(vkResetCommandPool(dev_.device(), pools_[slot], 0));
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    TEFRA_VK_CHECK(vkBeginCommandBuffer(cmds_[slot], &bi));
    ctx_.slot = slot;
    ctx_.frame_number = frame_number_;
    ctx_.cmd = cmds_[slot];
    return ctx_;
}

void FrameLoop::end(VkSemaphore wait_binary, VkPipelineStageFlags2 wait_stage, VkSemaphore signal_binary) {
    TEFRA_TRACE("frame: submit");
    TEFRA_VK_CHECK(vkEndCommandBuffer(ctx_.cmd));
    ++timeline_value_;
    slot_value_[ctx_.slot] = timeline_value_;

    VkSemaphoreSubmitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    wait.semaphore = wait_binary;
    wait.stageMask = wait_stage;
    VkSemaphoreSubmitInfo signals[2] = {{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO},
                                        {VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO}};
    signals[0].semaphore = timeline_;
    signals[0].value = timeline_value_;
    signals[0].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    signals[1].semaphore = signal_binary;
    signals[1].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

    VkCommandBufferSubmitInfo cbi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    cbi.commandBuffer = ctx_.cmd;
    VkSubmitInfo2 si{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    si.waitSemaphoreInfoCount = wait_binary ? 1u : 0u;
    si.pWaitSemaphoreInfos = &wait;
    si.commandBufferInfoCount = 1;
    si.pCommandBufferInfos = &cbi;
    si.signalSemaphoreInfoCount = signal_binary ? 2u : 1u;
    si.pSignalSemaphoreInfos = signals;
    TEFRA_VK_CHECK(vkQueueSubmit2KHR(dev_.queue(), 1, &si, VK_NULL_HANDLE));
    ++frame_number_;
}

void FrameLoop::wait_all() {
    if (timeline_value_ == 0) return;
    VkSemaphoreWaitInfo wi{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
    wi.semaphoreCount = 1;
    wi.pSemaphores = &timeline_;
    wi.pValues = &timeline_value_;
    TEFRA_VK_CHECK(vkWaitSemaphores(dev_.device(), &wi, UINT64_MAX));
    framebuffer_cache_clear();
}

} // namespace tefra::rhi
