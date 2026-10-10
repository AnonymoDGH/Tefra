#pragma once

// Deferred renderer (spec 1 §5): shadow cascades -> G-buffer -> lighting (sun + IBL) -> AgX tonemap.

#include "tefra/renderer/camera.h"
#include "tefra/renderer/gpu_types.h"
#include "tefra/rendergraph/graph.h"
#include "tefra/rhi/pipeline.h"
#include "tefra/rhi/resources.h"

#include <memory>
#include <vector>

namespace tefra::rhi {
class Device;
class BindlessHeap;
class GpuTimer;
} // namespace tefra::rhi

namespace tefra::render {

class Scene;
class Environment;
class Atmosphere;
class Terrain;
class Vegetation;
class Water;
class CanopyMap;

// Everything the renderer draws in a frame (optional parts may be null).
struct RenderWorld {
    const Scene* scene = nullptr;
    const Environment* env = nullptr;
    const Atmosphere* atmosphere = nullptr;
    const Terrain* terrain = nullptr;
    const Vegetation* vegetation = nullptr;
    const Water* water = nullptr;
};

struct RenderSettings {
    u32 shadow_resolution = 1024; // per cascade
    u32 cascades = 2;             // 1..4
    f32 shadow_distance = 80.0f;  // m
    f32 ev100 = 14.0f;
    vec3 sun_direction = normalize(vec3{0.35f, 0.17f, -0.92f}); // towards the sun
    vec3 sun_color{1.0f, 0.86f, 0.70f};                         // normalized to luminance 1 internally
    f32 sun_illuminance = 30000.0f;                             // lux
    f32 ibl_intensity = 1.0f;
    // Height fog (spec 2 §2). density = 0 disables.
    f32 fog_density = 0.0f;  // 1/m at fog_height
    f32 fog_falloff = 0.02f; // 1/m
    f32 fog_height = 0.0f;   // m (world y)
    f32 fog_anisotropy = 0.6f;
    bool taa = true;
    bool gtao = true;
    f32 gtao_radius = 1.2f;  // m
    f32 render_scale = 1.0f; // internal resolution / output resolution (TAAU when < 1; requires taa)
    // Sky visibility under the vegetation canopy (canopy.h): ambient light is cut where canopy lies above.
    bool canopy_occlusion = false;
    f32 canopy_strength = 0.85f; // fraction of the ambient light removed under a full canopy
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
    void render(VkCommandBuffer cmd, u32 slot, u64 frame_index, rhi::GpuTimer* timer, const RenderWorld& world,
                const Camera& camera, f32 time, const RenderSettings& settings, const OutputTarget& target);

    u32 draw_calls() const { return draw_calls_; }
    const rg::Graph& graph() const { return graph_; }

  private:
    struct FrameBuffers {
        rhi::Buffer constants;
        rhi::Buffer instances;
        u32 instance_capacity = 0;
        rhi::Buffer terrain; // TerrainGPU at 0, patches at kTerrainPatchOffset
        u32 terrain_capacity = 0;
        rhi::Buffer veg_visible, veg_shadow_visible, veg_cmds, veg_shadow_cmds;
        u32 veg_capacity = 0;
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
    VkPipeline p_skyview_ = VK_NULL_HANDLE, p_terrain_ = VK_NULL_HANDLE, p_terrain_shadow_ = VK_NULL_HANDLE;
    VkPipeline p_veg_ = VK_NULL_HANDLE, p_veg_2s_ = VK_NULL_HANDLE, p_veg_shadow_ = VK_NULL_HANDLE,
               p_veg_cull_ = VK_NULL_HANDLE, p_water_ = VK_NULL_HANDLE;
    rhi::Buffer veg_cmd_template_;
    std::unique_ptr<CanopyMap> canopy_;
    // TAA history (ping-pong), persistent across frames.
    rhi::Image history_[2];
    u32 history_bindless_[2] = {~0u, ~0u};
    VkImageLayout history_layout_[2] = {VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_UNDEFINED};
    u32 history_cur_ = 0;
    bool history_valid_ = false;
    VkExtent2D history_extent_{};
    mat4 prev_view_proj_{};
    dvec3 prev_camera_{};
    bool has_prev_ = false;
    VkPipeline p_taa_ = VK_NULL_HANDLE, p_gtao_ = VK_NULL_HANDLE;
    rg::TextureHandle taa_handles_[2];
    const Vegetation* veg_template_owner_ = nullptr;
    VkFormat output_format_ = VK_FORMAT_UNDEFINED;
    VkFormat hdr_format_ = VK_FORMAT_B10G11R11_UFLOAT_PACK32;
    VkFormat shadow_format_ = VK_FORMAT_D32_SFLOAT;
    u32 draw_calls_ = 0;
};

} // namespace tefra::render
