#include "tefra/renderer/testpattern.h"

#include "tefra/rhi/device.h"
#include "tefra/rhi/gpu_timer.h"

namespace tefra::render {
namespace {

// Must match shaders/testpattern.frag. 112 bytes (limit: 128).
struct Push {
    mat4 inv_view_proj; // camera-relative
    vec4 camera;        // xyz = world position (only y is used by the ground plane), w = time
    vec4 sun;           // xyz = direction towards the sun, w = exposure
    vec4 params;        // x = 1 m checker scale, yzw reserved
};
static_assert(sizeof(Push) == 112);

} // namespace

TestPattern::TestPattern(const rhi::Device& dev, VkFormat color_format, VkImageUsageFlags target_usage)
    : dev_(dev), format_(color_format), usage_(target_usage) {
    layout_ = rhi::create_pipeline_layout(dev_, {}, sizeof(Push));
}

TestPattern::~TestPattern() {
    if (pipeline_) vkDestroyPipeline(dev_.device(), pipeline_, nullptr);
    if (layout_) vkDestroyPipelineLayout(dev_.device(), layout_, nullptr);
    rhi::destroy_render_pass(dev_, rp_);
}

void TestPattern::record(VkCommandBuffer cmd, rhi::GpuTimer* timer, VkImageView target, VkExtent2D extent,
                         const TestPatternParams& p) {
    if (!rp_.pass || rp_.extent.width != extent.width || rp_.extent.height != extent.height) {
        // Render pass objects are bound to the extent; recreate on resize. The pipeline only depends on
        // render pass compatibility (format), so it survives.
        const bool first = rp_.pass == VK_NULL_HANDLE;
        rhi::destroy_render_pass(dev_, rp_);
        rhi::AttachmentDesc color;
        color.format = format_;
        color.usage = usage_;
        color.load = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color.store = VK_ATTACHMENT_STORE_OP_STORE;
        color.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        rhi::RenderPassDesc rpd;
        rpd.colors = {&color, 1};
        rp_ = rhi::create_render_pass(dev_, rpd, extent, "testpattern");
        if (first) {
            VkShaderModule vs = rhi::load_shader(dev_, "fullscreen.vert");
            VkShaderModule fs = rhi::load_shader(dev_, "testpattern.frag");
            rhi::GraphicsPipelineDesc gd;
            gd.vs = vs;
            gd.fs = fs;
            gd.layout = layout_;
            gd.render_pass = rp_.pass;
            gd.name = "testpattern";
            pipeline_ = rhi::create_graphics_pipeline(dev_, gd);
            vkDestroyShaderModule(dev_.device(), vs, nullptr);
            vkDestroyShaderModule(dev_.device(), fs, nullptr);
        }
    }

    const f32 aspect = static_cast<f32>(extent.width) / static_cast<f32>(extent.height);
    const mat4 view_proj = p.camera.projection(aspect) * p.camera.view_rotation();
    Push push;
    push.inv_view_proj = inverse(view_proj);
    push.camera = vec4(static_cast<f32>(p.camera.position.x), static_cast<f32>(p.camera.position.y),
                       static_cast<f32>(p.camera.position.z), p.time);
    push.sun = vec4(normalize(p.sun_direction), p.exposure);
    push.params = vec4(1.0f, 0.0f, 0.0f, 0.0f);

    const u32 scope = timer ? timer->begin(cmd, "testpattern") : ~0u;
    const VkImageView views[] = {target};
    rhi::cmd_begin_render_pass(cmd, rp_, views, {});
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdPushConstants(cmd, layout_, VK_SHADER_STAGE_ALL, 0, sizeof(Push), &push);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    rhi::cmd_end_render_pass(cmd);
    if (timer) timer->end(cmd, scope);
}

} // namespace tefra::render
