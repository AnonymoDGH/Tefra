#include "tefra/rhi/gpu_timer.h"
#include "tefra/rhi/commands.h"
#include "tefra/rhi/device.h"

namespace tefra::rhi {

GpuTimer::GpuTimer(const Device& dev, u32 frames_in_flight, u32 max_scopes)
    : dev_(dev), frames_(frames_in_flight), max_scopes_(max_scopes) {
    scopes_.resize(frames_);
    query_count_.assign(frames_, 0);
    if (!dev_.caps().timestamps) return;

    u32 qf_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(dev_.physical(), &qf_count, nullptr);
    std::vector<VkQueueFamilyProperties> qfs(qf_count);
    vkGetPhysicalDeviceQueueFamilyProperties(dev_.physical(), &qf_count, qfs.data());
    const u32 bits = qfs[dev_.queue_family()].timestampValidBits;
    if (bits == 0) return;
    valid_mask_ = bits >= 64 ? ~0ull : ((1ull << bits) - 1ull);
    period_ns_ = static_cast<f64>(dev_.caps().timestamp_period_ns);

    VkQueryPoolCreateInfo qci{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    qci.queryType = VK_QUERY_TYPE_TIMESTAMP;
    qci.queryCount = frames_ * max_scopes_ * 2;
    TEFRA_VK_CHECK(vkCreateQueryPool(dev_.device(), &qci, nullptr, &pool_));
    raw_.resize(max_scopes_ * 2);
}

GpuTimer::~GpuTimer() {
    if (pool_) vkDestroyQueryPool(dev_.device(), pool_, nullptr);
}

void GpuTimer::begin_frame(VkCommandBuffer cmd, u32 slot) {
    slot_ = slot;
    if (!pool_) return;
    const u32 base = slot * max_scopes_ * 2;
    const u32 used = query_count_[slot];
    if (used > 0) {
        // The slot's previous frame has completed (FrameLoop waited on it), so results are ready.
        const VkResult r = vkGetQueryPoolResults(dev_.device(), pool_, base, used, used * sizeof(u64), raw_.data(),
                                                 sizeof(u64), VK_QUERY_RESULT_64_BIT);
        if (r == VK_SUCCESS) {
            results_.clear();
            u64 first = ~0ull, last = 0;
            for (const Scope& s : scopes_[slot]) {
                const u64 b = raw_[s.begin_query - base] & valid_mask_;
                const u64 e = raw_[s.end_query - base] & valid_mask_;
                const u64 delta = (e - b) & valid_mask_;
                results_.push_back({s.name, static_cast<f64>(delta) * period_ns_ * 1e-6});
                if (b < first) first = b;
                if (e > last) last = e;
            }
            frame_ms_ = last > first ? static_cast<f64>((last - first) & valid_mask_) * period_ns_ * 1e-6 : 0.0;
            has_results_ = true;
        }
    }
    scopes_[slot].clear();
    query_count_[slot] = 0;
    vkCmdResetQueryPool(cmd, pool_, base, max_scopes_ * 2);
}

u32 GpuTimer::begin(VkCommandBuffer cmd, const char* name) {
    cmd_begin_label(cmd, name);
    if (!pool_ || query_count_[slot_] + 2 > max_scopes_ * 2) return ~0u;
    const u32 base = slot_ * max_scopes_ * 2;
    Scope s{name, base + query_count_[slot_], base + query_count_[slot_] + 1};
    query_count_[slot_] += 2;
    vkCmdWriteTimestamp2KHR(cmd, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, pool_, s.begin_query);
    scopes_[slot_].push_back(s);
    return static_cast<u32>(scopes_[slot_].size() - 1);
}

void GpuTimer::end(VkCommandBuffer cmd, u32 scope) {
    if (pool_ && scope != ~0u)
        vkCmdWriteTimestamp2KHR(cmd, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, pool_, scopes_[slot_][scope].end_query);
    cmd_end_label(cmd);
}

} // namespace tefra::rhi
