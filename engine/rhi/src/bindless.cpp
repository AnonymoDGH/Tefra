#include "tefra/rhi/bindless.h"
#include "tefra/rhi/device.h"

#include "tefra/core/math.h"

namespace tefra::rhi {
namespace {

constexpr u32 kBindingTextures = 0;
constexpr u32 kBindingCubes = 1;
constexpr u32 kBindingSamplers = 2;

VkSampler make_sampler(const Device& dev, VkFilter filter, VkSamplerAddressMode address, bool mips, f32 aniso,
                       bool compare) {
    VkSamplerCreateInfo ci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    ci.magFilter = filter;
    ci.minFilter = filter;
    ci.mipmapMode = filter == VK_FILTER_LINEAR ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    ci.addressModeU = address;
    ci.addressModeV = address;
    ci.addressModeW = address;
    ci.maxLod = mips ? VK_LOD_CLAMP_NONE : 0.0f;
    if (aniso > 1.0f) {
        ci.anisotropyEnable = VK_TRUE;
        ci.maxAnisotropy = min(aniso, dev.caps().max_sampler_anisotropy);
    }
    if (compare) {
        ci.compareEnable = VK_TRUE;
        ci.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        ci.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    }
    VkSampler s = VK_NULL_HANDLE;
    TEFRA_VK_CHECK(vkCreateSampler(dev.device(), &ci, nullptr, &s));
    return s;
}

} // namespace

BindlessHeap::BindlessHeap(const Device& dev, u32 frames_in_flight) : dev_(dev), frames_(frames_in_flight) {
    VkPhysicalDeviceDescriptorIndexingProperties dip{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES};
    VkPhysicalDeviceProperties2 p2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    p2.pNext = &dip;
    vkGetPhysicalDeviceProperties2(dev_.physical(), &p2);
    max_textures_ = min<u32>(kMaxTextures, dip.maxDescriptorSetUpdateAfterBindSampledImages - kMaxCubes);
    max_textures_ = min<u32>(max_textures_, dip.maxPerStageDescriptorUpdateAfterBindSampledImages - kMaxCubes);

    samplers_[u32(SamplerId::LinearRepeat)] =
        make_sampler(dev_, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT, true, 1.0f, false);
    samplers_[u32(SamplerId::LinearClamp)] =
        make_sampler(dev_, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false, 1.0f, false);
    samplers_[u32(SamplerId::NearestClamp)] =
        make_sampler(dev_, VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false, 1.0f, false);
    samplers_[u32(SamplerId::AnisoRepeat)] =
        make_sampler(dev_, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT, true, 16.0f, false);
    samplers_[u32(SamplerId::ShadowCompare)] =
        make_sampler(dev_, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, false, 1.0f, true);
    samplers_[u32(SamplerId::LinearClampMip)] =
        make_sampler(dev_, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, true, 1.0f, false);
    samplers_[u32(SamplerId::NearestRepeat)] =
        make_sampler(dev_, VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_REPEAT, false, 1.0f, false);
    samplers_[u32(SamplerId::AnisoClamp)] =
        make_sampler(dev_, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, true, 16.0f, false);

    const VkShaderStageFlags stages = VK_SHADER_STAGE_ALL;
    VkDescriptorSetLayoutBinding bindings[3] = {};
    bindings[0] = {kBindingTextures, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, max_textures_, stages, nullptr};
    bindings[1] = {kBindingCubes, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kMaxCubes, stages, nullptr};
    bindings[2] = {kBindingSamplers, VK_DESCRIPTOR_TYPE_SAMPLER, u32(SamplerId::Count), stages, samplers_};
    const VkDescriptorBindingFlags uab =
        VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
    VkDescriptorBindingFlags flags[3] = {uab, uab, 0};
    VkDescriptorSetLayoutBindingFlagsCreateInfo fci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO};
    fci.bindingCount = 3;
    fci.pBindingFlags = flags;
    VkDescriptorSetLayoutCreateInfo lci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    lci.pNext = &fci;
    lci.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    lci.bindingCount = 3;
    lci.pBindings = bindings;
    TEFRA_VK_CHECK(vkCreateDescriptorSetLayout(dev_.device(), &lci, nullptr, &layout_));

    VkDescriptorPoolSize sizes[2] = {{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, (max_textures_ + kMaxCubes) * frames_},
                                     {VK_DESCRIPTOR_TYPE_SAMPLER, u32(SamplerId::Count) * frames_}};
    VkDescriptorPoolCreateInfo pci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pci.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    pci.maxSets = frames_;
    pci.poolSizeCount = 2;
    pci.pPoolSizes = sizes;
    TEFRA_VK_CHECK(vkCreateDescriptorPool(dev_.device(), &pci, nullptr, &pool_));

    sets_.resize(frames_);
    std::vector<VkDescriptorSetLayout> layouts(frames_, layout_);
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = pool_;
    ai.descriptorSetCount = frames_;
    ai.pSetLayouts = layouts.data();
    TEFRA_VK_CHECK(vkAllocateDescriptorSets(dev_.device(), &ai, sets_.data()));
    for (u32 i = 0; i < frames_; ++i)
        dev_.set_name(VK_OBJECT_TYPE_DESCRIPTOR_SET, reinterpret_cast<u64>(sets_[i]), "bindless");
}

BindlessHeap::~BindlessHeap() {
    vkDestroyDescriptorPool(dev_.device(), pool_, nullptr);
    vkDestroyDescriptorSetLayout(dev_.device(), layout_, nullptr);
    for (VkSampler s : samplers_) vkDestroySampler(dev_.device(), s, nullptr);
}

u32 BindlessHeap::alloc_index(std::vector<u32>& free_list, u32& next, u32 max) {
    if (!free_list.empty()) {
        const u32 i = free_list.back();
        free_list.pop_back();
        return i;
    }
    TEFRA_VERIFY(next < max);
    return next++;
}

void BindlessHeap::queue(u32 binding, u32 index, VkImageView view, VkImageLayout layout) {
    // Replace a queued write for the same slot, so only the latest view is applied.
    for (auto& w : pending_) {
        if (w.binding == binding && w.index == index) {
            w.view = view;
            w.layout = layout;
            w.applied_mask = 0;
            return;
        }
    }
    pending_.push_back({binding, index, view, layout, 0});
}

u32 BindlessHeap::add_texture(VkImageView view, VkImageLayout layout) {
    const u32 i = alloc_index(free_textures_, next_texture_, max_textures_);
    queue(kBindingTextures, i, view, layout);
    return i;
}

void BindlessHeap::update_texture(u32 index, VkImageView view, VkImageLayout layout) {
    queue(kBindingTextures, index, view, layout);
}

void BindlessHeap::remove_texture(u32 index) {
    // Never reuse the error texture; the caller guarantees no in-flight frame still samples `index`
    // (resources are destroyed only after FrameLoop::wait_all or after frames_in_flight frames).
    if (index != kErrorTexture) free_textures_.push_back(index);
}

u32 BindlessHeap::add_cube(VkImageView view) {
    const u32 i = alloc_index(free_cubes_, next_cube_, kMaxCubes);
    queue(kBindingCubes, i, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    return i;
}

void BindlessHeap::remove_cube(u32 index) { free_cubes_.push_back(index); }

void BindlessHeap::flush(u32 slot) {
    if (pending_.empty()) return;
    std::vector<VkDescriptorImageInfo> infos;
    std::vector<VkWriteDescriptorSet> writes;
    infos.reserve(pending_.size());
    writes.reserve(pending_.size());
    const u32 bit = 1u << slot;
    for (auto& w : pending_) {
        if (w.applied_mask & bit) continue;
        infos.push_back({VK_NULL_HANDLE, w.view, w.layout});
        VkWriteDescriptorSet wd{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        wd.dstSet = sets_[slot];
        wd.dstBinding = w.binding;
        wd.dstArrayElement = w.index;
        wd.descriptorCount = 1;
        wd.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        writes.push_back(wd);
        w.applied_mask |= bit;
    }
    for (usize i = 0; i < writes.size(); ++i) writes[i].pImageInfo = &infos[i];
    if (!writes.empty()) vkUpdateDescriptorSets(dev_.device(), static_cast<u32>(writes.size()), writes.data(), 0, nullptr);
    const u32 all = (1u << frames_) - 1u;
    usize keep = 0;
    for (usize i = 0; i < pending_.size(); ++i)
        if (pending_[i].applied_mask != all) pending_[keep++] = pending_[i];
    pending_.resize(keep);
}

void BindlessHeap::flush_all_idle() {
    for (u32 s = 0; s < frames_; ++s) flush(s);
}

} // namespace tefra::rhi
