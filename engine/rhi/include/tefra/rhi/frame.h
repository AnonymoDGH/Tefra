#pragma once

// Frames in flight paced by one timeline semaphore. Each slot owns a command pool + primary buffer.

#include "tefra/rhi/vk.h"

namespace tefra::rhi {

class Device;

struct FrameContext {
    u32 slot = 0;         // 0 .. frames_in_flight-1
    u64 frame_number = 0; // monotonically increasing
    VkCommandBuffer cmd = VK_NULL_HANDLE;
};

class FrameLoop {
  public:
    static constexpr u32 kMaxFrames = 3;

    FrameLoop(const Device& dev, u32 frames_in_flight = 2);
    ~FrameLoop();
    FrameLoop(const FrameLoop&) = delete;
    FrameLoop& operator=(const FrameLoop&) = delete;

    // Waits until this slot's previous submission finished, then begins its command buffer.
    FrameContext& begin();
    // Waits until the next begin() will not block (the GPU finished that slot). Call it before polling input so the
    // frame is built from fresh input instead of input that waited behind the GPU.
    void wait_next();
    // Ends and submits. Optional binary semaphores for swapchain acquire/present.
    void end(VkSemaphore wait_binary = VK_NULL_HANDLE,
             VkPipelineStageFlags2 wait_stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
             VkSemaphore signal_binary = VK_NULL_HANDLE);
    // Blocks until every submitted frame has finished on the GPU.
    void wait_all();

    u32 frames_in_flight() const { return count_; }
    // Nanoseconds the CPU spent blocked waiting for the GPU in the last begin().
    u64 last_wait_ns() const { return last_wait_ns_; }

  private:
    const Device& dev_;
    u32 count_;
    VkSemaphore timeline_ = VK_NULL_HANDLE;
    u64 timeline_value_ = 0;
    u64 slot_value_[kMaxFrames] = {};
    VkCommandPool pools_[kMaxFrames] = {};
    VkCommandBuffer cmds_[kMaxFrames] = {};
    FrameContext ctx_;
    u64 frame_number_ = 0;
    u64 last_wait_ns_ = 0;
};

} // namespace tefra::rhi
