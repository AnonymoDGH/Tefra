#include "tefra/renderer/canopy.h"

#include "tefra/renderer/vegetation.h"
#include "tefra/rhi/bindless.h"
#include "tefra/rhi/commands.h"
#include "tefra/rhi/device.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace tefra::render {
namespace {

constexpr VkFormat kCanopyFormat = VK_FORMAT_R16G16_UNORM;
constexpr f64 kRebuildDistance = 64.0; // m: rebuilt when the camera moves this far from the centre (>= 192 m margin)
constexpr i32 kBlurRadius = 6;         // texels (sigma 2) for the cover
constexpr f32 kBlurSigma = 2.0f;
constexpr i32 kTopRadius = 3; // texels: the top height is dilated so the edges of tall crowns stay covered
constexpr usize kTexels = usize(kCanopyRes) * kCanopyRes;
constexpr u32 kNone = 0xFFFFFFFFu;

u16 unorm16(f32 v) { return static_cast<u16>(saturate(v) * 65535.0f + 0.5f); }

// Separable 1D pass over a square grid (weighted sum, or max when use_max), clamped at the edges. Row-contiguous
// inner loops so they vectorise: vertical passes accumulate whole rows; horizontal passes first pad the row with
// clamped copies so the taps need no per-sample clamping.
constexpr i32 kMaxTapRadius = 8;
void filter_1d(const std::vector<f32>& src, std::vector<f32>& dst, bool horizontal, i32 r, const std::vector<f32>& w,
               bool use_max) {
    const i32 n = static_cast<i32>(kCanopyRes);
    f32 pad[kCanopyRes + 2 * kMaxTapRadius];
    for (i32 y = 0; y < n; ++y) {
        f32* out = &dst[usize(y) * n];
        const f32 init = use_max ? -1e30f : 0.0f;
        for (i32 x = 0; x < n; ++x) out[x] = init;
        if (!horizontal) {
            for (i32 k = -r; k <= r; ++k) {
                const f32* in = &src[usize(clamp(y + k, 0, n - 1)) * n];
                if (use_max) {
                    for (i32 x = 0; x < n; ++x) out[x] = max(out[x], in[x]);
                } else {
                    const f32 wk = w[usize(k + r)];
                    for (i32 x = 0; x < n; ++x) out[x] += wk * in[x];
                }
            }
        } else {
            const f32* in = &src[usize(y) * n];
            for (i32 i = 0; i < n + 2 * r; ++i) pad[i] = in[clamp(i - r, 0, n - 1)];
            for (i32 k = -r; k <= r; ++k) {
                const f32* p = pad + (k + r);
                if (use_max) {
                    for (i32 x = 0; x < n; ++x) out[x] = max(out[x], p[x]);
                } else {
                    const f32 wk = w[usize(k + r)];
                    for (i32 x = 0; x < n; ++x) out[x] += wk * p[x];
                }
            }
        }
    }
}

} // namespace

CanopyMap::CanopyMap(const rhi::Device& dev, rhi::BindlessHeap& heap, u32 slots)
    : dev_(dev), heap_(heap), slots_(slots) {
    VkFormatProperties p{};
    vkGetPhysicalDeviceFormatProperties(dev_.physical(), kCanopyFormat, &p);
    const VkFormatFeatureFlags need = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                                      VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT |
                                      VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    supported_ = (p.optimalTilingFeatures & need) == need;
    if (supported_) {
        cpu_.assign(kTexels * 2, 0);
        tau_.assign(kTexels, 0.0f);
        top_.assign(kTexels, 0.0f);
        tmp_.assign(kTexels, 0.0f);
    }
}

CanopyMap::~CanopyMap() {
    for (Slot& s : slots_) {
        if (s.bindless != kNone) heap_.remove_texture(s.bindless);
        rhi::destroy_image(dev_, s.image);
        rhi::destroy_buffer(dev_, s.staging);
    }
}

void CanopyMap::update(const Vegetation& veg, dvec3 cam) {
    if (!supported_) return;
    const bool moved = !built_ || veg.uid() != veg_uid_ || std::fabs(cam.x - center_x_) > kRebuildDistance ||
                       std::fabs(cam.z - center_z_) > kRebuildDistance;
    if (!moved) return;
    const f32 texel = kCanopyTexelSize;
    const f32 half_m = 0.5f * f32(kCanopyRes) * texel;
    // Min corner on whole metres: the texel grid is fixed in the world, so rebuilds do not shift the content.
    const f64 ox = std::floor(cam.x - f64(half_m)), oz = std::floor(cam.z - f64(half_m));
    origin_ = {f32(ox), f32(oz)};
    center_x_ = cam.x;
    center_z_ = cam.z;
    veg_uid_ = veg.uid();
    built_ = true;

    std::vector<Vegetation::CanopyInstance> trees;
    veg.collect_canopy(vec2{f32(ox) + half_m, f32(oz) + half_m}, half_m, trees);
    std::fill(tau_.begin(), tau_.end(), 0.0f);
    std::fill(top_.begin(), top_.end(), kCanopyYBase);
    const i32 n = static_cast<i32>(kCanopyRes);
    for (const auto& t : trees) {
        // Soft-edged disc of optical depth `density` (1 m edge), plus the top height inside the footprint.
        const f32 reach = t.radius + 1.0f;
        const i32 x0 = max(0, i32(std::floor((t.x - reach - origin_.x) / texel)));
        const i32 x1 = min(n - 1, i32(std::floor((t.x + reach - origin_.x) / texel)));
        const i32 z0 = max(0, i32(std::floor((t.z - reach - origin_.y) / texel)));
        const i32 z1 = min(n - 1, i32(std::floor((t.z + reach - origin_.y) / texel)));
        for (i32 z = z0; z <= z1; ++z)
            for (i32 x = x0; x <= x1; ++x) {
                const f32 dx = origin_.x + (f32(x) + 0.5f) * texel - t.x;
                const f32 dz = origin_.y + (f32(z) + 0.5f) * texel - t.z;
                const f32 w = saturate(t.radius + 0.5f - std::sqrt(dx * dx + dz * dz));
                if (w <= 0.0f) continue;
                const usize i = usize(z) * kCanopyRes + usize(x);
                tau_[i] += t.density * w;
                top_[i] = max(top_[i], t.top_y);
            }
    }

    // Gaussian blur of the cover (on optical depth, so overlapping crowns add up), max filter of the top height.
    std::vector<f32> wts(usize(2 * kBlurRadius + 1));
    f32 sum = 0.0f;
    for (i32 k = -kBlurRadius; k <= kBlurRadius; ++k) {
        wts[usize(k + kBlurRadius)] = std::exp(-0.5f * f32(k * k) / (kBlurSigma * kBlurSigma));
        sum += wts[usize(k + kBlurRadius)];
    }
    for (f32& w : wts) w /= sum;
    std::vector<f32>& tmp = tmp_;
    const std::vector<f32> no_weights;
    filter_1d(tau_, tmp, true, kBlurRadius, wts, false);
    filter_1d(tmp, tau_, false, kBlurRadius, wts, false);
    filter_1d(top_, tmp, true, kTopRadius, no_weights, true);
    filter_1d(tmp, top_, false, kTopRadius, no_weights, true);

    for (usize i = 0; i < kTexels; ++i) {
        cpu_[2 * i] = unorm16(1.0f - std::exp(-tau_[i]));
        cpu_[2 * i + 1] = unorm16((top_[i] - kCanopyYBase) / kCanopyYRange);
    }
    ++version_;
}

CanopyBinding CanopyMap::sync(VkCommandBuffer cmd, u32 slot) {
    CanopyBinding out;
    if (!supported_ || !built_) return out;
    Slot& s = slots_[slot];
    const usize bytes = kTexels * 4;
    if (!s.image.image) {
        rhi::ImageDesc d;
        d.format = kCanopyFormat;
        d.width = d.height = kCanopyRes;
        d.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        d.name = "canopy_map";
        s.image = rhi::create_image(dev_, d);
        s.bindless = heap_.add_texture(s.image.view);
        s.staging = rhi::create_buffer(dev_, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, rhi::MemoryUsage::Upload,
                                       "canopy_staging");
    }
    if (s.version != version_) {
        // The slot fence guarantees the previous copy from this slot and its fragment-shader reads are finished.
        const bool first = s.version == 0;
        std::memcpy(s.staging.mapped, cpu_.data(), bytes);
        vmaFlushAllocation(dev_.allocator(), s.staging.allocation, 0, VK_WHOLE_SIZE);
        rhi::ImageBarrier to_dst;
        to_dst.image = s.image.image;
        to_dst.old_layout = first ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        to_dst.new_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        to_dst.src_stage = first ? VK_PIPELINE_STAGE_2_NONE : VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        to_dst.src_access = first ? VK_ACCESS_2_NONE : VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        to_dst.dst_stage = VK_PIPELINE_STAGE_2_COPY_BIT;
        to_dst.dst_access = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        rhi::cmd_image_barrier(cmd, to_dst);
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {kCanopyRes, kCanopyRes, 1};
        vkCmdCopyBufferToImage(cmd, s.staging.buffer, s.image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        rhi::ImageBarrier to_read = to_dst;
        to_read.old_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        to_read.new_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        to_read.src_stage = VK_PIPELINE_STAGE_2_COPY_BIT;
        to_read.src_access = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        to_read.dst_stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        to_read.dst_access = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        rhi::cmd_image_barrier(cmd, to_read);
        s.version = version_;
    }
    out.index = s.bindless;
    out.origin = origin_;
    out.size = f32(kCanopyRes) * kCanopyTexelSize;
    return out;
}

} // namespace tefra::render
