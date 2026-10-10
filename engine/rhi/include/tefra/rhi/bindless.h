#pragma once

// Global bindless descriptor set (set 0): sampled 2D textures, cube textures and a fixed sampler table.
// One descriptor set per frame slot; writes are queued and applied to a slot's set only when that slot is
// not in use by the GPU, so no update-while-pending support is required from the driver.

#include "tefra/rhi/vk.h"

#include <vector>

namespace tefra::rhi {

class Device;

enum class SamplerId : u32 {
    LinearRepeat = 0,
    LinearClamp,
    NearestClamp,
    AnisoRepeat,   // 16x (clamped to device limit), for material textures
    ShadowCompare, // depth compare LESS_OR_EQUAL, linear (hardware PCF)
    LinearClampMip,
    NearestRepeat,
    AnisoClamp,
    Count
};

class BindlessHeap {
  public:
    static constexpr u32 kMaxTextures = 16384;
    static constexpr u32 kMaxCubes = 256;
    static constexpr u32 kErrorTexture = 0; // magenta/black checker

    BindlessHeap(const Device& dev, u32 frames_in_flight);
    ~BindlessHeap();
    BindlessHeap(const BindlessHeap&) = delete;
    BindlessHeap& operator=(const BindlessHeap&) = delete;

    // View must be in SHADER_READ_ONLY_OPTIMAL (or DEPTH_STENCIL_READ_ONLY_OPTIMAL for depth, see layout arg)
    // whenever a shader samples it.
    u32 add_texture(VkImageView view, VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    void update_texture(u32 index, VkImageView view, VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    void remove_texture(u32 index); // slot reusable after all frames in flight have passed
    u32 add_cube(VkImageView view);
    void remove_cube(u32 index);

    // Applies queued writes to this slot's set. Call after the slot's GPU work has completed and before
    // submitting commands that use the set (may be called again later in the same frame).
    void flush(u32 slot);
    // Applies queued writes to every slot. Only valid while the GPU is idle (e.g. during loading).
    void flush_all_idle();

    VkDescriptorSetLayout layout() const { return layout_; }
    VkDescriptorSet set(u32 slot) const { return sets_[slot]; }
    VkSampler sampler(SamplerId id) const { return samplers_[static_cast<u32>(id)]; }

  private:
    struct PendingWrite {
        u32 binding;
        u32 index;
        VkImageView view;
        VkImageLayout layout;
        u32 applied_mask; // bit per slot
    };
    void queue(u32 binding, u32 index, VkImageView view, VkImageLayout layout);
    u32 alloc_index(std::vector<u32>& free_list, u32& next, u32 max);

    const Device& dev_;
    u32 frames_;
    VkDescriptorSetLayout layout_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> sets_;
    VkSampler samplers_[static_cast<u32>(SamplerId::Count)] = {};
    std::vector<PendingWrite> pending_;
    std::vector<u32> free_textures_, free_cubes_;
    u32 next_texture_ = 0, next_cube_ = 0;
    u32 max_textures_ = kMaxTextures;
};

} // namespace tefra::rhi
