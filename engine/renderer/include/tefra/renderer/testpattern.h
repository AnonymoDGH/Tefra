#pragma once

// M0 test scene: analytic sky + 1 m checker ground with distance haze, drawn by one fullscreen pass.
// Validates shader -> pipeline -> render pass -> present/readback, plus axis and scale conventions.

#include "tefra/renderer/camera.h"
#include "tefra/rhi/pipeline.h"

namespace tefra::rhi {
class Device;
class GpuTimer;
} // namespace tefra::rhi

namespace tefra::render {

struct TestPatternParams {
    Camera camera;
    vec3 sun_direction = normalize(vec3{0.35f, 0.17f, -0.92f}); // towards the sun
    f32 time = 0.0f;
    f32 exposure = 1.0f;
};

class TestPattern {
  public:
    // target_usage must equal the usage flags of the images passed to record().
    TestPattern(const rhi::Device& dev, VkFormat color_format, VkImageUsageFlags target_usage);
    ~TestPattern();
    TestPattern(const TestPattern&) = delete;
    TestPattern& operator=(const TestPattern&) = delete;

    // Target must be in COLOR_ATTACHMENT_OPTIMAL; it stays there.
    void record(VkCommandBuffer cmd, rhi::GpuTimer* timer, VkImageView target, VkExtent2D extent,
                const TestPatternParams& params);
    u32 draw_calls_last_frame() const { return 1; }

  private:
    const rhi::Device& dev_;
    VkFormat format_;
    VkImageUsageFlags usage_;
    rhi::RenderPassObjects rp_;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
};

} // namespace tefra::render
