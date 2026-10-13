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
class Ocean;
class CanopyMap;

// Everything the renderer draws in a frame (optional parts may be null).
struct RenderWorld {
    const Scene* scene = nullptr;
    const Environment* env = nullptr;
    const Atmosphere* atmosphere = nullptr;
    const Terrain* terrain = nullptr;
    const Vegetation* vegetation = nullptr;
    const Water* water = nullptr;
    const Ocean* ocean = nullptr;
    // Camera-facing particles, sorted back to front (world::ParticleSystem). Drawn after the water, before TAA.
    const ParticleGPU* particles = nullptr;
    u32 particle_count = 0;
};

struct RenderSettings {
    u32 shadow_resolution = 1024;  // per cascade
    u32 cascades = 2;              // 1..4
    f32 shadow_distance = 80.0f;   // m
    bool pcss_shadows = true;      // penumbra from blocker distance (sun 0.53 deg); false = fixed PCF
    bool contact_shadows = true;   // short screen-space march towards the sun (grass, stones, trunk flares)
    f32 penumbra_scale = 1.0f;     // artistic multiplier on the physical penumbra
    f32 contact_length = 0.75f;    // m
    bool debug_sun_shadow = false; // diagnostic: output the sun visibility term only
    f32 ev100 = 14.0f;
    vec3 sun_direction = normalize(vec3{0.35f, 0.17f, -0.92f}); // towards the sun
    vec3 sun_color{1.0f, 0.86f, 0.70f};                         // normalized to luminance 1 internally
    f32 sun_illuminance = 30000.0f;                             // lux
    f32 ibl_intensity = 1.0f;
    f32 sun_scale = 1.0f;           // multiplies the sun / moon light (0 = interiors without daylight)
    vec3 ambient{0.0f, 0.0f, 0.0f}; // flat indoor/bounce irradiance (lux): interiors lit by local lights
    u32 max_lights = 64;            // nearest local lights shaded per frame
    // Height fog (spec 2 §2). density = 0 disables.
    f32 fog_density = 0.0f;  // 1/m at fog_height
    f32 fog_falloff = 0.02f; // 1/m
    f32 fog_height = 0.0f;   // m (world y)
    f32 fog_anisotropy = 0.6f;
    // Cumulus layer (clouds.glsl) in the sky and the ocean reflections. coverage 0 = clear sky.
    f32 cloud_coverage = 0.0f;
    f32 cloud_altitude = 1600.0f; // m
    f32 cloud_size = 2200.0f;     // m: size of the cloud features
    f32 cloud_wind = 6.0f;        // m/s drift
    // Volumetric fog and sun shafts (half resolution, 16 steps to 150 m). Needs fog_density > 0 to have an effect.
    bool volumetric_fog = false;
    bool taa = true;
    bool gtao = true;
    f32 gtao_radius = 1.2f; // m
    // Terrain close-up detail (terrain.frag): height-blended layers, parallax occlusion within 16 m, stochastic
    // tiling and low-frequency macro variation. Off = the M2 look (luminance-height blend, two-scale tiling).
    bool terrain_detail = true;
    f32 render_scale = 1.0f; // internal resolution / output resolution (TAAU when < 1; requires taa)
    // Camera post (before the tonemap). Bloom: energy-preserving 6-level dual filter, mixed in at bloom_intensity.
    bool bloom = true;
    f32 bloom_intensity = 0.04f;
    // Auto exposure: log-average luminance of the resolved HDR drives an EV compensation (clamped to +-2 EV),
    // adapted over time and applied as a multiplier in the tonemap.
    bool auto_exposure = false;
    // Lens and film: cos^4 vignette, lateral chromatic aberration (<= 1 px at the corners), luminance-dependent grain.
    bool film_effects = true;
    // Scene-referred grading before AgX (neutral defaults): white balance gains, saturation around the luma and a
    // contrast slope in log2 around middle grey.
    vec3 grade_white_balance{1.0f, 1.0f, 1.0f};
    f32 grade_saturation = 1.0f;
    f32 grade_contrast = 1.0f;
    // Sky visibility under the vegetation canopy (canopy.h): ambient light is cut where canopy lies above.
    bool canopy_occlusion = false;
    f32 canopy_strength = 0.85f; // fraction of the ambient light removed under a full canopy
    // Procedural grass on the terrain (G-buffer pass only, foliage class). Needs a terrain.
    bool grass = true;
    f32 grass_density = 1.5f;       // candidate blades per m^2 = 256 * density (cost scales with it)
    f32 grass_radius = 30.0f;       // m, disc of blades around the camera (max 60; cost ~ radius^2, thinned far out)
    f32 vegetation_distance = 1.0f; // scales vegetation draw / LOD / shadow distances (0.4-1.5)
    bool grass_stats = false;       // count the blades drawn with an extra compute pass; see Renderer::grass_blades()
    // Sky objects (engine/world/sky.h). The defaults are the look before Sky, Atmosphere and Clouds existed.
    // Night: when the sun is below -0.1 (normalized y) and moon_illuminance > 0 the moon is the directional light
    // (sun_direction/sun_illuminance are replaced); moon_direction is the moon's position in the sky.
    bool celestial_shown = true;           // sun and moon discs
    bool moon_visible = true;              // moon disc and moonlight
    f32 moon_phase = 0.5f;                 // 0 new, 0.5 full
    f32 moon_angular_radius = 0.0262f;     // rad
    vec3 moon_direction{0.0f, 1.0f, 0.0f}; // towards the moon
    f32 moon_illuminance = 40.0f;          // lux at full moon (0 = no moonlight)
    f32 star_count = 3000.0f;              // stars over the whole sphere, seen at night
    f32 star_brightness = 1.0f;
    f32 sky_glare = 0.0f;                  // halo around the sun (0 = none)
    vec3 fog_tint{1.0f, 1.0f, 1.0f};       // colour of the height fog
    vec3 cloud_tint{1.0f, 1.0f, 1.0f};     // colour of the cumulus
    f32 cloud_density = 1.0f;              // optical depth multiplier of the cumulus
    vec2 cloud_wind_dir{0.9578f, 0.2873f}; // unit wind direction in world x, z
};

// Quality presets (Roblox-style graphics level): limit the costly features of a RenderSettings to what a GPU tier can
// afford. Low targets 30 fps at 720p on the reference hardware (Intel HD 520 / AMD R5 M330): 60 % internal resolution
// upscaled by TAA, one shadow cascade, no GTAO / volumetric / parallax / bloom, half-distance vegetation, no grass.
enum class Quality : u8 { Low, Medium, High, Ultra };
const char* quality_name(Quality q);
// Applies the preset as a ceiling: features the scene turned off stay off.
void limit_quality(RenderSettings& s, Quality q);

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
    // Grass blades rasterized in the last frame whose statistics were read back (needs grass_stats; the value lags
    // by the number of frames in flight).
    u32 grass_blades() const { return grass_blades_; }
    const rg::Graph& graph() const { return graph_; }

    // Landscape parts (terrain, vegetation, water, ocean) drawn when the RenderWorld given to render() leaves them
    // null: lets a module own the landscape without the host's frame code knowing about it (Tefra Studio's
    // world::EnvironmentSync). Set it again after the parts are rebuilt; reset it ({}) before destroying them.
    void set_landscape(const RenderWorld& parts) { landscape_ = parts; }
    const RenderWorld& landscape() const { return landscape_; }

  private:
    struct FrameBuffers {
        rhi::Buffer constants;
        rhi::Buffer instances;
        u32 instance_capacity = 0;
        rhi::Buffer terrain; // TerrainGPU at 0, patches at kTerrainPatchOffset
        u32 terrain_capacity = 0;
        rhi::Buffer veg_visible, veg_shadow_visible, veg_cmds, veg_shadow_cmds;
        rhi::Buffer particles;
        u32 particle_capacity = 0;
        rhi::Buffer lights;
        u32 light_capacity = 0;
        rhi::Buffer exposure; // ExposureState (auto exposure); per slot so it is fenced like the other buffers
        u32 veg_capacity = 0;
        rhi::Buffer grass_cells;    // visible cell list (host-written each frame)
        rhi::Buffer grass_counter;  // blade counter (GPU), copied to grass_readback by the count pass
        rhi::Buffer grass_readback; // host-visible copy of the counter
        rhi::Buffer skin_palettes;  // joint matrices: current palettes, then the previous ones (mat4 each)
        rhi::Buffer skin_out;       // posed vertices of this frame (Vertex layout)
        rhi::Buffer skin_prev;      // posed vertices with the previous palette (motion vectors)
        u32 skin_joint_capacity = 0;
        u32 skin_vertex_capacity = 0;
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
               p_veg_cull_ = VK_NULL_HANDLE, p_water_ = VK_NULL_HANDLE, p_particles_ = VK_NULL_HANDLE;
    bool cpu_veg_cull_ = false; // legacy AMD drivers: vegetation culled on the CPU (TEFRA_GPU_CULL=0/1 forces)
    rhi::Buffer veg_cmd_template_;
    std::unique_ptr<CanopyMap> canopy_;
    VkPipeline p_grass_ = VK_NULL_HANDLE, p_grass_count_ = VK_NULL_HANDLE;
    VkPipeline p_skin_ = VK_NULL_HANDLE;
    // Previous frame's instance transforms (skinned instances need them for motion vectors).
    struct XformHistory {
        dvec3 position{};
        quat rotation{};
        vec3 scale{1.0f};
    };
    std::vector<XformHistory> instance_history_;
    u32 grass_blades_ = 0;
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
    VkPipeline p_bloom_down_ = VK_NULL_HANDLE, p_bloom_up_ = VK_NULL_HANDLE;
    VkPipeline p_expo_first_ = VK_NULL_HANDLE, p_expo_reduce_ = VK_NULL_HANDLE, p_expo_adapt_ = VK_NULL_HANDLE;
    VkPipeline p_volumetric_ = VK_NULL_HANDLE;
    VkPipeline p_ocean_ = VK_NULL_HANDLE, p_ocean_copy_ = VK_NULL_HANDLE;
    rg::TextureHandle taa_handles_[2];
    u64 veg_template_uid_ = 0; // Vegetation::uid() the draw template was built for
    VkFormat output_format_ = VK_FORMAT_UNDEFINED;
    VkFormat hdr_format_ = VK_FORMAT_B10G11R11_UFLOAT_PACK32;
    VkFormat shadow_format_ = VK_FORMAT_D32_SFLOAT;
    u32 draw_calls_ = 0;
    RenderWorld landscape_; // set_landscape(): only terrain, vegetation, water and ocean are used
};

} // namespace tefra::render
