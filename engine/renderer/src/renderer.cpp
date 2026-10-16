#include "tefra/renderer/renderer.h"

#include "tefra/renderer/atmosphere.h"
#include "tefra/renderer/canopy.h"
#include "tefra/renderer/environment.h"
#include "tefra/renderer/ocean.h"
#include "tefra/renderer/scene.h"
#include "tefra/renderer/terrain.h"
#include "tefra/renderer/vegetation.h"
#include "tefra/renderer/water.h"
#include "tefra/rhi/bindless.h"
#include "tefra/rhi/commands.h"
#include "tefra/rhi/device.h"
#include "tefra/rhi/gpu_timer.h"
#include "tefra/rhi/upload.h"

#include "tefra/core/log.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>

namespace tefra::render {
namespace {

constexpr VkFormat kRt0 = VK_FORMAT_R8G8B8A8_SRGB;
constexpr VkFormat kRt1 = VK_FORMAT_A2B10G10R10_UNORM_PACK32;
constexpr VkFormat kRt2 = VK_FORMAT_R8G8B8A8_UNORM;
constexpr VkFormat kRt3 = VK_FORMAT_R16G16_SFLOAT;
constexpr VkFormat kDepth = VK_FORMAT_D32_SFLOAT;

struct MeshPush {
    VkDeviceAddress frame, vertices, instances, materials;
    u32 cascade;
    u32 pad[3];
    VkDeviceAddress prev_vertices; // skinned draws: posed vertices with the previous palette
};
struct SkinPush {
    VkDeviceAddress src, attrs, palette, palette_prev, dst, dst_prev;
    u32 count, pad;
};
// One skinned instance for this frame: its source vertices, palettes and posed output (device addresses).
struct SkinDraw {
    VkDeviceAddress src, attrs, palette, palette_prev, dst, dst_prev;
    u32 count;
};
struct LightingPush {
    VkDeviceAddress frame;
    u32 depth, rt0, rt1, rt2, ao, vol;
    VkDeviceAddress lights;
    u32 light_count, clouds; // clouds: volumetric cloud texture (0xFFFFFFFF = 2D layer)
    VkDeviceAddress tiles;   // per-tile light lists (light_cull.comp)
    u32 tiles_x, history;    // history: last frame's TAA output for SSR and bounce light (0xFFFFFFFF = off)
    u32 fx;                  // bit0: screen-space reflections, bits 8..11: bounce light rays per pixel
};
struct VolumetricPush {
    VkDeviceAddress frame;
    u32 depth;
    u32 pad;
    VkDeviceAddress lights; // nearest-first local lights (volumetric fog halos)
    u32 light_count, pad2;
};
struct TonemapPush {
    VkDeviceAddress frame;
    u32 hdr;
    u32 pad;
};
// Camera post (tonemap.frag). Layout must match the GLSL push block.
struct PostPush {
    VkDeviceAddress frame;
    u32 hdr, bloom;
    VkDeviceAddress exposure;
    u32 flags;
    f32 bloom_mix;
    vec4 grade0; // rgb white balance gains, a saturation
    vec4 grade1; // x contrast (log2 slope around 0.18), yzw unused
};
static_assert(sizeof(PostPush) == 64 && offsetof(PostPush, exposure) == 16 && offsetof(PostPush, flags) == 24 &&
              offsetof(PostPush, grade0) == 32);
// Auto exposure state, one per frame slot (tonemap.frag reads ev; exposure_adapt.comp writes it).
struct ExposureState {
    f32 ev; // EV compensation, clamped to +-kExposureLimitEv
    u32 last_frame;
    u32 valid;
    u32 pad;
};
static_assert(sizeof(ExposureState) == 16);
struct ExposureAdaptPush {
    VkDeviceAddress state;
    u32 log_lum, frame_index;
    f32 key_log2, limit_ev, tau_darken, tau_brighten;
};
static_assert(sizeof(ExposureAdaptPush) == 32 && offsetof(ExposureAdaptPush, key_log2) == 16);
constexpr f32 kExposureKey = 0.18f;        // linear luminance the log-average is mapped to (Reinhard 2002 key)
constexpr f32 kExposureLimitEv = 2.0f;     // compensation clamp (+-EV)
constexpr f32 kExposureTauDarken = 0.4f;   // s: scene got brighter, compensation drops quickly
constexpr f32 kExposureTauBrighten = 1.2f; // s: scene got darker, compensation rises slowly
constexpr f32 kNominalFps = 60.0f;         // the adaptation runs per frame; seconds are converted with this rate
struct BloomPush {
    u32 src, karis; // downsample
};
struct BloomUpPush {
    u32 coarse, fine;
};
constexpr u32 kPushBytes = 128;
static_assert(sizeof(MeshPush) <= kPushBytes && sizeof(LightingPush) <= kPushBytes);

// Grass (grass.vert / grass_count.comp, shaders/common/grass.glsl). The grid is kGrassSide^2 world cells centred on the
// camera; the CPU keeps the cells inside the disc and the frustum.
constexpr f32 kGrassLodStart = 12.0f; // m: beyond this, 5-vertex blades and thinner density
constexpr f32 kGrassWind = 0.12f;     // m: tip displacement at full gust
constexpr f32 kGrassMargin = 1.3f;    // m: cell bounding radius for the frustum test (blade height and slope)
constexpr i32 kGrassHalf = 61;        // cells from the camera cell to each grid edge (max disc radius + margin)
constexpr u32 kGrassSide = 2 * kGrassHalf + 1;
// Local lights: at most kMaxLights per frame, kMaxTileLights per 16x16 tile (light_cull.comp).
constexpr u32 kMaxLights = 1024;
constexpr u32 kLightTile = 16;
constexpr u32 kMaxTileLights = 64;
constexpr u32 kGrassMaxCells = kGrassSide * kGrassSide;
constexpr u32 kGrassBladesPerCell = 256; // candidate blades per 1 m^2 at density 1
struct GrassPush {
    VkDeviceAddress frame, terrain, cells, counter; // 0, 8, 16, 24
    vec4 p0;                                        // 32: disc radius, LOD start, tip wind, blade segments
    i32 grid[4];                                    // 48: grid origin x, side, origin z, blades per cell
    u32 cell_count;                                 // 64: entries in the cell list (count pass)
    u32 pad[3];
};
static_assert(offsetof(GrassPush, p0) == 32 && offsetof(GrassPush, grid) == 48 &&
              offsetof(GrassPush, cell_count) == 64);
static_assert(sizeof(GrassPush) <= kPushBytes);

bool format_supports(const rhi::Device& dev, VkFormat f, VkFormatFeatureFlags need) {
    VkFormatProperties p{};
    vkGetPhysicalDeviceFormatProperties(dev.physical(), f, &p);
    return (p.optimalTilingFeatures & need) == need;
}

// Dummy render pass with matching formats, only used for pipeline compatibility.
rhi::RenderPassObjects compat_pass(const rhi::Device& dev, std::initializer_list<VkFormat> colors, VkFormat depth) {
    std::vector<rhi::AttachmentDesc> c;
    for (VkFormat f : colors) {
        rhi::AttachmentDesc d;
        d.format = f;
        d.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        d.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        c.push_back(d);
    }
    rhi::AttachmentDesc dd;
    dd.format = depth;
    dd.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    dd.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    rhi::RenderPassDesc rpd;
    rpd.colors = c;
    rpd.depth = depth != VK_FORMAT_UNDEFINED ? &dd : nullptr;
    return rhi::create_render_pass(dev, rpd, {1, 1});
}

void store_rows(vec4 rows[3], const mat4& m) {
    for (int r = 0; r < 3; ++r) rows[r] = {m.at(r, 0), m.at(r, 1), m.at(r, 2), m.at(r, 3)};
}

bool sphere_in_frustum(const vec4 planes[6], vec3 c, f32 r) {
    for (int i = 0; i < 6; ++i)
        if (dot(planes[i].xyz(), c) + planes[i].w < -r) return false;
    return true;
}

// Gribb-Hartmann planes from a view-projection matrix (Vulkan depth range, reverse-Z infinite: far plane skipped).
void frustum_planes(const mat4& m, vec4 out[6]) {
    auto row = [&](int r) { return vec4{m.at(r, 0), m.at(r, 1), m.at(r, 2), m.at(r, 3)}; };
    const vec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
    out[0] = r3 + r0;
    out[1] = r3 - r0;
    out[2] = r3 + r1;
    out[3] = r3 - r1;
    out[4] = r2;      // z >= 0
    out[5] = r3 - r2; // z <= w
    for (int i = 0; i < 6; ++i) {
        const f32 l = length(out[i].xyz());
        if (l > 0.0f) out[i] = out[i] * (1.0f / l);
    }
}

} // namespace

Renderer::Renderer(const rhi::Device& dev, rhi::BindlessHeap& heap, u32 frames_in_flight)
    : dev_(dev), heap_(heap), graph_(dev, heap) {
    // Legacy AMD drivers (GCN, Vulkan < 1.3) draw no vegetation from the compute culling path although validation is
    // clean; the CPU fills the same visible lists and indirect commands there instead.
    {
        const rhi::AdapterInfo& a = dev_.adapter();
        cpu_veg_cull_ =
            a.vendor_id == 0x1002 && a.api_version.rfind("1.3", 0) != 0 && a.api_version.rfind("1.4", 0) != 0;
        if (const char* e = std::getenv("TEFRA_GPU_CULL")) cpu_veg_cull_ = e[0] == '0';
        if (cpu_veg_cull_) TEFRA_LOG_INFO("Vegetation: CPU culling (GPU culling disabled for this driver)");
    }
    frames_.resize(frames_in_flight);
    for (auto& f : frames_) {
        f.constants = rhi::create_buffer(dev_, sizeof(FrameConstants), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                         rhi::MemoryUsage::Upload, "frame_constants");
        // Zeroed: valid = 0 makes the first adaptation snap to the measured exposure.
        f.exposure = rhi::create_buffer(dev_, sizeof(ExposureState), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                        rhi::MemoryUsage::Upload, "exposure");
        std::memset(f.exposure.mapped, 0, sizeof(ExposureState));
        f.grass_cells = rhi::create_buffer(dev_, kGrassMaxCells * sizeof(u32), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                           rhi::MemoryUsage::Upload, "grass_cells");
        f.grass_counter = rhi::create_buffer(dev_, sizeof(u32),
                                             VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                                 VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                             rhi::MemoryUsage::GpuOnly, "grass_counter");
        f.grass_readback = rhi::create_buffer(dev_, sizeof(u32), VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                              rhi::MemoryUsage::Readback, "grass_readback");
        if (f.grass_readback.mapped) *static_cast<u32*>(f.grass_readback.mapped) = 0;
    }
    if (!format_supports(dev_, hdr_format_,
                         VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT))
        hdr_format_ = VK_FORMAT_R16G16B16A16_SFLOAT;
    if (!format_supports(dev_, shadow_format_,
                         VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT |
                             VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
        shadow_format_ = VK_FORMAT_D16_UNORM;
    const VkDescriptorSetLayout sets[1] = {heap_.layout()};
    layout_ = rhi::create_pipeline_layout(dev_, sets, kPushBytes);
    canopy_ = std::make_unique<CanopyMap>(dev_, heap_, frames_in_flight);
}

Renderer::~Renderer() {
    dev_.wait_idle();
    for (auto& f : frames_) {
        rhi::destroy_buffer(dev_, f.constants);
        rhi::destroy_buffer(dev_, f.exposure);
        rhi::destroy_buffer(dev_, f.lights);
        rhi::destroy_buffer(dev_, f.light_tiles);
        rhi::destroy_buffer(dev_, f.particles);
        rhi::destroy_buffer(dev_, f.instances);
        rhi::destroy_buffer(dev_, f.terrain);
        for (rhi::Buffer* b : {&f.veg_visible, &f.veg_shadow_visible, &f.veg_cmds, &f.veg_shadow_cmds})
            rhi::destroy_buffer(dev_, *b);
        for (rhi::Buffer* b :
             {&f.grass_cells, &f.grass_counter, &f.grass_readback, &f.skin_palettes, &f.skin_out, &f.skin_prev})
            rhi::destroy_buffer(dev_, *b);
    }
    rhi::destroy_buffer(dev_, veg_cmd_template_);
    for (u32 i = 0; i < 2; ++i) {
        if (history_bindless_[i] != ~0u) heap_.remove_texture(history_bindless_[i]);
        rhi::destroy_image(dev_, history_[i]);
    }
    for (VkPipeline p :
         {p_gbuffer_,        p_gbuffer_2s_,  p_shadow_,   p_lighting_,   p_tonemap_,     p_skyview_,    p_terrain_,
          p_terrain_shadow_, p_veg_,         p_veg_2s_,   p_veg_shadow_, p_veg_cull_,    p_water_,      p_taa_,
          p_gtao_,           p_bloom_down_,  p_bloom_up_, p_expo_first_, p_expo_reduce_, p_expo_adapt_, p_volumetric_,
          p_grass_,          p_grass_count_, p_ocean_,    p_ocean_copy_, p_skin_,        p_particles_,  p_clouds_,
          p_light_cull_,     p_rain_})
        if (p) vkDestroyPipeline(dev_.device(), p, nullptr);
    vkDestroyPipelineLayout(dev_.device(), layout_, nullptr);
}

void Renderer::create_pipelines(VkFormat output_format) {
    for (VkPipeline* p : {&p_gbuffer_,    &p_gbuffer_2s_, &p_shadow_,         &p_lighting_,    &p_tonemap_,
                          &p_skyview_,    &p_terrain_,    &p_terrain_shadow_, &p_veg_,         &p_veg_2s_,
                          &p_veg_shadow_, &p_veg_cull_,   &p_water_,          &p_taa_,         &p_gtao_,
                          &p_bloom_down_, &p_bloom_up_,   &p_expo_first_,     &p_expo_reduce_, &p_expo_adapt_,
                          &p_volumetric_, &p_grass_,      &p_grass_count_,    &p_ocean_,       &p_ocean_copy_,
                          &p_skin_,       &p_particles_,  &p_clouds_,         &p_light_cull_,  &p_rain_}) {
        if (*p) vkDestroyPipeline(dev_.device(), *p, nullptr);
        *p = VK_NULL_HANDLE;
    }
    output_format_ = output_format;
    VkShaderModule mesh_vs = rhi::load_shader(dev_, "mesh.vert");
    VkShaderModule gbuf_fs = rhi::load_shader(dev_, "gbuffer.frag");
    VkShaderModule shadow_vs = rhi::load_shader(dev_, "shadow.vert");
    VkShaderModule fs_vs = rhi::load_shader(dev_, "fullscreen.vert");
    VkShaderModule light_fs = rhi::load_shader(dev_, "lighting.frag");
    VkShaderModule tone_fs = rhi::load_shader(dev_, "tonemap.frag");
    VkShaderModule sky_fs = rhi::load_shader(dev_, "skyview.frag");
    VkShaderModule terrain_vs = rhi::load_shader(dev_, "terrain.vert");
    VkShaderModule terrain_fs = rhi::load_shader(dev_, "terrain.frag");
    VkShaderModule terrain_shadow_vs = rhi::load_shader(dev_, "terrain_shadow.vert");
    VkShaderModule veg_vs = rhi::load_shader(dev_, "veg.vert");
    VkShaderModule veg_shadow_vs = rhi::load_shader(dev_, "veg_shadow.vert");
    VkShaderModule shadow_alpha_fs = rhi::load_shader(dev_, "shadow_alpha.frag");
    VkShaderModule veg_cull_cs = rhi::load_shader(dev_, "veg_cull.comp");
    VkShaderModule grass_vs = rhi::load_shader(dev_, "grass.vert");
    VkShaderModule grass_fs = rhi::load_shader(dev_, "grass.frag");
    VkShaderModule grass_count_cs = rhi::load_shader(dev_, "grass_count.comp");
    VkShaderModule skin_cs = rhi::load_shader(dev_, "skin.comp");
    VkShaderModule water_vs = rhi::load_shader(dev_, "water.vert");
    VkShaderModule water_fs = rhi::load_shader(dev_, "water.frag");
    VkShaderModule particle_vs = rhi::load_shader(dev_, "particle.vert");
    VkShaderModule particle_fs = rhi::load_shader(dev_, "particle.frag");
    VkShaderModule taa_fs = rhi::load_shader(dev_, "taa.frag");
    VkShaderModule gtao_fs = rhi::load_shader(dev_, "gtao.frag");
    VkShaderModule volumetric_fs = rhi::load_shader(dev_, "volumetric.frag");
    VkShaderModule clouds_fs = rhi::load_shader(dev_, "clouds_vol.frag");
    VkShaderModule light_cull_cs = rhi::load_shader(dev_, "light_cull.comp");
    VkShaderModule rain_vs = rhi::load_shader(dev_, "rain.vert");
    VkShaderModule rain_fs = rhi::load_shader(dev_, "rain.frag");
    VkShaderModule bloom_down_fs = rhi::load_shader(dev_, "bloom_down.frag");
    VkShaderModule bloom_up_fs = rhi::load_shader(dev_, "bloom_up.frag");
    VkShaderModule expo_first_fs = rhi::load_shader(dev_, "exposure_first.frag");
    VkShaderModule expo_reduce_fs = rhi::load_shader(dev_, "exposure_reduce.frag");
    VkShaderModule expo_adapt_cs = rhi::load_shader(dev_, "exposure_adapt.comp");
    p_veg_cull_ = rhi::create_compute_pipeline(dev_, veg_cull_cs, layout_, "veg_cull");
    p_light_cull_ = rhi::create_compute_pipeline(dev_, light_cull_cs, layout_, "light_cull");
    p_grass_count_ = rhi::create_compute_pipeline(dev_, grass_count_cs, layout_, "grass_count");
    p_skin_ = rhi::create_compute_pipeline(dev_, skin_cs, layout_, "skin");

    auto rp_g = compat_pass(dev_, {kRt0, kRt1, kRt2, kRt3}, kDepth);
    rhi::GraphicsPipelineDesc g;
    g.vs = mesh_vs;
    g.fs = gbuf_fs;
    g.layout = layout_;
    g.render_pass = rp_g.pass;
    g.color_count = 4;
    g.cull = VK_CULL_MODE_BACK_BIT;
    g.depth_test = true;
    g.depth_write = true;
    g.depth_compare = VK_COMPARE_OP_GREATER_OR_EQUAL;
    g.name = "gbuffer";
    p_gbuffer_ = rhi::create_graphics_pipeline(dev_, g);
    g.cull = VK_CULL_MODE_NONE;
    g.name = "gbuffer_two_sided";
    p_gbuffer_2s_ = rhi::create_graphics_pipeline(dev_, g);
    g.vs = terrain_vs;
    g.fs = terrain_fs;
    g.cull = VK_CULL_MODE_BACK_BIT;
    g.front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    g.name = "terrain";
    p_terrain_ = rhi::create_graphics_pipeline(dev_, g);
    g.vs = veg_vs;
    g.fs = gbuf_fs;
    g.name = "vegetation";
    p_veg_ = rhi::create_graphics_pipeline(dev_, g);
    g.cull = VK_CULL_MODE_NONE;
    g.name = "vegetation_two_sided";
    p_veg_2s_ = rhi::create_graphics_pipeline(dev_, g);
    // Grass: no vertex input, two-sided ribbons (front = CCW side, see grass.vert).
    g.vs = grass_vs;
    g.fs = grass_fs;
    g.name = "grass";
    p_grass_ = rhi::create_graphics_pipeline(dev_, g);

    auto rp_s = compat_pass(dev_, {}, shadow_format_);
    rhi::GraphicsPipelineDesc s;
    s.vs = shadow_vs;
    s.layout = layout_;
    s.render_pass = rp_s.pass;
    s.color_count = 0;
    s.cull = VK_CULL_MODE_NONE; // both faces: thin geometry and fewer light leaks
    s.depth_test = true;
    s.depth_write = true;
    s.depth_compare = VK_COMPARE_OP_LESS_OR_EQUAL;
    s.depth_clamp = true; // casters behind the near plane still cast (pancaking)
    s.name = "shadow";
    p_shadow_ = rhi::create_graphics_pipeline(dev_, s);
    s.vs = terrain_shadow_vs;
    s.name = "terrain_shadow";
    p_terrain_shadow_ = rhi::create_graphics_pipeline(dev_, s);
    s.vs = veg_shadow_vs;
    s.fs = shadow_alpha_fs;
    s.name = "vegetation_shadow";
    p_veg_shadow_ = rhi::create_graphics_pipeline(dev_, s);

    auto rp_l = compat_pass(dev_, {hdr_format_}, VK_FORMAT_UNDEFINED);
    rhi::GraphicsPipelineDesc l;
    l.vs = fs_vs;
    l.fs = light_fs;
    l.layout = layout_;
    l.render_pass = rp_l.pass;
    l.name = "lighting";
    p_lighting_ = rhi::create_graphics_pipeline(dev_, l);

    auto rp_w = compat_pass(dev_, {hdr_format_}, kDepth);
    rhi::GraphicsPipelineDesc w;
    w.vs = water_vs;
    w.fs = water_fs;
    w.layout = layout_;
    w.render_pass = rp_w.pass;
    w.depth_test = true;
    w.depth_write = false;
    w.depth_compare = VK_COMPARE_OP_GREATER_OR_EQUAL;
    w.premultiplied_blend = true;
    w.name = "water";
    p_water_ = rhi::create_graphics_pipeline(dev_, w);
    auto rp_p = compat_pass(dev_, {hdr_format_, VK_FORMAT_R8_UNORM}, kDepth);
    w.vs = particle_vs;
    w.fs = particle_fs;
    w.render_pass = rp_p.pass;
    w.color_count = 2;
    w.name = "particles";
    p_particles_ = rhi::create_graphics_pipeline(dev_, w);
    w.vs = rain_vs;
    w.fs = rain_fs;
    w.name = "rain";
    p_rain_ = rhi::create_graphics_pipeline(dev_, w);

    auto rp_a = compat_pass(dev_, {VK_FORMAT_R16G16B16A16_SFLOAT}, VK_FORMAT_UNDEFINED);
    rhi::GraphicsPipelineDesc ta;
    ta.vs = fs_vs;
    ta.fs = taa_fs;
    ta.layout = layout_;
    ta.render_pass = rp_a.pass;
    ta.name = "taa";
    p_taa_ = rhi::create_graphics_pipeline(dev_, ta);
    auto rp_o = compat_pass(dev_, {VK_FORMAT_R8_UNORM}, VK_FORMAT_UNDEFINED);
    ta.fs = gtao_fs;
    ta.render_pass = rp_o.pass;
    ta.name = "gtao";
    p_gtao_ = rhi::create_graphics_pipeline(dev_, ta);

    auto rp_v = compat_pass(dev_, {VK_FORMAT_R16G16B16A16_SFLOAT}, VK_FORMAT_UNDEFINED);
    ta.fs = volumetric_fs;
    ta.render_pass = rp_v.pass;
    ta.name = "volumetric";
    p_volumetric_ = rhi::create_graphics_pipeline(dev_, ta);
    ta.fs = clouds_fs;
    ta.name = "clouds";
    p_clouds_ = rhi::create_graphics_pipeline(dev_, ta);

    auto rp_b = compat_pass(dev_, {hdr_format_}, VK_FORMAT_UNDEFINED);
    rhi::GraphicsPipelineDesc bd;
    bd.vs = fs_vs;
    bd.fs = bloom_down_fs;
    bd.layout = layout_;
    bd.render_pass = rp_b.pass;
    bd.name = "bloom_down";
    p_bloom_down_ = rhi::create_graphics_pipeline(dev_, bd);
    bd.fs = bloom_up_fs;
    bd.name = "bloom_up";
    p_bloom_up_ = rhi::create_graphics_pipeline(dev_, bd);

    auto rp_e = compat_pass(dev_, {VK_FORMAT_R16_SFLOAT}, VK_FORMAT_UNDEFINED);
    rhi::GraphicsPipelineDesc ed;
    ed.vs = fs_vs;
    ed.layout = layout_;
    ed.render_pass = rp_e.pass;
    ed.fs = expo_first_fs;
    ed.name = "exposure_first";
    p_expo_first_ = rhi::create_graphics_pipeline(dev_, ed);
    ed.fs = expo_reduce_fs;
    ed.name = "exposure_reduce";
    p_expo_reduce_ = rhi::create_graphics_pipeline(dev_, ed);
    p_expo_adapt_ = rhi::create_compute_pipeline(dev_, expo_adapt_cs, layout_, "exposure_adapt");

    auto rp_t = compat_pass(dev_, {output_format}, VK_FORMAT_UNDEFINED);
    l.fs = tone_fs;
    l.render_pass = rp_t.pass;
    l.name = "tonemap";
    p_tonemap_ = rhi::create_graphics_pipeline(dev_, l);

    auto rp_k = compat_pass(dev_, {VK_FORMAT_R16G16B16A16_SFLOAT}, VK_FORMAT_UNDEFINED);
    l.fs = sky_fs;
    l.render_pass = rp_k.pass;
    l.name = "skyview";
    p_skyview_ = rhi::create_graphics_pipeline(dev_, l);

    // Ocean pass: copy of the lit image, then the displaced surface (depth-tested against the scene, opaque).
    VkShaderModule ocean_vs = rhi::load_shader(dev_, "ocean.vert");
    VkShaderModule ocean_fs = rhi::load_shader(dev_, "ocean.frag");
    VkShaderModule ocean_copy_fs = rhi::load_shader(dev_, "ocean_copy.frag");
    rhi::GraphicsPipelineDesc oc;
    oc.vs = fs_vs;
    oc.fs = ocean_copy_fs;
    oc.layout = layout_;
    oc.render_pass = rp_w.pass;
    oc.name = "ocean_copy";
    p_ocean_copy_ = rhi::create_graphics_pipeline(dev_, oc);
    oc.vs = ocean_vs;
    oc.fs = ocean_fs;
    oc.depth_test = true;
    oc.depth_write = false;
    oc.depth_compare = VK_COMPARE_OP_GREATER_OR_EQUAL;
    oc.name = "ocean";
    p_ocean_ = rhi::create_graphics_pipeline(dev_, oc);
    for (VkShaderModule m : {ocean_vs, ocean_fs, ocean_copy_fs}) vkDestroyShaderModule(dev_.device(), m, nullptr);

    for (auto* rp : {&rp_g, &rp_s, &rp_l, &rp_t, &rp_k, &rp_w, &rp_p, &rp_a, &rp_o, &rp_b, &rp_e, &rp_v})
        rhi::destroy_render_pass(dev_, *rp);
    for (VkShaderModule m : {mesh_vs,       gbuf_fs,       shadow_vs,       fs_vs,          light_fs,
                             tone_fs,       sky_fs,        terrain_vs,      terrain_fs,     terrain_shadow_vs,
                             veg_vs,        veg_shadow_vs, shadow_alpha_fs, veg_cull_cs,    water_vs,
                             water_fs,      particle_vs,   particle_fs,     taa_fs,         gtao_fs,
                             bloom_down_fs, bloom_up_fs,   expo_first_fs,   expo_reduce_fs, expo_adapt_cs,
                             volumetric_fs, grass_vs,      grass_fs,        grass_count_cs, skin_cs,
                             clouds_fs,     light_cull_cs, rain_vs,         rain_fs})
        vkDestroyShaderModule(dev_.device(), m, nullptr);
}

void Renderer::compute_cascades(const Camera& cam, f32 aspect, const RenderSettings& s, FrameConstants& fc,
                                vec4 spheres[kMaxCascades]) const {
    const u32 n = clamp<u32>(s.cascades, 1u, kMaxCascades);
    const f32 z_near_ = cam.z_near, z_far_ = s.shadow_distance;
    const f32 lambda = 0.75f;
    f32 splits[kMaxCascades + 1];
    splits[0] = z_near_;
    for (u32 i = 1; i <= n; ++i) {
        const f32 t = f32(i) / f32(n);
        splits[i] =
            lambda * z_near_ * std::pow(z_far_ / z_near_, t) + (1.0f - lambda) * (z_near_ + (z_far_ - z_near_) * t);
    }
    const f32 tan_y = std::tan(cam.fovy * 0.5f), tan_x = tan_y * aspect;
    const vec3 fwd = cam.forward(), right = cam.right(), up = cross(right, fwd);
    const vec3 L = normalize(s.sun_direction);
    const mat4 light_rot = look_at(vec3{}, -L, std::fabs(L.y) > 0.99f ? vec3{1, 0, 0} : vec3{0, 1, 0});
    const f32 res = f32(s.shadow_resolution);
    for (u32 c = 0; c < n; ++c) {
        vec3 corners[8];
        u32 k = 0;
        for (f32 d : {splits[c], splits[c + 1]})
            for (f32 sy : {-1.0f, 1.0f})
                for (f32 sx : {-1.0f, 1.0f}) corners[k++] = fwd * d + right * (sx * tan_x * d) + up * (sy * tan_y * d);
        vec3 center{};
        for (const vec3& p : corners) center += p;
        center = center * (1.0f / 8.0f);
        f32 r = 0.0f;
        for (const vec3& p : corners) r = max(r, length(p - center));
        r = std::ceil(r * 16.0f) / 16.0f; // quantized radius: stable texel size while the camera rotates
        // Snap the center to the shadow texel grid, in world space (camera-relative positions shift with the
        // camera, so snap the absolute world position).
        const vec3 world_center = center + vec3{f32(cam.position.x), f32(cam.position.y), f32(cam.position.z)};
        vec3 cl = transform_point(light_rot, world_center);
        const f32 texel = 2.0f * r / res;
        cl.x = std::floor(cl.x / texel) * texel;
        cl.y = std::floor(cl.y / texel) * texel;
        const mat4 inv_rot = transpose(light_rot);
        const vec3 snapped_world = transform_point(inv_rot, cl);
        const vec3 snapped_rel = snapped_world - vec3{f32(cam.position.x), f32(cam.position.y), f32(cam.position.z)};
        const f32 zn = r + 400.0f; // casters up to 400 m towards the sun
        mat4 ortho;
        ortho.c[0] = {1.0f / r, 0, 0, 0};
        ortho.c[1] = {0, 1.0f / r, 0, 0};
        ortho.c[2] = {0, 0, -1.0f / (zn + r), 0};
        ortho.c[3] = {0, 0, zn / (zn + r), 1};
        fc.cascade_view_proj[c] = ortho * translate(-transform_point(light_rot, snapped_rel)) * light_rot;
        fc.cascade_splits[static_cast<int>(c)] = splits[c + 1];
        fc.cascade_atlas[c] = {f32(c) / f32(n), 0.0f, 1.0f / f32(n), 1.0f};
        spheres[c] = vec4(snapped_rel, r);
    }
    for (u32 c = n; c < kMaxCascades; ++c) fc.cascade_splits[static_cast<int>(c)] = 1e30f;
    fc.shadow_params = {1.0f / (res * f32(n)), 1.0f / res, f32(n), res};
    fc.veg_lod_scale = clamp(s.vegetation_distance, 0.2f, 2.0f);
    fc.shadow_flags = (s.pcss_shadows ? 1u : 0u) | (s.contact_shadows ? 2u : 0u) | (s.debug_sun_shadow ? 4u : 0u);
    fc.shadow_soft = vec4{s.penumbra_scale, s.contact_length, 0.0f, 0.0f};
}

void Renderer::render(VkCommandBuffer cmd, u32 slot, u64 frame_index, rhi::GpuTimer* timer, const RenderWorld& world_in,
                      const Camera& camera, f32 time, const RenderSettings& settings_in, const OutputTarget& target) {
    RenderWorld world = world_in;
    if (!world.terrain) world.terrain = landscape_.terrain;
    if (!world.vegetation) world.vegetation = landscape_.vegetation;
    if (!world.water) world.water = landscape_.water;
    if (!world.ocean) world.ocean = landscape_.ocean;
    const Scene& scene = *world.scene;
    const Environment& env = *world.env;
    const Atmosphere* atmo = world.atmosphere;
    const Terrain* terrain = world.terrain;
    const Vegetation* veg = world.vegetation;
    if (output_format_ != target.format) create_pipelines(target.format);
    RenderSettings s = settings_in;
    const vec3 sun_true = normalize(s.sun_direction);
    if (atmo) {
        // Sun light at the camera from the atmosphere: color and intensity change with the sun elevation.
        const f32 alt = atmo->params().ground_altitude + max(static_cast<f32>(camera.position.y), 0.0f);
        const vec3 e = atmo->sun_illuminance(alt, sun_true);
        const f32 lum = 0.2126f * e.x + 0.7152f * e.y + 0.0722f * e.z;
        s.sun_illuminance = lum;
        s.sun_color = lum > 0.0f ? e / lum : vec3{1.0f};
    }
    // Night: below the horizon the moon is the directional light (shadows included). The sky itself still comes
    // from the real sun (sun_true), which is below the horizon and draws no light.
    constexpr f32 kMoonBelowSun = -0.1f;
    if (s.moon_illuminance > 0.0f && sun_true.y < kMoonBelowSun) {
        s.sun_direction = normalize(s.moon_direction);
        s.sun_illuminance = s.moon_illuminance;
        s.sun_color = vec3{0.80f, 0.88f, 1.0f};
    }
    s.sun_illuminance *= max(s.sun_scale, 0.0f);
    FrameBuffers& fb = frames_[slot];
    // The slot's fence has been waited on, so last frame's grass counter copy in this slot is complete.
    if (s.grass_stats && fb.grass_readback.mapped) {
        vmaInvalidateAllocation(dev_.allocator(), fb.grass_readback.allocation, 0, VK_WHOLE_SIZE);
        grass_blades_ = *static_cast<const u32*>(fb.grass_readback.mapped);
    }
    const VkExtent2D out_ext = target.extent;
    const f32 scale = s.taa ? clamp(s.render_scale, 0.5f, 1.0f) : 1.0f;
    // Internal (render) resolution; the TAA pass reconstructs the output resolution.
    const VkExtent2D ext{max(1u, static_cast<u32>(std::lround(f32(out_ext.width) * scale))),
                         max(1u, static_cast<u32>(std::lround(f32(out_ext.height) * scale)))};
    const f32 aspect = f32(ext.width) / f32(ext.height);

    TEFRA_TRACE("render: per-frame constants");
    // ---- Per-frame constants ---------------------------------------------------------------------
    FrameConstants fc{};
    fc.view = camera.view_rotation();
    fc.proj = camera.projection(aspect);
    const mat4 unjittered_vp = fc.proj * fc.view;
    // TAA: sub-pixel Halton(2,3) jitter applied to the projection.
    auto halton = [](u32 i, u32 b) {
        f32 f = 1.0f, r = 0.0f;
        while (i > 0) {
            f /= f32(b);
            r += f * f32(i % b);
            i /= b;
        }
        return r;
    };
    vec2 jitter{};
    if (s.taa) {
        const u32 hi = static_cast<u32>(frame_index % 8) + 1;
        jitter = {(halton(hi, 2) - 0.5f) * 2.0f / f32(ext.width), (halton(hi, 3) - 0.5f) * 2.0f / f32(ext.height)};
        fc.proj.c[2].x -= jitter.x; // ndc.x += jitter.x (clip.w = -z)
        fc.proj.c[2].y -= jitter.y;
    }
    fc.view_proj = fc.proj * fc.view;
    fc.inv_view_proj = inverse(fc.view_proj);
    fc.jitter = {jitter.x, jitter.y, 0.0f, 0.0f};
    // Previous frame's view-projection, re-expressed for the current camera-relative positions.
    const vec3 cam_delta = relative_to(camera.position, prev_camera_);
    if (has_prev_)
        fc.prev_view_proj = prev_view_proj_ * translate(cam_delta);
    else
        fc.prev_view_proj = unjittered_vp;
    const bool camera_cut = has_prev_ && length(cam_delta) > 20.0f;
    prev_view_proj_ = unjittered_vp;
    prev_camera_ = camera.position;
    has_prev_ = true;
    vec4 cascade_spheres[kMaxCascades];
    const u32 cascades = clamp<u32>(s.cascades, 1u, kMaxCascades);
    compute_cascades(camera, aspect, s, fc, cascade_spheres);
    const vec3 sun_col = s.sun_color / (0.2126f * s.sun_color.x + 0.7152f * s.sun_color.y + 0.0722f * s.sun_color.z);
    fc.sun_dir = vec4(normalize(s.sun_direction), s.sun_illuminance);
    fc.sun_color = vec4(sun_col, 0.00465f);
    fc.camera_pos = {f32(camera.position.x), f32(camera.position.y), f32(camera.position.z), time};
    fc.exposure = {1.0f / (1.2f * std::exp2(s.ev100)), s.ibl_intensity, s.ev100, f32(frame_index)};
    fc.viewport = {f32(ext.width), f32(ext.height), 1.0f / f32(ext.width), 1.0f / f32(ext.height)};
    for (int i = 0; i < 9; ++i) fc.sh[i] = env.sh()[i];
    fc.env_cube = env.cube();
    fc.env_mips = env.mips();
    fc.brdf_lut = env.brdf_lut();
    fc.env_background = env.background();
    fc.fog = {s.fog_density, s.fog_falloff, s.fog_height, s.fog_anisotropy};
    fc.clouds = {s.cloud_coverage, s.cloud_altitude, s.cloud_size, s.cloud_wind * time};
    {
        const vec3 moon_dir = normalize(s.moon_direction);
        const f32 night = saturate((-sun_true.y - 0.02f) / 0.10f);
        const bool moon_disc = s.celestial_shown && s.moon_visible;
        fc.celestial = {s.celestial_shown ? 1.0f : 0.0f, moon_disc ? 1.0f : 0.0f, s.moon_angular_radius, s.sky_glare};
        fc.moon = {moon_dir.x, moon_dir.y, moon_dir.z, s.moon_phase};
        fc.stars = {s.star_count, s.star_brightness, night, 0.0f};
        fc.fog_tint = {s.fog_tint.x, s.fog_tint.y, s.fog_tint.z, 0.0f};
        fc.cloud_look = {s.cloud_tint.x, s.cloud_tint.y, s.cloud_tint.z, s.cloud_density};
        fc.cloud_wind = {s.cloud_wind_dir.x, s.cloud_wind_dir.y, 0.0f, 0.0f};
        fc.sun_true = {sun_true.x, sun_true.y, sun_true.z, 0.0f};
    }
    fc.ambient = {s.ambient.x, s.ambient.y, s.ambient.z, 0.0f};
    const bool vclouds = s.volumetric_clouds && s.cloud_coverage > 0.0f && atmo != nullptr;
    fc.weather = {clamp(s.wetness, 0.0f, 1.0f), clamp(s.puddles, 0.0f, 1.0f), clamp(s.rain, 0.0f, 1.0f), s.rain_speed};
    fc.rain_wind = {s.rain_wind.x, s.rain_wind.y, s.rain_wind.z, s.rain_length};
    fc.vclouds = {vclouds ? 1.0f : 0.0f, s.cloud_thickness, f32(clamp(s.cloud_steps, 8u, 96u)), 0.0f};
    if (world.ocean) {
        const OceanDesc& od = world.ocean->desc();
        fc.ocean = {od.level, 1.0f, 0.0f, 0.0f};
        fc.water_absorption = vec4(od.absorption, 0.0f);
        fc.water_scattering = vec4(od.scattering, 0.0f);
    }
    // Canopy occlusion map: built on the CPU from vegetation footprints, uploaded for this frame slot if stale.
    if (s.canopy_occlusion && veg && canopy_ && canopy_->supported()) {
        canopy_->update(*veg, camera.position);
        const CanopyBinding cb = canopy_->sync(cmd, slot);
        fc.canopy_map = cb.index;
        fc.canopy = {cb.origin.x, cb.origin.y, cb.size, s.canopy_strength};
    }
    if (atmo)
        fc.atmo = atmo->gpu(sun_true, 0);
    else
        fc.atmo.skyview_lut = 0xFFFFFFFFu;

    TEFRA_TRACE("render: instances");
    // ---- Instances (camera-relative) and culling ---------------------------------------------------
    const auto& instances = scene.instances();
    if (fb.instance_capacity < instances.size() || !fb.instances.buffer) {
        rhi::destroy_buffer(dev_, fb.instances);
        fb.instance_capacity = max<u32>(64u, static_cast<u32>(instances.size()) * 2u);
        fb.instances = rhi::create_buffer(dev_, sizeof(InstanceGPU) * fb.instance_capacity,
                                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, rhi::MemoryUsage::Upload, "instances");
    }
    vec4 planes[6];
    frustum_planes(fc.view_proj, planes);
    std::vector<u32> visible, casters[kMaxCascades];
    struct CasterSphere {
        vec3 c;
        f32 r;
        u32 index;
    };
    std::vector<CasterSphere> caster_spheres; // shadow casters for the local light shadow maps
    auto* gpu_inst = static_cast<InstanceGPU*>(fb.instances.mapped);
    const bool have_history = instance_history_.size() == instances.size();
    std::vector<XformHistory> history(instances.size());
    for (u32 i = 0; i < instances.size(); ++i) {
        const Instance& in = instances[i];
        const vec3 rel = relative_to(in.position, camera.position);
        const mat4 model = compose_trs(rel, in.rotation, in.scale);
        const mat4 normal_m = transpose(inverse(model));
        InstanceGPU g{};
        store_rows(g.model_rows, model);
        store_rows(g.normal_rows, normal_m);
        g.material = in.material;
        // Skinned instances move their vertices with the palette and their root with the transform: the previous
        // frame's model matrix (relative to this frame's camera) gives the motion of the surface.
        const bool dynamic = in.skin != kNoSkin;
        const XformHistory prev =
            have_history ? instance_history_[i] : XformHistory{in.position, in.rotation, in.scale};
        const mat4 prev_model =
            dynamic ? compose_trs(relative_to(prev.position, camera.position), prev.rotation, prev.scale) : model;
        store_rows(g.prev_rows, prev_model);
        g.flags = dynamic ? 1u : 0u;
        history[i] = {in.position, in.rotation, in.scale};
        gpu_inst[i] = g;
        const MeshRange& mr = scene.meshes()[in.mesh];
        const vec3 c = transform_point(model, mr.bounds.center);
        f32 r = mr.bounds.radius * max(max(in.scale.x, in.scale.y), in.scale.z);
        if (dynamic) r = r * 1.5f + 0.5f; // posed vertices can leave the bind-pose sphere
        if (sphere_in_frustum(planes, c, r)) visible.push_back(i);
        if (in.cast_shadows && s.local_shadows > 0) caster_spheres.push_back({c, r, i});
        if (in.cast_shadows)
            for (u32 k = 0; k < cascades; ++k) {
                // Cylinder test along the light direction approximated by sphere distance in the light plane.
                const vec3 d = c - cascade_spheres[k].xyz();
                const vec3 dl = d - fc.sun_dir.xyz() * dot(d, fc.sun_dir.xyz());
                if (length(dl) <= cascade_spheres[k].w + r) casters[k].push_back(i);
            }
    }
    vmaFlushAllocation(dev_.allocator(), fb.instances.allocation, 0, VK_WHOLE_SIZE);
    instance_history_ = std::move(history);

    TEFRA_TRACE("render: skinning");
    // ---- Skinned meshes: palettes (current and previous) and posed vertices (current and previous) ----------------
    std::vector<u32> skin_draw_of(instances.size(), ~0u); // instance -> index into skin_draws
    std::vector<SkinDraw> skin_draws;
    {
        const auto& skins = scene.skins();
        auto skinned = [&](u32 i) {
            const Instance& in = instances[i];
            return in.skin != kNoSkin && in.skin < skins.size() && scene.meshes()[in.mesh].skinned;
        };
        std::vector<u32> palette_base(skins.size());
        u32 joints = 0, verts = 0;
        for (usize k = 0; k < skins.size(); ++k) {
            palette_base[k] = joints;
            joints += skins[k].joint_count;
        }
        for (u32 i = 0; i < instances.size(); ++i)
            if (skinned(i)) verts += scene.meshes()[instances[i].mesh].vertex_count;
        if (joints > 0 && fb.skin_joint_capacity < joints) {
            rhi::destroy_buffer(dev_, fb.skin_palettes);
            fb.skin_joint_capacity = max<u32>(joints, 64u);
            fb.skin_palettes =
                rhi::create_buffer(dev_, 2ull * fb.skin_joint_capacity * sizeof(mat4),
                                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, rhi::MemoryUsage::Upload, "skin_palettes");
        }
        if (verts > 0 && fb.skin_vertex_capacity < verts) {
            rhi::destroy_buffer(dev_, fb.skin_out);
            rhi::destroy_buffer(dev_, fb.skin_prev);
            fb.skin_vertex_capacity = max<u32>(verts, 1024u);
            const VkDeviceSize bytes = VkDeviceSize(fb.skin_vertex_capacity) * sizeof(Vertex);
            fb.skin_out = rhi::create_buffer(dev_, bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, rhi::MemoryUsage::GpuOnly,
                                             "skin_out");
            fb.skin_prev = rhi::create_buffer(dev_, bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                              rhi::MemoryUsage::GpuOnly, "skin_prev");
        }
        if (joints > 0) {
            auto* pal = static_cast<mat4*>(fb.skin_palettes.mapped);
            for (usize k = 0; k < skins.size(); ++k)
                for (u32 j = 0; j < skins[k].joint_count; ++j) {
                    pal[palette_base[k] + j] = skins[k].palette[j];
                    pal[joints + palette_base[k] + j] = skins[k].prev_palette[j];
                }
            vmaFlushAllocation(dev_.allocator(), fb.skin_palettes.allocation, 0, VK_WHOLE_SIZE);
        }
        u32 base = 0;
        for (u32 i = 0; i < instances.size(); ++i) {
            if (!skinned(i)) continue;
            const Instance& in = instances[i];
            const MeshRange& mr = scene.meshes()[in.mesh];
            const VkDeviceAddress off = VkDeviceAddress(base) * sizeof(Vertex);
            skin_draw_of[i] = static_cast<u32>(skin_draws.size());
            skin_draws.push_back(
                SkinDraw{scene.vertex_buffer().address + VkDeviceAddress(mr.vertex_offset) * sizeof(Vertex),
                         scene.skin_attribute_buffer().address + VkDeviceAddress(mr.skin_offset) * sizeof(SkinAttr),
                         fb.skin_palettes.address + VkDeviceAddress(palette_base[in.skin]) * sizeof(mat4),
                         fb.skin_palettes.address + VkDeviceAddress(joints + palette_base[in.skin]) * sizeof(mat4),
                         fb.skin_out.address + off, fb.skin_prev.address + off, mr.vertex_count});
            base += mr.vertex_count;
        }
    }

    TEFRA_TRACE("render: terrain");
    // ---- Terrain patches ---------------------------------------------------------------------------
    constexpr VkDeviceSize kTerrainPatchOffset = 256;
    u32 patch_count = 0, shadow_patch_count = 0;
    if (terrain) {
        std::vector<TerrainPatchGPU> patches;
        terrain->build_patches(camera.position, patches);
        // Frustum cull patches with their height range.
        std::vector<TerrainPatchGPU> kept;
        const f32 hmid = 0.5f * (terrain->min_height() + terrain->max_height()) - f32(camera.position.y);
        const f32 hext = 0.5f * (terrain->max_height() - terrain->min_height());
        for (const auto& p : patches) {
            const f32 half = p.origin_size.z * 0.5f;
            const vec3 c{p.origin_size.x + half, hmid, p.origin_size.y + half};
            const f32 r = std::sqrt(2.0f * half * half + hext * hext);
            if (sphere_in_frustum(planes, c, r)) kept.push_back(p);
        }
        patch_count = static_cast<u32>(kept.size());
        const u32 all = static_cast<u32>(patches.size());
        shadow_patch_count = all;
        if (fb.terrain_capacity < all) {
            rhi::destroy_buffer(dev_, fb.terrain);
            fb.terrain_capacity = all * 2;
            fb.terrain =
                rhi::create_buffer(dev_, kTerrainPatchOffset + 2 * sizeof(TerrainPatchGPU) * fb.terrain_capacity,
                                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, rhi::MemoryUsage::Upload, "terrain_frame");
        }
        u8* dst = static_cast<u8*>(fb.terrain.mapped);
        TerrainGPU tg = terrain->gpu();
        tg.flags = (tg.flags & ~1u) | (s.terrain_detail ? 1u : 0u);
        std::memcpy(dst, &tg, sizeof(TerrainGPU));
        // Visible patches first (G-buffer), then all patches (shadows) after them.
        std::memcpy(dst + kTerrainPatchOffset, kept.data(), kept.size() * sizeof(TerrainPatchGPU));
        std::memcpy(dst + kTerrainPatchOffset + kept.size() * sizeof(TerrainPatchGPU), patches.data(),
                    patches.size() * sizeof(TerrainPatchGPU));
        vmaFlushAllocation(dev_.allocator(), fb.terrain.allocation, 0, VK_WHOLE_SIZE);
    }
    struct TerrainPush {
        VkDeviceAddress frame, terrain, patches, grid;
        u32 cascade;
        u32 pad[3];
    };
    const VkDeviceAddress terrain_addr = terrain ? fb.terrain.address : 0;
    const VkDeviceAddress grid_addr = terrain ? terrain->grid_vertices().address : 0;
    const VkBuffer grid_ib = terrain ? terrain->grid_indices().buffer : VK_NULL_HANDLE;
    const u32 grid_count = terrain ? terrain->grid_index_count() : 0;
    const VkDeviceAddress terrain_frame_addr = fb.constants.address;
    const VkPipelineLayout terrain_layout = layout_;
    auto draw_terrain = [=](VkCommandBuffer c, VkPipeline pipe, bool shadow_pass, u32 cascade) {
        if (!terrain_addr) return;
        vkCmdBindPipeline(c, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
        const VkDeviceAddress patches =
            terrain_addr + kTerrainPatchOffset + (shadow_pass ? patch_count * sizeof(TerrainPatchGPU) : 0);
        TerrainPush tp{terrain_frame_addr, terrain_addr, patches, grid_addr, cascade, {}};
        vkCmdPushConstants(c, terrain_layout, VK_SHADER_STAGE_ALL, 0, sizeof(tp), &tp);
        vkCmdBindIndexBuffer(c, grid_ib, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(c, grid_count, shadow_pass ? shadow_patch_count : patch_count, 0, 0, 0);
    };

    TEFRA_TRACE("render: grass");
    // ---- Grass: visible cells (CPU: disc around the camera and frustum), blades are generated on the GPU ----------
    const bool has_grass = terrain && s.grass;
    u32 grass_near = 0, grass_far = 0; // cell list: near cells first, then far cells
    GrassPush grass{};
    if (has_grass) {
        const i32 bx = i32(std::floor(camera.position.x)) - kGrassHalf;
        const i32 bz = i32(std::floor(camera.position.z)) - kGrassHalf;
        const f32 px = std::fabs(fc.proj.c[0].x), py = std::fabs(fc.proj.c[1].y); // Vulkan y-flip: proj[1][1] < 0
        const u32 blades =
            max<u32>(1u, static_cast<u32>(std::lround(f32(kGrassBladesPerCell) * max(s.grass_density, 0.0f))));
        std::vector<u32> near_cells, far_cells;
        const f32 grass_radius = clamp(s.grass_radius, 4.0f, f32(kGrassHalf - 1));
        for (u32 gi = 0; gi < kGrassMaxCells; ++gi) {
            const f64 cx = f64(bx + i32(gi % kGrassSide)) + 0.5, cz = f64(bz + i32(gi / kGrassSide)) + 0.5;
            const f64 dx = cx - camera.position.x, dz = cz - camera.position.z;
            const f32 dist = static_cast<f32>(std::sqrt(dx * dx + dz * dz));
            if (dist >= grass_radius) continue;
            const f32 ground = terrain->height_at(static_cast<f32>(cx), static_cast<f32>(cz));
            const vec3 rel{static_cast<f32>(dx), ground + 0.4f - static_cast<f32>(camera.position.y),
                           static_cast<f32>(dz)};
            const vec3 v = transform_point(fc.view, rel);
            const f32 depth = -v.z;
            if (depth + kGrassMargin < 0.0f) continue;
            if (depth > kGrassMargin) {
                // Conservative frustum test on the cell's bounding circle (same test as the GPU count shader).
                const f32 nx = px * v.x / depth, ny = py * v.y / depth;
                const f32 rx = px * kGrassMargin / depth, ry = py * kGrassMargin / depth;
                if (std::fabs(nx) - rx > 1.0f || std::fabs(ny) - ry > 1.0f) continue;
            }
            (dist < kGrassLodStart ? near_cells : far_cells).push_back(gi);
        }
        grass_near = static_cast<u32>(near_cells.size());
        grass_far = static_cast<u32>(far_cells.size());
        auto* list = static_cast<u32*>(fb.grass_cells.mapped);
        std::memcpy(list, near_cells.data(), near_cells.size() * sizeof(u32));
        std::memcpy(list + near_cells.size(), far_cells.data(), far_cells.size() * sizeof(u32));
        vmaFlushAllocation(dev_.allocator(), fb.grass_cells.allocation, 0, VK_WHOLE_SIZE);
        grass.frame = fb.constants.address;
        grass.terrain = fb.terrain.address;
        grass.cells = fb.grass_cells.address;
        grass.counter = fb.grass_counter.address;
        grass.p0 = {grass_radius, kGrassLodStart, kGrassWind, 3.0f};
        grass.grid[0] = bx;
        grass.grid[1] = static_cast<i32>(kGrassSide);
        grass.grid[2] = bz;
        grass.grid[3] = static_cast<i32>(blades);
    }
    const u32 grass_blades_per_cell = static_cast<u32>(grass.grid[3]);

    TEFRA_TRACE("render: vegetation");
    // ---- Vegetation (GPU culling + indirect draws) --------------------------------------------------
    struct DrawCmd {
        u32 index_count, instance_count, first_index;
        i32 vertex_offset;
        u32 first_instance;
    };
    static_assert(sizeof(DrawCmd) == 20);
    const bool has_veg = veg && veg->draw_count() > 0;
    if (has_veg) {
        if (veg_template_uid_ != veg->uid()) {
            dev_.wait_idle();
            rhi::destroy_buffer(dev_, veg_cmd_template_);
            for (u32 i = 0; i < 2; ++i) {
                if (history_bindless_[i] != ~0u) heap_.remove_texture(history_bindless_[i]);
                rhi::destroy_image(dev_, history_[i]);
            }
            std::vector<DrawCmd> tmpl;
            for (const auto& d : veg->draws()) tmpl.push_back({d.index_count, 0, d.first_index, d.vertex_offset, 0});
            veg_cmd_template_ = rhi::upload_buffer(dev_, tmpl.data(), tmpl.size() * sizeof(DrawCmd),
                                                   VK_BUFFER_USAGE_TRANSFER_SRC_BIT, "veg_cmd_template");
            veg_template_uid_ = veg->uid();
        }
        if (fb.veg_capacity < veg->visible_capacity()) {
            for (rhi::Buffer* b : {&fb.veg_visible, &fb.veg_shadow_visible, &fb.veg_cmds, &fb.veg_shadow_cmds})
                rhi::destroy_buffer(dev_, *b);
            fb.veg_capacity = veg->visible_capacity();
            const VkBufferUsageFlags ind = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                           VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            const rhi::MemoryUsage mem = cpu_veg_cull_ ? rhi::MemoryUsage::Upload : rhi::MemoryUsage::GpuOnly;
            fb.veg_visible = rhi::create_buffer(dev_, fb.veg_capacity * 4ull, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, mem,
                                                "veg_visible");
            fb.veg_shadow_visible = rhi::create_buffer(dev_, fb.veg_capacity * 4ull, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                       mem, "veg_shadow_visible");
            fb.veg_cmds = rhi::create_buffer(dev_, Vegetation::kMaxDraws * sizeof(DrawCmd), ind, mem, "veg_cmds");
            fb.veg_shadow_cmds =
                rhi::create_buffer(dev_, Vegetation::kMaxDraws * sizeof(DrawCmd), ind, mem, "veg_shadow_cmds");
        }
    }
    if (has_veg && cpu_veg_cull_) {
        // Same rules as shaders/veg_cull.comp: LOD by distance, shadow casters by distance, main list by frustum.
        const auto& protos = veg->cpu_protos();
        const auto& draws = veg->draws();
        auto* cmds = static_cast<DrawCmd*>(fb.veg_cmds.mapped);
        auto* scmds = static_cast<DrawCmd*>(fb.veg_shadow_cmds.mapped);
        auto* vis = static_cast<u32*>(fb.veg_visible.mapped);
        auto* svis = static_cast<u32*>(fb.veg_shadow_visible.mapped);
        for (u32 d = 0; d < veg->draw_count(); ++d) {
            cmds[d] = {draws[d].index_count, 0, draws[d].first_index, draws[d].vertex_offset, 0};
            scmds[d] = cmds[d];
        }
        vec4 vplanes[6];
        frustum_planes(fc.view_proj, vplanes);
        const vec3 cam = fc.camera_pos.xyz();
        const f32 ls = fc.veg_lod_scale > 0.0f ? fc.veg_lod_scale : 1.0f;
        auto emit = [&](DrawCmd* c, u32* list, u32 d0, u32 d1, u32 index) {
            const u32 at = c[d0].instance_count++;
            list[draws[d0].visible_base + at] = index;
            if (d1 != ~0u) ++c[d1].instance_count;
        };
        const auto& insts = veg->cpu_instances();
        for (u32 i = 0; i < veg->instance_count() && i < insts.size(); ++i) {
            const VegInstanceGPU& in = insts[i];
            const u32 pid = static_cast<u32>(in.rot_proto.z);
            if (pid >= protos.size()) continue;
            const Vegetation::ProtoGPU& p = protos[pid];
            const f32 sc = in.pos_scale.w;
            const vec3 c = in.pos_scale.xyz() + vec3{0.0f, p.bounds.y * sc, 0.0f} - cam;
            const f32 r = p.bounds.w * sc, dist = length(c);
            const int lod = dist < p.lod_dist.x * ls ? 0 : ((p.lod_dist.w > 1.5f && dist < p.lod_dist.y * ls) ? 1 : -1);
            if (lod < 0) continue;
            const u32 d0 = p.draw[lod][0], d1 = p.draw[lod][1];
            if (d0 == ~0u) continue;
            if (p.casts_shadows != 0u && dist < p.lod_dist.z * ls) emit(scmds, svis, d0, d1, i);
            bool inside = true;
            for (int k = 0; k < 5 && inside; ++k)
                if (dot(vplanes[k].xyz(), c) + vplanes[k].w < -r) inside = false;
            if (inside) emit(cmds, vis, d0, d1, i);
        }
        for (rhi::Buffer* b : {&fb.veg_visible, &fb.veg_shadow_visible, &fb.veg_cmds, &fb.veg_shadow_cmds})
            vmaFlushAllocation(dev_.allocator(), b->allocation, 0, VK_WHOLE_SIZE);
    }
    struct VegCullPush {
        VkDeviceAddress frame, instances, protos, draws, visible, cmds, shadow_visible, shadow_cmds;
        u32 count;
    };
    struct VegDrawPush {
        VkDeviceAddress frame, vertices, instances, materials, visible;
        u32 material;
        f32 wind;
        u32 cascade;
        u32 pad;
    };
    const VkDeviceAddress veg_frame_addr = fb.constants.address;
    const VkPipelineLayout veg_layout = layout_;
    auto draw_veg = [=, this, &scene](VkCommandBuffer c, bool shadow_pass, u32 cascade) {
        if (!has_veg) return;
        const VkBuffer cmds = shadow_pass ? fb.veg_shadow_cmds.buffer : fb.veg_cmds.buffer;
        const VkDeviceAddress vis = shadow_pass ? fb.veg_shadow_visible.address : fb.veg_visible.address;
        vkCmdBindIndexBuffer(c, scene.index_buffer().buffer, 0, VK_INDEX_TYPE_UINT32);
        VkPipeline bound = VK_NULL_HANDLE;
        for (u32 d = 0; d < veg->draw_count(); ++d) {
            const auto& dr = veg->draws()[d];
            const bool two_sided = (scene.materials()[dr.material].flags & 2u) != 0;
            const VkPipeline want = shadow_pass ? p_veg_shadow_ : (two_sided ? p_veg_2s_ : p_veg_);
            if (want != bound) {
                vkCmdBindPipeline(c, VK_PIPELINE_BIND_POINT_GRAPHICS, want);
                bound = want;
            }
            VegDrawPush vp{veg_frame_addr,
                           scene.vertex_buffer().address,
                           veg->instances().address,
                           scene.material_buffer().address,
                           vis + dr.visible_base * 4ull,
                           dr.material,
                           veg->prototype_wind(dr.prototype),
                           cascade,
                           0};
            vkCmdPushConstants(c, veg_layout, VK_SHADER_STAGE_ALL, 0, sizeof(vp), &vp);
            vkCmdDrawIndexedIndirect(c, cmds, d * sizeof(DrawCmd), 1, sizeof(DrawCmd));
        }
    };

    TEFRA_TRACE("render: frame graph");
    // ---- Frame graph -----------------------------------------------------------------------------
    graph_.begin(slot, timer);
    const u32 sres = s.shadow_resolution;
    rg::TextureHandle shadow, depth, rt0, rt1, rt2, rt3, hdr, skyview;
    const VkDeviceAddress frame_addr = fb.constants.address;
    draw_calls_ = 0;
    u32* draw_counter = &draw_calls_;
    const VkPipelineLayout layout = layout_;
    const VkDescriptorSet bindless = heap_.set(slot);
    const Scene* sc = &scene;
    const VkDeviceAddress inst_addr = fb.instances.address;

    auto draw_list = [=](VkCommandBuffer c, const std::vector<u32>& list, u32 cascade, bool shadow_pass,
                         VkPipeline one_sided, VkPipeline two_sided) {
        if (list.empty()) return;
        MeshPush push{frame_addr, sc->vertex_buffer().address, inst_addr, sc->material_buffer().address, cascade,
                      {},         sc->vertex_buffer().address};
        vkCmdPushConstants(c, layout, VK_SHADER_STAGE_ALL, 0, sizeof(push), &push);
        vkCmdBindIndexBuffer(c, sc->index_buffer().buffer, 0, VK_INDEX_TYPE_UINT32);
        VkPipeline bound = VK_NULL_HANDLE;
        bool static_pushed = true;
        for (u32 i : list) {
            const Instance& in = sc->instances()[i];
            const bool two = !shadow_pass && (sc->materials()[in.material].flags & 2u);
            const VkPipeline want = two ? two_sided : one_sided;
            if (want != bound) {
                vkCmdBindPipeline(c, VK_PIPELINE_BIND_POINT_GRAPHICS, want);
                bound = want;
            }
            const MeshRange& mr = sc->meshes()[in.mesh];
            if (skin_draw_of[i] != ~0u) {
                // Skinned: the posed copy is indexed from 0 (indices are mesh-local).
                const SkinDraw& d = skin_draws[skin_draw_of[i]];
                MeshPush sp{frame_addr, d.dst, inst_addr, sc->material_buffer().address, cascade, {}, d.dst_prev};
                vkCmdPushConstants(c, layout, VK_SHADER_STAGE_ALL, 0, sizeof(sp), &sp);
                vkCmdDrawIndexed(c, mr.index_count, 1, mr.first_index, 0, i);
                static_pushed = false;
            } else {
                if (!static_pushed) {
                    vkCmdPushConstants(c, layout, VK_SHADER_STAGE_ALL, 0, sizeof(push), &push);
                    static_pushed = true;
                }
                vkCmdDrawIndexed(c, mr.index_count, 1, mr.first_index, mr.vertex_offset, i);
            }
            ++*draw_counter;
        }
    };

    if (has_veg && !cpu_veg_cull_) {
        graph_.add_pass(
            "veg_cull", rg::PassType::Compute, [](rg::PassBuilder&) {},
            [=, this](const rg::PassContext& ctx) {
                const VkDeviceSize bytes = veg->draw_count() * sizeof(DrawCmd);
                // Previous frame's indirect reads must finish before the template copy overwrites the commands.
                rhi::cmd_memory_barrier(ctx.cmd,
                                        VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
                                        VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
                VkBufferCopy region{0, 0, bytes};
                vkCmdCopyBuffer(ctx.cmd, veg_cmd_template_.buffer, fb.veg_cmds.buffer, 1, &region);
                vkCmdCopyBuffer(ctx.cmd, veg_cmd_template_.buffer, fb.veg_shadow_cmds.buffer, 1, &region);
                rhi::cmd_memory_barrier(ctx.cmd, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                        VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
                vkCmdBindPipeline(ctx.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p_veg_cull_);
                VegCullPush cp{frame_addr,
                               veg->instances().address,
                               veg->protos().address,
                               veg->draws_table().address,
                               fb.veg_visible.address,
                               fb.veg_cmds.address,
                               fb.veg_shadow_visible.address,
                               fb.veg_shadow_cmds.address,
                               veg->instance_count()};
                vkCmdPushConstants(ctx.cmd, layout, VK_SHADER_STAGE_ALL, 0, sizeof(cp), &cp);
                vkCmdDispatch(ctx.cmd, (veg->instance_count() + 63) / 64, 1, 1);
                rhi::cmd_memory_barrier(ctx.cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                                        VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
                                        VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
            });
    }

    if (has_grass && s.grass_stats) {
        graph_.add_pass(
            "grass_count", rg::PassType::Compute, [](rg::PassBuilder&) {},
            [=, this](const rg::PassContext& ctx) {
                const VkBuffer counter = fb.grass_counter.buffer;
                vkCmdFillBuffer(ctx.cmd, counter, 0, sizeof(u32), 0);
                rhi::cmd_memory_barrier(ctx.cmd, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                        VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
                vkCmdBindPipeline(ctx.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p_grass_count_);
                vkCmdBindDescriptorSets(ctx.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &bindless, 0, nullptr);
                GrassPush gp = grass;
                gp.cell_count = grass_near + grass_far;
                vkCmdPushConstants(ctx.cmd, layout, VK_SHADER_STAGE_ALL, 0, sizeof(gp), &gp);
                const u32 invocations = gp.cell_count * grass_blades_per_cell;
                vkCmdDispatch(ctx.cmd, (invocations + 63) / 64, 1, 1);
                rhi::cmd_memory_barrier(ctx.cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_PIPELINE_STAGE_2_COPY_BIT,
                                        VK_ACCESS_2_TRANSFER_READ_BIT);
                VkBufferCopy region{0, 0, sizeof(u32)};
                vkCmdCopyBuffer(ctx.cmd, counter, fb.grass_readback.buffer, 1, &region);
                rhi::cmd_memory_barrier(ctx.cmd, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                        VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
            });
    }

    if (!skin_draws.empty()) {
        graph_.add_pass(
            "skin", rg::PassType::Compute, [](rg::PassBuilder&) {},
            [=, this](const rg::PassContext& ctx) {
                vkCmdBindPipeline(ctx.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p_skin_);
                for (const SkinDraw& d : skin_draws) {
                    const SkinPush sp{d.src, d.attrs, d.palette, d.palette_prev, d.dst, d.dst_prev, d.count, 0};
                    vkCmdPushConstants(ctx.cmd, layout_, VK_SHADER_STAGE_ALL, 0, sizeof(sp), &sp);
                    vkCmdDispatch(ctx.cmd, (d.count + 63) / 64, 1, 1);
                }
                rhi::cmd_memory_barrier(ctx.cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
                                        VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
            });
    }

    graph_.add_pass(
        "shadows", rg::PassType::Raster,
        [&](rg::PassBuilder& b) {
            shadow = b.create({shadow_format_, sres * cascades, sres, "shadow_atlas"});
            b.depth(shadow, rg::Load::Clear, 1.0f, true);
        },
        [=, this](const rg::PassContext& ctx) {
            vkCmdBindDescriptorSets(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &bindless, 0, nullptr);
            for (u32 c = 0; c < cascades; ++c) {
                VkViewport vp{f32(c * sres), 0.0f, f32(sres), f32(sres), 0.0f, 1.0f};
                VkRect2D sc_rect{{i32(c * sres), 0}, {sres, sres}};
                vkCmdSetViewport(ctx.cmd, 0, 1, &vp);
                vkCmdSetScissor(ctx.cmd, 0, 1, &sc_rect);
                draw_list(ctx.cmd, casters[c], c, true, p_shadow_, p_shadow_);
                draw_terrain(ctx.cmd, p_terrain_shadow_, true, c);
                draw_veg(ctx.cmd, true, c);
            }
        });

    graph_.add_pass(
        "gbuffer", rg::PassType::Raster,
        [&](rg::PassBuilder& b) {
            depth = b.create({kDepth, ext.width, ext.height, "depth"});
            rt0 = b.create({kRt0, ext.width, ext.height, "gbuf_albedo"});
            rt1 = b.create({kRt1, ext.width, ext.height, "gbuf_normal"});
            rt2 = b.create({kRt2, ext.width, ext.height, "gbuf_material"});
            rt3 = b.create({kRt3, ext.width, ext.height, "gbuf_velocity"});
            b.color(rt0, rg::Load::DontCare);
            b.color(rt1, rg::Load::DontCare);
            b.color(rt2, rg::Load::DontCare);
            b.color(rt3, rg::Load::Clear);
            b.depth(depth, rg::Load::Clear, 0.0f, true);
        },
        [=, this](const rg::PassContext& ctx) {
            vkCmdBindDescriptorSets(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &bindless, 0, nullptr);
            draw_list(ctx.cmd, visible, 0, false, p_gbuffer_, p_gbuffer_2s_);
            draw_terrain(ctx.cmd, p_terrain_, false, 0);
            draw_veg(ctx.cmd, false, 0);
            if (has_grass && grass_near + grass_far > 0) {
                vkCmdBindPipeline(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p_grass_);
                // Near cells: 7-vertex blades (3 segments). Far cells: 5-vertex blades (2 segments).
                for (u32 ring = 0; ring < 2; ++ring) {
                    const u32 count = ring == 0 ? grass_near : grass_far;
                    if (count == 0) continue;
                    const u32 seg = ring == 0 ? 3u : 2u;
                    GrassPush gp = grass;
                    gp.p0.w = f32(seg);
                    vkCmdPushConstants(ctx.cmd, layout, VK_SHADER_STAGE_ALL, 0, sizeof(gp), &gp);
                    const u32 verts = grass_blades_per_cell * 3u * (2u * seg - 1u);
                    vkCmdDraw(ctx.cmd, verts, count, 0, ring == 0 ? 0u : grass_near);
                    ++*draw_counter;
                }
            }
        });

    if (atmo) {
        graph_.add_pass(
            "skyview_lut", rg::PassType::Raster,
            [&](rg::PassBuilder& b) {
                skyview =
                    b.create({VK_FORMAT_R16G16B16A16_SFLOAT, Atmosphere::kSkyViewW, Atmosphere::kSkyViewH, "skyview"});
                b.color(skyview, rg::Load::DontCare);
            },
            [=, this](const rg::PassContext& ctx) {
                vkCmdBindDescriptorSets(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &bindless, 0, nullptr);
                vkCmdBindPipeline(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p_skyview_);
                TonemapPush p{frame_addr, 0, 0};
                vkCmdPushConstants(ctx.cmd, layout, VK_SHADER_STAGE_ALL, 0, sizeof(p), &p);
                vkCmdDraw(ctx.cmd, 3, 1, 0, 0);
            });
    }

    rg::TextureHandle ao;
    if (s.gtao) {
        const VkExtent2D ao_ext{max(1u, ext.width / 2), max(1u, ext.height / 2)};
        graph_.add_pass(
            "gtao", rg::PassType::Raster,
            [&](rg::PassBuilder& b) {
                ao = b.create({VK_FORMAT_R8_UNORM, ao_ext.width, ao_ext.height, "gtao"});
                b.read(depth, rg::Access::Sampled);
                b.read(rt1, rg::Access::Sampled);
                b.color(ao, rg::Load::DontCare);
            },
            [=, this](const rg::PassContext& ctx) {
                struct GtaoPush {
                    VkDeviceAddress frame;
                    u32 depth, normals;
                    f32 radius;
                    u32 pad[3]; // GLSL std430 aligns the vec4 to 16 bytes
                    vec4 sizes;
                } gp{frame_addr,       ctx.sampled(depth),
                     ctx.sampled(rt1), s.gtao_radius,
                     {0, 0, 0},        {f32(ext.width), f32(ext.height), f32(ao_ext.width), f32(ao_ext.height)}};
                static_assert(offsetof(GtaoPush, sizes) == 32);
                vkCmdBindDescriptorSets(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &bindless, 0, nullptr);
                vkCmdBindPipeline(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p_gtao_);
                vkCmdPushConstants(ctx.cmd, layout, VK_SHADER_STAGE_ALL, 0, sizeof(gp), &gp);
                vkCmdDraw(ctx.cmd, 3, 1, 0, 0);
            });
    }

    // Volumetric fog: half-resolution in-scatter and transmittance (volumetric.frag), composed in the lighting pass.
    // ---- Local lights: the nearest visible ones, camera-relative -------------------------------------------
    u32 light_count = 0;
    u32 local_shadow_count = 0;
    vec3 local_shadow_eye[kMaxLocalShadows];
    f32 local_shadow_range[kMaxLocalShadows];
    {
        const auto& src = scene.lights();
        vec4 lplanes[6];
        frustum_planes(fc.view_proj, lplanes);
        struct Pick {
            f32 d2;
            u32 index;
            vec3 rel;
        };
        std::vector<Pick> picks;
        picks.reserve(src.size());
        for (u32 i = 0; i < src.size(); ++i) {
            const LocalLight& l = src[i];
            if (l.intensity <= 0.0f || l.range <= 0.0f) continue;
            const vec3 rel{f32(l.position.x - camera.position.x), f32(l.position.y - camera.position.y),
                           f32(l.position.z - camera.position.z)};
            bool in_view = true;
            for (int k = 0; k < 5 && in_view; ++k)
                if (dot(lplanes[k].xyz(), rel) + lplanes[k].w < -l.range) in_view = false;
            if (in_view) picks.push_back({dot(rel, rel), i, rel});
        }
        const u32 cap = min<u32>(s.max_lights, kMaxLights);
        if (picks.size() > cap) {
            std::nth_element(picks.begin(), picks.begin() + cap, picks.end(),
                             [](const Pick& x, const Pick& y) { return x.d2 < y.d2; });
            picks.resize(cap);
        }
        // Nearest first: passes that only afford a few lights (rain, volumetric fog) take the head of the list.
        std::sort(picks.begin(), picks.end(), [](const Pick& x, const Pick& y) { return x.d2 < y.d2; });
        light_count = static_cast<u32>(picks.size());
        if (fb.light_capacity < max(light_count, 1u) || !fb.lights.buffer) {
            rhi::destroy_buffer(dev_, fb.lights);
            fb.light_capacity = max<u32>(64u, light_count * 2u);
            fb.lights = rhi::create_buffer(dev_, sizeof(LightGPU) * fb.light_capacity,
                                           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, rhi::MemoryUsage::Upload, "lights");
        }
        auto* dst = static_cast<LightGPU*>(fb.lights.mapped);
        const u32 max_shadows = min(s.local_shadows, kMaxLocalShadows);
        for (u32 i = 0; i < light_count; ++i) {
            const LocalLight& l = src[picks[i].index];
            dst[i].pos_range = vec4(picks[i].rel, l.range);
            dst[i].color = vec4(l.color * l.intensity, 0.0f);
            dst[i].spot = vec4(l.direction, l.cos_outer);
            f32 shadow_index = -1.0f;
            if (l.shadows && local_shadow_count < max_shadows) {
                // Perspective shadow map along the spot axis (straight down for point lights), at most 120 degrees
                // wide: lamps light the ground below them. Outside the frustum the light stays unshadowed.
                const u32 k = local_shadow_count++;
                const bool spot = l.cos_outer >= -1.0f;
                const vec3 axis = spot ? l.direction : vec3{0.0f, -1.0f, 0.0f};
                const f32 half = spot ? min(std::acos(clamp(l.cos_outer, -1.0f, 1.0f)) + 0.05f, 1.047f) : 1.047f;
                const vec3 up = std::fabs(axis.y) > 0.9f ? vec3{0.0f, 0.0f, 1.0f} : vec3{0.0f, 1.0f, 0.0f};
                const vec3 eye = picks[i].rel;
                const mat4 view = look_at(eye, eye + axis, up);
                // Vulkan right-handed perspective, depth 0 (near) .. 1 (far). The near plane (0.3 m) clips the
                // fixture's own glass and frame around the light.
                const f32 n = 0.3f, fz = max(l.range, n + 0.5f), t = 1.0f / std::tan(half);
                mat4 proj{};
                proj.c[0].x = t;
                proj.c[1].y = t;
                proj.c[2].z = fz / (n - fz);
                proj.c[2].w = -1.0f;
                proj.c[3].z = -(fz * n) / (fz - n);
                fc.local_shadow_vp[k] = proj * view;
                fc.local_shadow_atlas[k] = {f32(k % 4) * 0.25f, f32(k / 4) * 0.5f, 0.25f, 0.5f};
                local_shadow_eye[k] = eye;
                local_shadow_range[k] = fz;
                shadow_index = f32(k);
            }
            dst[i].extra = vec4(l.cos_inner, l.spill, shadow_index, 0.0f);
        }
        fc.local_shadow_count = local_shadow_count;
        fc.local_shadow_texel = 1.0f / f32(max(s.local_shadow_resolution, 1u) * 4u);
        if (light_count) vmaFlushAllocation(dev_.allocator(), fb.lights.allocation, 0, VK_WHOLE_SIZE);
    }
    const VkDeviceAddress lights_addr = fb.lights.address;

    // Local light shadow maps: one tile of a 4 x 2 atlas per shadowed light, casters within its range.
    rg::TextureHandle local_shadow{};
    if (local_shadow_count > 0) {
        std::vector<u32> local_casters[kMaxLocalShadows];
        for (u32 k = 0; k < local_shadow_count; ++k) {
            vec4 lp[6];
            frustum_planes(fc.local_shadow_vp[k], lp);
            for (const CasterSphere& cs : caster_spheres) {
                const vec3 d = cs.c - local_shadow_eye[k];
                if (dot(d, d) > (local_shadow_range[k] + cs.r) * (local_shadow_range[k] + cs.r)) continue;
                if (sphere_in_frustum(lp, cs.c, cs.r)) local_casters[k].push_back(cs.index);
            }
        }
        const u32 lres = max(s.local_shadow_resolution, 64u);
        graph_.add_pass(
            "local_shadows", rg::PassType::Raster,
            [&](rg::PassBuilder& b) {
                local_shadow = b.create({shadow_format_, lres * 4, lres * 2, "local_shadow_atlas"});
                b.depth(local_shadow, rg::Load::Clear, 1.0f, true);
            },
            [=, this, &fc](const rg::PassContext& ctx) {
                std::memcpy(frames_[slot].constants.mapped, &fc, sizeof(fc));
                vmaFlushAllocation(dev_.allocator(), frames_[slot].constants.allocation, 0, VK_WHOLE_SIZE);
                vkCmdBindDescriptorSets(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &bindless, 0, nullptr);
                for (u32 k = 0; k < local_shadow_count; ++k) {
                    VkViewport vp{f32((k % 4) * lres), f32((k / 4) * lres), f32(lres), f32(lres), 0.0f, 1.0f};
                    VkRect2D sc_rect{{i32((k % 4) * lres), i32((k / 4) * lres)}, {lres, lres}};
                    vkCmdSetViewport(ctx.cmd, 0, 1, &vp);
                    vkCmdSetScissor(ctx.cmd, 0, 1, &sc_rect);
                    draw_list(ctx.cmd, local_casters[k], kMaxCascades + k, true, p_shadow_, p_shadow_);
                }
            });
    }

    // Tiled light culling: each 16x16 pixel tile keeps the lights whose sphere touches its depth range.
    const u32 tiles_x = (ext.width + kLightTile - 1) / kLightTile, tiles_y = (ext.height + kLightTile - 1) / kLightTile;
    {
        const u32 need = tiles_x * tiles_y * (1u + kMaxTileLights);
        if (fb.tile_capacity < need || !fb.light_tiles.buffer) {
            rhi::destroy_buffer(dev_, fb.light_tiles);
            fb.tile_capacity = need;
            fb.light_tiles = rhi::create_buffer(dev_, need * 4ull, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                rhi::MemoryUsage::GpuOnly, "light_tiles");
        }
    }
    const VkDeviceAddress tiles_addr = fb.light_tiles.address;
    if (light_count > 0) {
        graph_.add_pass(
            "light_cull", rg::PassType::Compute, [&](rg::PassBuilder& b) { b.read(depth, rg::Access::Sampled); },
            [=, this](const rg::PassContext& ctx) {
                struct CullPush {
                    VkDeviceAddress frame, lights, tiles;
                    u32 depth, count, tiles_x, tiles_y;
                } cp{frame_addr, lights_addr, tiles_addr, ctx.sampled(depth), light_count, tiles_x, tiles_y};
                // Last frame's reads of this slot's tile buffer finished (fence); this frame overwrites it.
                rhi::cmd_memory_barrier(ctx.cmd, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_NONE,
                                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
                vkCmdBindDescriptorSets(ctx.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &bindless, 0, nullptr);
                vkCmdBindPipeline(ctx.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p_light_cull_);
                vkCmdPushConstants(ctx.cmd, layout, VK_SHADER_STAGE_ALL, 0, sizeof(cp), &cp);
                vkCmdDispatch(ctx.cmd, tiles_x, tiles_y, 1);
                rhi::cmd_memory_barrier(ctx.cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                        VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
            });
    }

    // Volumetric clouds: sky pixels only, reduced resolution, upsampled in the lighting pass.
    rg::TextureHandle clouds_tex;
    if (vclouds) {
        const u32 div = clamp(s.cloud_res_div, 1u, 8u);
        const VkExtent2D cext{max(1u, ext.width / div), max(1u, ext.height / div)};
        graph_.add_pass(
            "clouds", rg::PassType::Raster,
            [&](rg::PassBuilder& b) {
                clouds_tex = b.create({VK_FORMAT_R16G16B16A16_SFLOAT, cext.width, cext.height, "clouds"});
                b.read(depth, rg::Access::Sampled);
                b.color(clouds_tex, rg::Load::DontCare);
            },
            [=, this](const rg::PassContext& ctx) {
                vkCmdBindDescriptorSets(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &bindless, 0, nullptr);
                vkCmdBindPipeline(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p_clouds_);
                VolumetricPush cp{frame_addr, ctx.sampled(depth), 0};
                vkCmdPushConstants(ctx.cmd, layout, VK_SHADER_STAGE_ALL, 0, sizeof(cp), &cp);
                vkCmdDraw(ctx.cmd, 3, 1, 0, 0);
            });
    }
    rg::TextureHandle vol;
    const bool volumetric = s.volumetric_fog && s.fog_density > 0.0f;
    if (volumetric) {
        const VkExtent2D vol_ext{max(1u, ext.width / 2), max(1u, ext.height / 2)};
        graph_.add_pass(
            "volumetric", rg::PassType::Raster,
            [&](rg::PassBuilder& b) {
                vol = b.create({VK_FORMAT_R16G16B16A16_SFLOAT, vol_ext.width, vol_ext.height, "volumetric"});
                b.read(depth, rg::Access::Sampled);
                b.read(shadow, rg::Access::Sampled);
                if (local_shadow.valid()) b.read(local_shadow, rg::Access::Sampled);
                b.color(vol, rg::Load::DontCare);
            },
            [=, this, &fc](const rg::PassContext& ctx) {
                // This pass samples the shadow atlas and the frame constants before the lighting pass runs.
                fc.shadow_atlas = ctx.sampled(shadow);
                fc.local_shadow_tex = local_shadow.valid() ? ctx.sampled(local_shadow) : kNoTexture;
                std::memcpy(frames_[slot].constants.mapped, &fc, sizeof(fc));
                vmaFlushAllocation(dev_.allocator(), frames_[slot].constants.allocation, 0, VK_WHOLE_SIZE);
                vkCmdBindDescriptorSets(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &bindless, 0, nullptr);
                vkCmdBindPipeline(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p_volumetric_);
                VolumetricPush vp{frame_addr, ctx.sampled(depth), 0, lights_addr, min(light_count, 16u), 0};
                vkCmdPushConstants(ctx.cmd, layout, VK_SHADER_STAGE_ALL, 0, sizeof(vp), &vp);
                vkCmdDraw(ctx.cmd, 3, 1, 0, 0);
            });
    }

    // TAA history (imported before the lighting pass: screen-space reflections read last frame's image).
    rg::TextureHandle taa_prev{}, taa_next{};
    u32 taa_valid = 0;
    if (s.taa) {
        if (history_extent_.width != out_ext.width || history_extent_.height != out_ext.height) {
            dev_.wait_idle();
            for (u32 i = 0; i < 2; ++i) {
                if (history_bindless_[i] != ~0u) heap_.remove_texture(history_bindless_[i]);
                rhi::destroy_image(dev_, history_[i]);
                rhi::ImageDesc hd;
                hd.format = VK_FORMAT_R16G16B16A16_SFLOAT;
                hd.width = out_ext.width;
                hd.height = out_ext.height;
                hd.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
                hd.name = i == 0 ? "taa_history_0" : "taa_history_1";
                history_[i] = rhi::create_image(dev_, hd);
                history_bindless_[i] = heap_.add_texture(history_[i].view);
                history_layout_[i] = VK_IMAGE_LAYOUT_UNDEFINED;
            }
            history_extent_ = out_ext;
            history_valid_ = false;
        }
        if (camera_cut) history_valid_ = false;
        const u32 prev_i = history_cur_, next_i = history_cur_ ^ 1u;
        auto import_history = [&](u32 i, const char* name) {
            rg::ImportDesc d;
            d.name = name;
            d.image = history_[i].image;
            d.view = history_[i].view;
            d.format = VK_FORMAT_R16G16B16A16_SFLOAT;
            d.extent = out_ext;
            d.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            d.initial_layout = history_layout_[i];
            d.initial_stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            d.initial_access = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            d.bindless = history_bindless_[i];
            return graph_.import(d);
        };
        taa_prev = import_history(prev_i, "taa_history_prev");
        taa_next = import_history(next_i, "taa_history_next");
        taa_valid = history_valid_ ? 1u : 0u;
    }
    const bool ssr = s.ssr && taa_valid != 0u;
    static const bool no_gi = std::getenv("TEFRA_NO_GI") != nullptr; // A/B comparisons of the bounce light
    const u32 gi_rays = taa_valid != 0u && !no_gi ? min(s.gi_rays, 15u) : 0u;
    const bool use_history = ssr || gi_rays > 0u;

    graph_.add_pass(
        "lighting", rg::PassType::Raster,
        [&](rg::PassBuilder& b) {
            hdr = b.create({hdr_format_, ext.width, ext.height, "hdr"});
            for (auto h : {depth, rt0, rt1, rt2, shadow}) b.read(h, rg::Access::Sampled);
            if (local_shadow.valid()) b.read(local_shadow, rg::Access::Sampled);
            if (skyview.valid()) b.read(skyview, rg::Access::Sampled);
            if (ao.valid()) b.read(ao, rg::Access::Sampled);
            if (vol.valid()) b.read(vol, rg::Access::Sampled);
            if (clouds_tex.valid()) b.read(clouds_tex, rg::Access::Sampled);
            if (use_history) b.read(taa_prev, rg::Access::Sampled);
            b.color(hdr, rg::Load::DontCare);
        },
        [=, this, &fc](const rg::PassContext& ctx) {
            // The shadow atlas index is only known once the graph has bound physical textures.
            fc.shadow_atlas = ctx.sampled(shadow);
            fc.local_shadow_tex = local_shadow.valid() ? ctx.sampled(local_shadow) : kNoTexture;
            if (skyview.valid()) fc.atmo.skyview_lut = ctx.sampled(skyview);
            std::memcpy(frames_[slot].constants.mapped, &fc, sizeof(fc));
            vmaFlushAllocation(dev_.allocator(), frames_[slot].constants.allocation, 0, VK_WHOLE_SIZE);
            vkCmdBindDescriptorSets(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &bindless, 0, nullptr);
            vkCmdBindPipeline(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p_lighting_);
            LightingPush p{frame_addr,
                           ctx.sampled(depth),
                           ctx.sampled(rt0),
                           ctx.sampled(rt1),
                           ctx.sampled(rt2),
                           ao.valid() ? ctx.sampled(ao) : 0xFFFFFFFFu,
                           vol.valid() ? ctx.sampled(vol) : 0xFFFFFFFFu,
                           lights_addr,
                           light_count,
                           clouds_tex.valid() ? ctx.sampled(clouds_tex) : 0xFFFFFFFFu,
                           tiles_addr,
                           tiles_x,
                           use_history ? ctx.sampled(taa_prev) : 0xFFFFFFFFu,
                           (ssr ? 1u : 0u) | (gi_rays << 8u)};
            vkCmdPushConstants(ctx.cmd, layout, VK_SHADER_STAGE_ALL, 0, sizeof(p), &p);
            vkCmdDraw(ctx.cmd, 3, 1, 0, 0);
        });

    if (world.ocean) {
        const Ocean* ocean = world.ocean;
        // Sectors of the polar grid facing the camera (all of them when looking steeply down or up).
        const mat4& vr = fc.view;
        const vec3 fwd{-vr.c[0].z, -vr.c[1].z, -vr.c[2].z};
        const f32 half_h = std::atan(std::tan(camera.fovy * 0.5f) * aspect);
        std::vector<u32> sectors;
        const bool all = std::fabs(fwd.y) > 0.75f;
        const f32 fa = std::atan2(fwd.z, fwd.x);
        for (u32 i = 0; i < Ocean::kSectors; ++i) {
            f32 d = std::fabs(ocean->sector_angle(i) - fa);
            d = std::fmod(d, kTwoPi);
            d = min(d, kTwoPi - d);
            if (all || d < half_h + kTwoPi / f32(Ocean::kSectors) + 0.35f) sectors.push_back(i);
        }
        const VkDeviceAddress ocean_terrain = terrain ? fb.terrain.address : 0;
        const vec4 ocean_params{ocean->desc().level, ocean->desc().surf_height, f32(camera.position.x),
                                f32(camera.position.z)};
        rg::TextureHandle sea;
        graph_.add_pass(
            "ocean", rg::PassType::Raster,
            [&](rg::PassBuilder& b) {
                sea = b.create({hdr_format_, ext.width, ext.height, "hdr_ocean"});
                b.read(hdr, rg::Access::Sampled);
                b.read(depth, rg::Access::Sampled);
                b.read(shadow, rg::Access::Sampled);
                b.color(sea, rg::Load::DontCare);
                b.depth(depth, rg::Load::Load, 0.0f, false);
            },
            [=, this](const rg::PassContext& ctx) {
                struct OceanPush {
                    VkDeviceAddress frame, verts, waves, terrain;
                    u32 hdr, depth, has_terrain;
                    f32 time;
                    vec4 params;
                } op{frame_addr,       ocean->vertices().address, ocean->waves().address,  ocean_terrain,
                     ctx.sampled(hdr), ctx.sampled(depth),        ocean_terrain ? 1u : 0u, time,
                     ocean_params};
                static_assert(offsetof(OceanPush, params) == 48 && sizeof(OceanPush) == 64);
                vkCmdBindDescriptorSets(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &bindless, 0, nullptr);
                vkCmdPushConstants(ctx.cmd, layout, VK_SHADER_STAGE_ALL, 0, sizeof(op), &op);
                vkCmdBindPipeline(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p_ocean_copy_);
                vkCmdDraw(ctx.cmd, 3, 1, 0, 0);
                vkCmdBindPipeline(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p_ocean_);
                vkCmdBindIndexBuffer(ctx.cmd, ocean->indices().buffer, 0, VK_INDEX_TYPE_UINT32);
                for (u32 sct : sectors)
                    vkCmdDrawIndexed(ctx.cmd, ocean->sector_count(), 1, ocean->sector_first(sct), 0, 0);
            });
        hdr = sea;
    }

    if (world.water) {
        const Water* water = world.water;
        graph_.add_pass(
            "water", rg::PassType::Raster,
            [&](rg::PassBuilder& b) {
                b.color(hdr, rg::Load::Load);
                b.depth(depth, rg::Load::Load, 0.0f, false);
                b.read(depth, rg::Access::Sampled);
            },
            [=, this](const rg::PassContext& ctx) {
                struct WaterPush {
                    VkDeviceAddress frame, verts;
                    u32 depth;
                } wp{frame_addr, water->vertices().address, ctx.sampled(depth)};
                vkCmdBindDescriptorSets(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &bindless, 0, nullptr);
                vkCmdBindPipeline(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p_water_);
                vkCmdPushConstants(ctx.cmd, layout, VK_SHADER_STAGE_ALL, 0, sizeof(wp), &wp);
                vkCmdBindIndexBuffer(ctx.cmd, water->indices().buffer, 0, VK_INDEX_TYPE_UINT32);
                vkCmdDrawIndexed(ctx.cmd, water->index_count(), 1, 0, 0, 0);
            });
    }

    rg::TextureHandle particle_mask{};
    bool has_particle_mask = false;
    const u32 pcount = world.particles ? min<u32>(world.particle_count, 200000u) : 0u;
    const u32 rain_drops = s.rain > 0.0f ? static_cast<u32>(s.rain * f32(s.rain_drops)) : 0u;
    if (pcount > 0 || rain_drops > 0) {
        has_particle_mask = true;
        VkDeviceAddress parts = 0;
        if (pcount > 0) {
            if (fb.particle_capacity < pcount || !fb.particles.buffer) {
                rhi::destroy_buffer(dev_, fb.particles);
                fb.particle_capacity = max<u32>(1024u, pcount * 2u);
                fb.particles =
                    rhi::create_buffer(dev_, sizeof(ParticleGPU) * fb.particle_capacity,
                                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                       rhi::MemoryUsage::Upload, "particles");
            }
            std::memcpy(fb.particles.mapped, world.particles, sizeof(ParticleGPU) * pcount);
            vmaFlushAllocation(dev_.allocator(), fb.particles.allocation, 0, VK_WHOLE_SIZE);
            parts = fb.particles.address;
        }
        graph_.add_pass(
            "particles", rg::PassType::Raster,
            [&](rg::PassBuilder& b) {
                particle_mask = b.create({VK_FORMAT_R8_UNORM, ext.width, ext.height, "particle_mask"});
                b.color(hdr, rg::Load::Load);
                b.color(particle_mask, rg::Load::Clear);
                b.depth(depth, rg::Load::Load, 0.0f, false);
                b.read(depth, rg::Access::Sampled);
            },
            [=, this](const rg::PassContext& ctx) {
                vkCmdBindDescriptorSets(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &bindless, 0, nullptr);
                if (pcount > 0) {
                    struct ParticlePush {
                        VkDeviceAddress frame, parts;
                        u32 depth;
                    } pp{frame_addr, parts, ctx.sampled(depth)};
                    vkCmdBindPipeline(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p_particles_);
                    vkCmdPushConstants(ctx.cmd, layout, VK_SHADER_STAGE_ALL, 0, sizeof(pp), &pp);
                    vkCmdDraw(ctx.cmd, pcount * 6u, 1, 0, 0);
                }
                if (rain_drops > 0) {
                    // Rain streaks: procedural (no buffer), wrapped in a box around the camera, lit by the nearest
                    // local lights (rain is only visible where light catches it).
                    struct RainPush {
                        VkDeviceAddress frame, lights;
                        u32 depth, light_count, drops, pad;
                    } rp{frame_addr, lights_addr, ctx.sampled(depth), min(light_count, 24u), rain_drops, 0};
                    vkCmdBindPipeline(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p_rain_);
                    vkCmdPushConstants(ctx.cmd, layout, VK_SHADER_STAGE_ALL, 0, sizeof(rp), &rp);
                    vkCmdDraw(ctx.cmd, rain_drops * 6u, 1, 0, 0);
                }
            });
    }

    TEFRA_TRACE("render: taa");
    // ---- TAA ---------------------------------------------------------------------------------------
    rg::TextureHandle resolved = hdr;
    if (s.taa) {
        const rg::TextureHandle hist_prev = taa_prev, hist_next = taa_next;
        const u32 valid = taa_valid;
        graph_.add_pass(
            "taa", rg::PassType::Raster,
            [&](rg::PassBuilder& b) {
                b.read(hdr, rg::Access::Sampled);
                b.read(depth, rg::Access::Sampled);
                b.read(rt3, rg::Access::Sampled);
                if (has_particle_mask) b.read(particle_mask, rg::Access::Sampled);
                if (valid) b.read(hist_prev, rg::Access::Sampled);
                b.color(hist_next, rg::Load::DontCare);
            },
            [=, this](const rg::PassContext& ctx) {
                struct TaaPush {
                    VkDeviceAddress frame;
                    u32 current, history, depth, valid;
                    u32 velocity, reactive; // GLSL std430 aligns the vec4 to 16 bytes
                    vec4 sizes;
                } tp{frame_addr,
                     ctx.sampled(hdr),
                     valid ? ctx.sampled(hist_prev) : 0u,
                     ctx.sampled(depth),
                     valid,
                     ctx.sampled(rt3),
                     has_particle_mask ? ctx.sampled(particle_mask) : 0xFFFFFFFFu,
                     {f32(ext.width), f32(ext.height), f32(out_ext.width), f32(out_ext.height)}};
                static_assert(offsetof(TaaPush, sizes) == 32);
                vkCmdBindDescriptorSets(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &bindless, 0, nullptr);
                vkCmdBindPipeline(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p_taa_);
                vkCmdPushConstants(ctx.cmd, layout, VK_SHADER_STAGE_ALL, 0, sizeof(tp), &tp);
                vkCmdDraw(ctx.cmd, 3, 1, 0, 0);
            });
        resolved = hist_next;
        taa_handles_[0] = hist_prev;
        taa_handles_[1] = hist_next;
    }

    TEFRA_TRACE("render: exposure");
    // ---- Auto exposure: mean log2 luminance reduced to 1x1 (4x4 per level), then adapted by a compute pass -------
    // The adapted EV compensation lives in this slot's buffer and is read by the tonemap of this frame.
    // Always a valid address (the buffer exists per slot): the tonemap must never dereference a null pointer.
    const VkDeviceAddress exposure_addr = fb.exposure.address;
    if (s.auto_exposure) {
        rg::TextureHandle lum = resolved;
        u32 lw = out_ext.width, lh = out_ext.height;
        bool first = true;
        for (;;) {
            const u32 nw = max(1u, (lw + 3u) / 4u), nh = max(1u, (lh + 3u) / 4u);
            const rg::TextureHandle src = lum;
            const bool is_first = first;
            rg::TextureHandle level;
            graph_.add_pass(
                "exposure_reduce", rg::PassType::Raster,
                [&, nw, nh](rg::PassBuilder& b) {
                    level = b.create({VK_FORMAT_R16_SFLOAT, nw, nh, "exposure_reduce"});
                    b.read(src, rg::Access::Sampled);
                    b.color(level, rg::Load::DontCare);
                },
                [=, this](const rg::PassContext& ctx) {
                    const u32 idx = ctx.sampled(src);
                    vkCmdBindDescriptorSets(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &bindless, 0,
                                            nullptr);
                    vkCmdBindPipeline(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                      is_first ? p_expo_first_ : p_expo_reduce_);
                    vkCmdPushConstants(ctx.cmd, layout, VK_SHADER_STAGE_ALL, 0, sizeof(idx), &idx);
                    vkCmdDraw(ctx.cmd, 3, 1, 0, 0);
                });
            lum = level;
            lw = nw;
            lh = nh;
            first = false;
            if (nw == 1 && nh == 1) break;
        }
        const rg::TextureHandle mean = lum;
        const u32 frame32 = static_cast<u32>(frame_index);
        graph_.add_pass(
            "exposure_adapt", rg::PassType::Compute, [&](rg::PassBuilder& b) { b.read(mean, rg::Access::Sampled); },
            [=, this](const rg::PassContext& ctx) {
                const ExposureAdaptPush ap{exposure_addr,
                                           ctx.sampled(mean),
                                           frame32,
                                           std::log2(kExposureKey),
                                           kExposureLimitEv,
                                           kExposureTauDarken * kNominalFps,
                                           kExposureTauBrighten * kNominalFps};
                vkCmdBindDescriptorSets(ctx.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &bindless, 0, nullptr);
                vkCmdBindPipeline(ctx.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p_expo_adapt_);
                vkCmdPushConstants(ctx.cmd, layout, VK_SHADER_STAGE_ALL, 0, sizeof(ap), &ap);
                vkCmdDispatch(ctx.cmd, 1, 1, 1);
                rhi::cmd_memory_barrier(ctx.cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                        VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
            });
    }

    TEFRA_TRACE("render: bloom");
    // ---- Bloom: 6-level dual filter (13-tap down with Karis on the first level, tent up), no threshold --------
    rg::TextureHandle bloom;
    if (s.bloom) {
        constexpr u32 kLevels = 6;
        rg::TextureHandle down[kLevels];
        rg::TextureHandle prev = resolved;
        for (u32 l = 0; l < kLevels; ++l) {
            const u32 w = max(1u, out_ext.width >> (l + 1)), h = max(1u, out_ext.height >> (l + 1));
            const rg::TextureHandle src = prev;
            const u32 karis = l == 0 ? 1u : 0u;
            graph_.add_pass(
                "bloom_down", rg::PassType::Raster,
                [&, l, w, h](rg::PassBuilder& b) {
                    down[l] = b.create({hdr_format_, w, h, "bloom_down"});
                    b.read(src, rg::Access::Sampled);
                    b.color(down[l], rg::Load::DontCare);
                },
                [=, this](const rg::PassContext& ctx) {
                    const BloomPush bp{ctx.sampled(src), karis};
                    vkCmdBindDescriptorSets(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &bindless, 0,
                                            nullptr);
                    vkCmdBindPipeline(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p_bloom_down_);
                    vkCmdPushConstants(ctx.cmd, layout, VK_SHADER_STAGE_ALL, 0, sizeof(bp), &bp);
                    vkCmdDraw(ctx.cmd, 3, 1, 0, 0);
                });
            prev = down[l];
        }
        rg::TextureHandle chain = down[kLevels - 1];
        for (int l = static_cast<int>(kLevels) - 2; l >= 0; --l) {
            const u32 w = max(1u, out_ext.width >> (static_cast<u32>(l) + 1));
            const u32 h = max(1u, out_ext.height >> (static_cast<u32>(l) + 1));
            const rg::TextureHandle coarse = chain, fine = down[l];
            rg::TextureHandle level;
            graph_.add_pass(
                "bloom_up", rg::PassType::Raster,
                [&, w, h](rg::PassBuilder& b) {
                    level = b.create({hdr_format_, w, h, "bloom_up"});
                    b.read(coarse, rg::Access::Sampled);
                    b.read(fine, rg::Access::Sampled);
                    b.color(level, rg::Load::DontCare);
                },
                [=, this](const rg::PassContext& ctx) {
                    const BloomUpPush up{ctx.sampled(coarse), ctx.sampled(fine)};
                    vkCmdBindDescriptorSets(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &bindless, 0,
                                            nullptr);
                    vkCmdBindPipeline(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p_bloom_up_);
                    vkCmdPushConstants(ctx.cmd, layout, VK_SHADER_STAGE_ALL, 0, sizeof(up), &up);
                    vkCmdDraw(ctx.cmd, 3, 1, 0, 0);
                });
            chain = level;
        }
        bloom = chain;
    }

    rg::ImportDesc out;
    out.name = "output";
    out.image = target.image;
    out.view = target.view;
    out.format = target.format;
    out.extent = out_ext;
    out.usage = target.usage;
    out.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    out.initial_stage = target.initial_stage;
    out.initial_access = target.initial_access;
    out.final_layout = target.final_layout;
    const rg::TextureHandle output = graph_.import(out);

    const u32 post_flags = (s.bloom ? 1u : 0u) | (s.auto_exposure ? 2u : 0u) | (s.film_effects ? 4u : 0u);
    graph_.add_pass(
        "tonemap", rg::PassType::Raster,
        [&](rg::PassBuilder& b) {
            b.read(resolved, rg::Access::Sampled);
            if (bloom.valid()) b.read(bloom, rg::Access::Sampled);
            b.color(output, rg::Load::DontCare);
        },
        [=, this](const rg::PassContext& ctx) {
            vkCmdBindDescriptorSets(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &bindless, 0, nullptr);
            vkCmdBindPipeline(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p_tonemap_);
            const PostPush p{
                frame_addr,
                ctx.sampled(resolved),
                bloom.valid() ? ctx.sampled(bloom) : 0u,
                exposure_addr,
                post_flags,
                s.bloom_intensity,
                vec4{s.grade_white_balance.x, s.grade_white_balance.y, s.grade_white_balance.z, s.grade_saturation},
                vec4{s.grade_contrast, 0.0f, 0.0f, 0.0f}};
            vkCmdPushConstants(ctx.cmd, layout, VK_SHADER_STAGE_ALL, 0, sizeof(p), &p);
            vkCmdDraw(ctx.cmd, 3, 1, 0, 0);
        });

    graph_.execute(cmd);
    if (s.taa) {
        const u32 prev_i = history_cur_, next_i = history_cur_ ^ 1u;
        history_layout_[prev_i] = graph_.final_state(taa_handles_[0]).layout;
        history_layout_[next_i] = graph_.final_state(taa_handles_[1]).layout;
        history_cur_ = next_i;
        history_valid_ = true;
    }
}

const char* quality_name(Quality q) {
    switch (q) {
    case Quality::Low: return "Low";
    case Quality::Medium: return "Medium";
    case Quality::High: return "High";
    case Quality::Ultra: return "Ultra";
    }
    return "?";
}

void limit_quality(RenderSettings& s, Quality q) {
    struct Tier {
        f32 render_scale;
        u32 cascades, shadow_resolution;
        f32 shadow_distance;
        bool pcss, contact, gtao, volumetric, terrain_detail, bloom, canopy, grass;
        f32 grass_density, grass_radius, vegetation;
    };
    static const Tier tiers[4] = {
        // scale casc res   dist  pcss   contact gtao   vol    detail bloom  canopy grass  dens rad  veg
        {0.60f, 1, 1024, 50.0f, false, false, false, false, false, false, false, false, 0.0f, 0.0f, 0.45f}, // Low
        {0.80f, 2, 1024, 90.0f, false, true, true, false, false, true, true, true, 0.8f, 20.0f, 0.7f},      // Medium
        {1.00f, 3, 2048, 160.0f, true, true, true, true, true, true, true, true, 1.5f, 30.0f, 1.0f},        // High
        {1.00f, 4, 2048, 250.0f, true, true, true, true, true, true, true, true, 2.2f, 50.0f, 1.3f},        // Ultra
    };
    const Tier& t = tiers[static_cast<u32>(q)];
    s.render_scale = min(s.render_scale, t.render_scale);
    if (s.render_scale < 1.0f) s.taa = true; // TAAU reconstructs the output resolution
    s.cascades = min(s.cascades, t.cascades);
    s.shadow_resolution = min(s.shadow_resolution, t.shadow_resolution);
    s.shadow_distance = min(s.shadow_distance, t.shadow_distance);
    s.pcss_shadows = s.pcss_shadows && t.pcss;
    s.contact_shadows = s.contact_shadows && t.contact;
    s.gtao = s.gtao && t.gtao;
    s.volumetric_fog = s.volumetric_fog && t.volumetric;
    s.terrain_detail = s.terrain_detail && t.terrain_detail;
    s.bloom = s.bloom && t.bloom;
    s.canopy_occlusion = s.canopy_occlusion && t.canopy;
    s.grass = s.grass && t.grass;
    s.grass_density = min(s.grass_density, t.grass_density);
    s.grass_radius = min(s.grass_radius, t.grass_radius);
    s.vegetation_distance *= t.vegetation;
    // Volumetric clouds stay optional (Clouds.Volumetric); presets set their cost.
    static const u32 cloud_steps[4] = {14, 20, 32, 48};
    static const u32 cloud_div[4] = {4, 4, 2, 2};
    s.cloud_steps = min(s.cloud_steps, cloud_steps[static_cast<u32>(q)]);
    s.cloud_res_div = max(s.cloud_res_div, cloud_div[static_cast<u32>(q)]);
    // Rain streaks, screen-space reflections and local lights per frame.
    static const u32 rain_drops[4] = {5000, 9000, 16000, 24000};
    static const u32 max_lights[4] = {128, 256, 512, 1024};
    s.rain_drops = min(s.rain_drops, rain_drops[static_cast<u32>(q)]);
    s.max_lights = min(s.max_lights, max_lights[static_cast<u32>(q)]);
    static const u32 gi_rays[4] = {0, 1, 2, 3};
    s.gi_rays = min(s.gi_rays, gi_rays[static_cast<u32>(q)]);
    static const u32 local_shadows[4] = {0, 4, 8, 8};
    static const u32 local_res[4] = {256, 512, 512, 1024};
    s.local_shadows = min(s.local_shadows, local_shadows[static_cast<u32>(q)]);
    s.local_shadow_resolution = min(s.local_shadow_resolution, local_res[static_cast<u32>(q)]);
    if (q == Quality::Low) s.ssr = false;
    if (q == Quality::Low) {
        s.film_effects = false;
        s.auto_exposure = false;
    }
}

} // namespace tefra::render
