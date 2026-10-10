#pragma once

// Per-pass GPU timestamps. Results become available once the frame slot is reused (frames_in_flight later).

#include "tefra/rhi/vk.h"

#include <vector>

namespace tefra::rhi {

class Device;

class GpuTimer {
  public:
    struct Result {
        const char* name; // must be a string literal / static storage
        f64 ms;
    };

    GpuTimer(const Device& dev, u32 frames_in_flight, u32 max_scopes = 32);
    ~GpuTimer();
    GpuTimer(const GpuTimer&) = delete;
    GpuTimer& operator=(const GpuTimer&) = delete;

    // Call right after FrameLoop::begin(): collects this slot's previous results, then resets its queries.
    void begin_frame(VkCommandBuffer cmd, u32 slot);
    u32 begin(VkCommandBuffer cmd, const char* name);
    void end(VkCommandBuffer cmd, u32 scope);

    bool enabled() const { return pool_ != VK_NULL_HANDLE; }
    // Results of the latest completed frame (empty until the first one completes).
    const std::vector<Result>& results() const { return results_; }
    // GPU time from the first scope begin to the last scope end of that frame.
    f64 frame_ms() const { return frame_ms_; }
    bool has_results() const { return has_results_; }

  private:
    struct Scope {
        const char* name;
        u32 begin_query, end_query;
    };
    const Device& dev_;
    VkQueryPool pool_ = VK_NULL_HANDLE;
    u32 frames_;
    u32 max_scopes_;
    u64 valid_mask_ = ~0ull;
    f64 period_ns_ = 1.0;
    u32 slot_ = 0;
    std::vector<std::vector<Scope>> scopes_; // per slot
    std::vector<u32> query_count_;           // per slot
    std::vector<Result> results_;
    std::vector<u64> raw_;
    f64 frame_ms_ = 0.0;
    bool has_results_ = false;
};

// RAII scope helper.
class GpuScope {
  public:
    GpuScope(GpuTimer& t, VkCommandBuffer cmd, const char* name) : t_(t), cmd_(cmd), id_(t.begin(cmd, name)) {}
    ~GpuScope() { t_.end(cmd_, id_); }
    GpuScope(const GpuScope&) = delete;
    GpuScope& operator=(const GpuScope&) = delete;

  private:
    GpuTimer& t_;
    VkCommandBuffer cmd_;
    u32 id_;
};

} // namespace tefra::rhi
