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
    u32 flags = 0; // bit0: foliage (translucency), bit1: double-sided
    f32 translucency = 0.0f;
    u32 pad[2] = {};
};
static_assert(sizeof(MaterialGPU) == 80);
constexpr u32 kNoTexture = 0xFFFFFFFFu;

struct InstanceGPU {
    vec4 model_rows[3];  // camera-relative 3x4 model matrix, row-major rows
    vec4 normal_rows[3]; // inverse-transpose 3x3 (rows), w unused
    u32 material = 0;
    u32 pad[3] = {};
};
static_assert(sizeof(InstanceGPU) == 112);

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
    u32 brdf_lut = 0;       // bindless 2D index
    u32 shadow_atlas = 0;   // bindless 2D index
    u32 env_background = 0; // bindless 2D index: full-resolution equirect for the visible sky
    u32 shadow_flags = 0;   // bit0: PCSS penumbra, bit1: screen-space contact shadows
    f32 penumbra_scale = 1.0f;
    f32 contact_length = 0.75f; // m
    mat4 prev_view_proj;        // previous frame, in the current camera-relative space
    vec4 jitter;                // xy current jitter (NDC), zw previous
    vec4 fog;                   // x density (1/m) at reference height, y height falloff (1/m), z reference height, w g
    AtmosphereGPU atmo;
};
static_assert(sizeof(FrameConstants) % 16 == 0);

} // namespace tefra::render
