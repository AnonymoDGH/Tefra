#include "tefra/rhi/upload.h"
#include "tefra/rhi/commands.h"
#include "tefra/rhi/device.h"

#include "tefra/core/math.h"

#include <cstring>
#include <vector>

namespace tefra::rhi {

u32 bytes_per_pixel(VkFormat f) {
    switch (f) {
    case VK_FORMAT_R8G8B8A8_SRGB:
    case VK_FORMAT_R8G8B8A8_UNORM:
    case VK_FORMAT_B8G8R8A8_SRGB:
    case VK_FORMAT_B8G8R8A8_UNORM: return 4;
    case VK_FORMAT_R8G8_UNORM: return 2;
    case VK_FORMAT_R8_UNORM: return 1;
    case VK_FORMAT_R16_UNORM: return 2;
    case VK_FORMAT_R16G16_SFLOAT: return 4;
    case VK_FORMAT_R16G16B16A16_SFLOAT: return 8;
    case VK_FORMAT_R32G32B32A32_SFLOAT: return 16;
    case VK_FORMAT_R32_SFLOAT: return 4;
    default: TEFRA_PANIC("bytes_per_pixel: unsupported format %d", static_cast<int>(f));
    }
}

u32 mip_count_for(u32 width, u32 height) {
    u32 levels = 1;
    u32 size = max(width, height);
    while (size > 1) {
        size >>= 1;
        ++levels;
    }
    return levels;
}

namespace {

f32 coverage(const std::vector<u8>& px, f32 cutoff, f32 scale) {
    usize pass = 0;
    for (usize i = 3; i < px.size(); i += 4) pass += (f32(px[i]) / 255.0f * scale) >= cutoff ? 1 : 0;
    return f32(pass) / f32(px.size() / 4);
}

// CPU mip chain with alpha coverage preservation. Returns the packed levels (level 0 first).
std::vector<u8> build_coverage_mips(const u8* src, u32 w, u32 h, u32 levels, f32 cutoff) {
    std::vector<u8> out(src, src + usize(w) * h * 4);
    std::vector<u8> prev(out);
    const f32 target = coverage(prev, cutoff, 1.0f);
    u32 pw = w, ph = h;
    for (u32 l = 1; l < levels; ++l) {
        const u32 nw = max(pw / 2, 1u), nh = max(ph / 2, 1u);
        std::vector<u8> cur(usize(nw) * nh * 4);
        for (u32 y = 0; y < nh; ++y)
            for (u32 x = 0; x < nw; ++x)
                for (u32 c = 0; c < 4; ++c) {
                    u32 sum = 0, n = 0;
                    for (u32 dy = 0; dy < 2; ++dy)
                        for (u32 dx = 0; dx < 2; ++dx) {
                            const u32 sx = min(x * 2 + dx, pw - 1), sy = min(y * 2 + dy, ph - 1);
                            sum += prev[(usize(sy) * pw + sx) * 4 + c];
                            ++n;
                        }
                    cur[(usize(y) * nw + x) * 4 + c] = static_cast<u8>((sum + n / 2) / n);
                }
        // Binary search the alpha scale that restores the level-0 coverage.
        f32 lo = 0.0f, hi = 4.0f;
        for (int it = 0; it < 12; ++it) {
            const f32 mid = 0.5f * (lo + hi);
            if (coverage(cur, cutoff, mid) < target)
                lo = mid;
            else
                hi = mid;
        }
        for (usize i = 3; i < cur.size(); i += 4) cur[i] = static_cast<u8>(min(255.0f, f32(cur[i]) * hi));
        out.insert(out.end(), cur.begin(), cur.end());
        prev = std::move(cur);
        pw = nw;
        ph = nh;
    }
    return out;
}

} // namespace

Image upload_texture(const Device& dev, const TextureUpload& up) {
    TEFRA_VERIFY(up.pixels && up.width > 0 && up.height > 0);
    if (up.alpha_coverage_cutoff > 0.0f) {
        TEFRA_VERIFY(bytes_per_pixel(up.format) == 4);
        const u32 levels = mip_count_for(up.width, up.height);
        const std::vector<u8> data = build_coverage_mips(static_cast<const u8*>(up.pixels), up.width, up.height, levels,
                                                         up.alpha_coverage_cutoff);
        ImageDesc d;
        d.format = up.format;
        d.width = up.width;
        d.height = up.height;
        d.mip_levels = levels;
        d.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        d.name = up.name;
        Image img = create_image(dev, d);
        Buffer staging =
            create_buffer(dev, data.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, MemoryUsage::Upload, "staging");
        std::memcpy(staging.mapped, data.data(), data.size());
        vmaFlushAllocation(dev.allocator(), staging.allocation, 0, VK_WHOLE_SIZE);
        ImmediateContext ctx(dev);
        VkCommandBuffer cmd = ctx.begin();
        ImageBarrier b;
        b.image = img.image;
        b.old_layout = VK_IMAGE_LAYOUT_UNDEFINED;
        b.new_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.dst_stage = VK_PIPELINE_STAGE_2_COPY_BIT;
        b.dst_access = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        cmd_image_barrier(cmd, b);
        std::vector<VkBufferImageCopy> regions;
        VkDeviceSize offset = 0;
        u32 w = up.width, h = up.height;
        for (u32 l = 0; l < levels; ++l) {
            VkBufferImageCopy r{};
            r.bufferOffset = offset;
            r.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, l, 0, 1};
            r.imageExtent = {w, h, 1};
            regions.push_back(r);
            offset += VkDeviceSize(w) * h * 4;
            w = max(w / 2, 1u);
            h = max(h / 2, 1u);
        }
        vkCmdCopyBufferToImage(cmd, staging.buffer, img.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               static_cast<u32>(regions.size()), regions.data());
        b.old_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.new_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b.src_stage = VK_PIPELINE_STAGE_2_COPY_BIT;
        b.src_access = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        b.dst_stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        b.dst_access = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        cmd_image_barrier(cmd, b);
        ctx.submit_and_wait();
        destroy_buffer(dev, staging);
        return img;
    }
    const u32 levels = up.generate_mips ? mip_count_for(up.width, up.height) : 1;
    ImageDesc d;
    d.format = up.format;
    d.width = up.width;
    d.height = up.height;
    d.mip_levels = levels;
    d.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    d.name = up.name;
    Image img = create_image(dev, d);

    const VkDeviceSize size = VkDeviceSize(up.width) * up.height * bytes_per_pixel(up.format);
    Buffer staging = create_buffer(dev, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, MemoryUsage::Upload, "staging");
    std::memcpy(staging.mapped, up.pixels, size);
    vmaFlushAllocation(dev.allocator(), staging.allocation, 0, VK_WHOLE_SIZE);

    ImmediateContext ctx(dev);
    VkCommandBuffer cmd = ctx.begin();
    ImageBarrier b;
    b.image = img.image;
    b.old_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    b.new_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.dst_stage = VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT;
    b.dst_access = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    cmd_image_barrier(cmd, b);

    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {up.width, up.height, 1};
    vkCmdCopyBufferToImage(cmd, staging.buffer, img.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // Mip chain: level i-1 (TRANSFER_SRC) -> level i (TRANSFER_DST) with a linear blit.
    i32 w = static_cast<i32>(up.width), h = static_cast<i32>(up.height);
    for (u32 level = 1; level < levels; ++level) {
        ImageBarrier to_src;
        to_src.image = img.image;
        to_src.base_mip = level - 1;
        to_src.mip_count = 1;
        to_src.old_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        to_src.new_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        to_src.src_stage = VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT;
        to_src.src_access = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        to_src.dst_stage = VK_PIPELINE_STAGE_2_BLIT_BIT;
        to_src.dst_access = VK_ACCESS_2_TRANSFER_READ_BIT;
        cmd_image_barrier(cmd, to_src);
        const i32 nw = max(w / 2, 1), nh = max(h / 2, 1);
        VkImageBlit blit{};
        blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level - 1, 0, 1};
        blit.srcOffsets[1] = {w, h, 1};
        blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1};
        blit.dstOffsets[1] = {nw, nh, 1};
        vkCmdBlitImage(cmd, img.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, img.image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
        w = nw;
        h = nh;
    }
    // Last level is still TRANSFER_DST; the others are TRANSFER_SRC. Bring all to SHADER_READ_ONLY.
    if (levels > 1) {
        ImageBarrier rest;
        rest.image = img.image;
        rest.base_mip = 0;
        rest.mip_count = levels - 1;
        rest.old_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        rest.new_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        rest.src_stage = VK_PIPELINE_STAGE_2_BLIT_BIT;
        rest.src_access = VK_ACCESS_2_NONE;
        rest.dst_stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        rest.dst_access = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        cmd_image_barrier(cmd, rest);
    }
    ImageBarrier last;
    last.image = img.image;
    last.base_mip = levels - 1;
    last.mip_count = 1;
    last.old_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    last.new_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    last.src_stage = VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT;
    last.src_access = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    last.dst_stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    last.dst_access = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    cmd_image_barrier(cmd, last);
    ctx.submit_and_wait();
    destroy_buffer(dev, staging);
    return img;
}

Buffer upload_buffer(const Device& dev, const void* data, VkDeviceSize size, VkBufferUsageFlags usage,
                     const char* name) {
    Buffer buf = create_buffer(dev, size, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, MemoryUsage::GpuOnly, name);
    Buffer staging = create_buffer(dev, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, MemoryUsage::Upload, "staging");
    std::memcpy(staging.mapped, data, size);
    vmaFlushAllocation(dev.allocator(), staging.allocation, 0, VK_WHOLE_SIZE);
    ImmediateContext ctx(dev);
    VkCommandBuffer cmd = ctx.begin();
    VkBufferCopy region{0, 0, size};
    vkCmdCopyBuffer(cmd, staging.buffer, buf.buffer, 1, &region);
    cmd_memory_barrier(cmd, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                       VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT);
    ctx.submit_and_wait();
    destroy_buffer(dev, staging);
    return buf;
}

} // namespace tefra::rhi
