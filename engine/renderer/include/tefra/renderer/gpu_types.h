#pragma once

// CPU mirrors of GPU structs (std430, shaders/common/gpu_types.glsl). Sizes are static_asserted.

#include "tefra/core/math.h"
#include "tefra/renderer/atmosphere.h"

namespace tefra::render {

// 32-byte vertex for vertex pulling.
struct Vertex {
    f32 pos[3];
    u32 normal; // octahedral snorm16x2
    f32 uv[2];
    u32 tangent; // octahedral snorm16x2
    f32 tangent_sign;
};
static_assert(sizeof(Vertex) == 32);

struct MaterialGPU {
    vec4 base_color;      // linear rgba factor
    vec4 emissive_cutoff; // rgb emissive (nits-ish factor), w = alpha cutoff (0 = opaque)
    f32 roughness = 1.0f;
    f32 metallic = 0.0f;
    f32 normal_scale = 1.0f;
    f32 occlusion_strength = 1.0f;
    u32 base_color_tex = 0; // bindless indices; 0 = error texture, kNoTexture = none
    u32 normal_tex = 0;
    u32 orm_tex = 0;
    u32 emissive_tex = 0;
    u32 flags = 0; // bit0: foliage (translucency, normal blended with the crown normal), bit1: double-sided,
                   // bit2: bark (tint + moss), bit3: normal map alpha holds baked AO (needles),
                   // bit4: procedural Part surface (pad[1] = Material enum index)
    f32 translucency = 0.0f;
    u32 pad[2] = {}; // bark: pad[0] = moss coverage (float bits)
};
static_assert(sizeof(MaterialGPU) == 80);
constexpr u32 kNoTexture = 0xFFFFFFFFu;

struct InstanceGPU {
    vec4 model_rows[3];  // camera-relative 3x4 model matrix, row-major rows
    vec4 normal_rows[3]; // inverse-transpose 3x3 (rows), w unused
    vec4 prev_rows[3];   // previous frame's model matrix, camera-relative to this frame (motion vectors)
    u32 material = 0;
    u32 flags = 0; // bit0: skinned (posed vertices; velocity from prev_rows and the previous palette)
    u32 pad[2] = {};
};
static_assert(sizeof(InstanceGPU) == 160);

// One camera-facing particle (particles pass, shaders/particle.*). World position in float (as the water).
struct ParticleGPU {
    vec4 pos_size;         // world xyz, size (m)
    vec4 color;            // linear rgb, opacity
    vec4 vel_rot;          // world velocity (m/s, for Stretch), rotation (rad)
    u32 shape = 0;         // 0 circle, 1 smoke, 2 spark, 3 star, 4 fire, 5 snow, 6 ring
    f32 emission = 0.0f;   // LightEmission: 0 blended, 1 additive
    f32 stretch = 0.0f;    // elongation along the screen velocity
    f32 brightness = 1.0f; // glow of emissive particles
};
static_assert(sizeof(ParticleGPU) == 64);

constexpr u32 kMaxCascades = 4;

struct FrameConstants {
    mat4 view_proj; // camera-relative
    mat4 inv_view_proj;
    mat4 view; // rotation only
    mat4 proj;
    mat4 cascade_view_proj[kMaxCascades]; // camera-relative, atlas UV space applied in shader
    vec4 cascade_splits;                  // view-space far distance of each cascade (m)
    vec4 cascade_atlas[kMaxCascades];     // xy offset, zw scale in atlas UV
    vec4 shadow_params;                   // x,y = atlas texel size (uv), z = cascade count, w = cascade resolution
    vec4 sun_dir;                         // xyz towards sun, w = illuminance (lux)
    vec4 sun_color;                       // linear rgb (luminance 1), w = angular radius (rad)
    vec4 camera_pos;                      // world position (float, for texturing), w = time (s)
    vec4 exposure;                        // x = exposure scale, y = ibl intensity, z = ev100, w = frame index
    vec4 viewport;                        // w, h, 1/w, 1/h
    vec4 sh[9];                           // irradiance SH9 (rgb) of the environment, cosine-convolved, pre-scaled
    u32 env_cube = 0;                     // bindless cube index (prefiltered specular)
    u32 env_mips = 1;
    u32 brdf_lut = 0;            // bindless 2D index
    u32 shadow_atlas = 0;        // bindless 2D index
    u32 env_background = 0;      // bindless 2D index: full-resolution equirect for the visible sky
    u32 canopy_map = kNoTexture; // bindless 2D index of the canopy occlusion map (canopy.h)
    u32 shadow_flags = 0;        // bit0: PCSS penumbra, bit1: screen-space contact shadows, bit2: debug sun vis
    f32 veg_lod_scale = 1.0f;    // multiplies the vegetation LOD / cull distances (quality presets)
    mat4 prev_view_proj;         // previous frame, in the current camera-relative space
    vec4 jitter;                 // xy current jitter (NDC), zw previous
    vec4 fog;                    // x density (1/m) at reference height, y height falloff (1/m), z reference height, w g
    vec4 canopy;                 // xy world xz of the canopy map's min corner, z size (m), w strength
    vec4 shadow_soft;            // x penumbra scale, y contact shadow length (m)
    vec4 ocean;                  // x still sea level (world y), y 1 = ocean present (underwater light + caustics)
    vec4 water_absorption;       // xyz 1/m
    vec4 water_scattering;       // xyz 1/m
    vec4 clouds;                 // x coverage (0 = none), y base altitude (m), z feature size (m), w drift (m)
    AtmosphereGPU atmo;
    vec4 celestial;  // x sun disc (0/1), y moon disc (0/1), z moon angular radius (rad), w sun glare strength
    vec4 moon;       // xyz towards the moon, w moon phase (0 new, 0.5 full)
    vec4 stars;      // x star count (whole sphere), y brightness, z night (0 day .. 1 night), w unused
    vec4 fog_tint;   // xyz colour of the height fog
    vec4 cloud_look; // xyz colour of the cumulus, w optical depth multiplier
    vec4 cloud_wind; // xy unit wind direction (world x, z)
    vec4 sun_true;   // xyz towards the sun itself (sun_dir is the moon at night)
    vec4 ambient;    // xyz flat indoor/bounce irradiance (lux), added to the image-based ambient
    vec4 vclouds;    // x volumetric clouds on (1), y thickness (m), z view steps, w unused
};

// Local light as the lighting pass reads it (camera-relative position).
struct LightGPU {
    vec4 pos_range; // xyz camera-relative position (m), w range (m)
    vec4 color;     // rgb = linear colour x intensity (cd)
    vec4 spot;      // xyz spot axis, w cos outer (< -1: point light)
    vec4 extra;     // x cos inner
};
static_assert(sizeof(LightGPU) == 64);
static_assert(sizeof(FrameConstants) % 16 == 0);

} // namespace tefra::render
