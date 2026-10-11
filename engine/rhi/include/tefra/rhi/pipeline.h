#pragma once

// Render passes (renderpass2 + imageless framebuffers) and pipelines. Geometry uses vertex pulling via
// buffer device address, so pipelines have no vertex input state.

#include "tefra/rhi/vk.h"

#include <span>

namespace tefra::rhi {

class Device;

// Loads <exe_dir>/shaders/<name>.spv. Panics if missing (shaders are build outputs).
VkShaderModule load_shader(const Device& dev, const char* name);

struct AttachmentDesc {
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImageUsageFlags usage = 0; // must equal the image's usage (imageless framebuffer rule)
    VkAttachmentLoadOp load = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    VkAttachmentStoreOp store = VK_ATTACHMENT_STORE_OP_STORE;
    // Layout before and after the pass; transitions happen through explicit barriers outside.
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

struct RenderPassDesc {
    std::span<const AttachmentDesc> colors;
    const AttachmentDesc* depth = nullptr;
};

struct RenderPassObjects {
    VkRenderPass pass = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE; // imageless, bound to a fixed extent
    VkExtent2D extent{};
    u32 color_count = 0;
    bool has_depth = false;
};

RenderPassObjects create_render_pass(const Device& dev, const RenderPassDesc& desc, VkExtent2D extent,
                                     const char* name = nullptr);
void destroy_render_pass(const Device& dev, RenderPassObjects& rp);

// Imageless framebuffers (Vulkan 1.2 core) are the default. Some legacy drivers (AMD GCN 1.x on Windows,
// Vulkan 1.2.1xx) crash on them; then a classic framebuffer is created per (render pass, views) on first use and
// cached, entries unused for a few frames are destroyed. Device creation decides; TEFRA_IMAGELESS=0/1 overrides.
void set_imageless_framebuffers(bool enabled);
bool imageless_framebuffers();
void framebuffer_cache_next_frame(); // FrameLoop::begin: ages and purges cached framebuffers
void framebuffer_cache_clear();      // after a full GPU wait (views may be recreated with recycled handles)

// Begins the pass with concrete views (colors then depth) and sets a full viewport/scissor.
void cmd_begin_render_pass(VkCommandBuffer cmd, const RenderPassObjects& rp, std::span<const VkImageView> views,
                           std::span<const VkClearValue> clears);
void cmd_end_render_pass(VkCommandBuffer cmd);

struct GraphicsPipelineDesc {
    VkShaderModule vs = VK_NULL_HANDLE;
    VkShaderModule fs = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkRenderPass render_pass = VK_NULL_HANDLE;
    u32 color_count = 1;
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkCullModeFlags cull = VK_CULL_MODE_NONE;
    VkFrontFace front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    VkPolygonMode polygon = VK_POLYGON_MODE_FILL;
    bool depth_test = false;
    bool depth_write = false;
    VkCompareOp depth_compare = VK_COMPARE_OP_GREATER_OR_EQUAL; // reverse-Z
    bool depth_clamp = false;
    bool alpha_blend = false;
    bool premultiplied_blend = false; // src + dst * (1 - src.a)
    const VkSpecializationInfo* specialization = nullptr;
    const char* name = nullptr;
};

VkPipeline create_graphics_pipeline(const Device& dev, const GraphicsPipelineDesc& desc);

VkPipeline create_compute_pipeline(const Device& dev, VkShaderModule cs, VkPipelineLayout layout,
                                   const char* name = nullptr);

// Pipeline layout from push-constant size and descriptor set layouts.
VkPipelineLayout create_pipeline_layout(const Device& dev, std::span<const VkDescriptorSetLayout> sets,
                                        u32 push_constant_bytes);

} // namespace tefra::rhi
