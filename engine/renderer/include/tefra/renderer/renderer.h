#pragma once

// Deferred renderer (spec 1 §5): shadow cascades -> G-buffer -> lighting (sun + IBL) -> AgX tonemap.

#include "tefra/renderer/camera.h"
#include "tefra/renderer/gpu_types.h"
#include "tefra/rendergraph/graph.h"
#include "tefra/rhi/pipeline.h"
#include "tefra/rhi/resources.h"

#include <vector>

namespace tefra::rhi {
class Device;
class BindlessHeap;
class GpuTimer;
} // namespace tefra::rhi

namespace tefra::render {

class Scene;
class Environment;

struct RenderSettings {
    u32 shadow_resolution = 1024; // per cascade
    u32 cascades = 2;             // 1..4
    f32 shadow_distance = 80.0f;  // m
    f32 ev100 = 14.0f;
    vec3 sun_direction = normalize(vec3{0.35f, 0.17f, -0.92f}); // towards the sun
    vec3 sun_color{1.0f, 0.86f, 0.70f};                         // normalized to luminance 1 internally
    f32 sun_illuminance = 30000.0f;                             // lux
    f32 ibl_intensity = 1.0f;
};

struct OutputTarget {
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{};
    VkImageUsageFlags usage = 0;
    VkImageLayout final_layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkPipelineStageFlags2 initial_stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkAccessFlags2 initial_access = VK_ACCESS_2_NONE;
};

class Renderer {
  public:
    Renderer(const rhi::Device& dev, rhi::BindlessHeap& heap, u32 frames_in_flight);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Records the whole frame into cmd. Output ends in target.final_layout.
    void render(VkCommandBuffer cmd, u32 slot, u64 frame_index, rhi::GpuTimer* timer, const Scene& scene,
                const Environment& env, const Camera& camera, f32 time, const RenderSettings& settings,
                const OutputTarget& target);

    u32 draw_calls() const { return draw_calls_; }
    const rg::Graph& graph() const { return graph_; }

  private:
    struct FrameBuffers {
        rhi::Buffer constants;
        rhi::Buffer instances;
        u32 instance_capacity = 0;
    };
    void create_pipelines(VkFormat output_format);
    void compute_cascades(const Camera& camera, f32 aspect, const RenderSettings& s, FrameConstants& fc,
                          vec4 spheres[kMaxCascades]) const;

    const rhi::Device& dev_;
    rhi::BindlessHeap& heap_;
    rg::Graph graph_;
    std::vector<FrameBuffers> frames_;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline p_gbuffer_ = VK_NULL_HANDLE, p_gbuffer_2s_ = VK_NULL_HANDLE, p_shadow_ = VK_NULL_HANDLE;
    VkPipeline p_lighting_ = VK_NULL_HANDLE, p_tonemap_ = VK_NULL_HANDLE;
    VkFormat output_format_ = VK_FORMAT_UNDEFINED;
    VkFormat hdr_format_ = VK_FORMAT_B10G11R11_UFLOAT_PACK32;
    VkFormat shadow_format_ = VK_FORMAT_D32_SFLOAT;
    u32 draw_calls_ = 0;
};

} // namespace tefra::render
