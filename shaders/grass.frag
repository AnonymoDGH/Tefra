#version 460
#extension GL_GOOGLE_include_directive : require
#include "common/gpu_types.glsl"
// G-buffer output for grass: foliage class (1/3) with translucency, so lighting adds thin-leaf transmission.
layout(location = 0) in vec3 v_pos;
layout(location = 1) in vec3 v_normal;
layout(location = 2) in vec4 v_color_ao;
layout(location = 0) out vec4 o_albedo;   // RGBA8_SRGB: base color, ambient occlusion
layout(location = 1) out vec4 o_normal;   // A2B10G10R10: oct normal, roughness, class
layout(location = 2) out vec4 o_material; // RGBA8: metallic, translucency, emissive, wetness
layout(location = 3) out vec2 o_velocity; // RG16F
void main() {
    vec3 N = normalize(v_normal);
    if (!gl_FrontFacing) N = -N;
    o_albedo = v_color_ao;
    o_normal = vec4(oct_encode01(N), 0.6, 1.0 / 3.0);
    o_material = vec4(0.0, 0.55, 0.0, 0.0);
    o_velocity = vec2(0.0);
}
