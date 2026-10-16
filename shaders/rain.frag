#version 460
#extension GL_GOOGLE_include_directive : require
#include "common/gpu_types.glsl"
layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec3 v_color;
layout(location = 2) in float v_alpha;
layout(location = 0) out vec4 o_color;
layout(location = 1) out vec4 o_reactive;
void main() {
    // Soft across the streak, tapered at both ends (motion-blurred drop).
    float a = (1.0 - v_uv.x * v_uv.x) * sin(clamp(v_uv.y, 0.0, 1.0) * 3.14159) * v_alpha * 0.55;
    if (a < 0.002) discard;
    // Water is transparent: the streak adds the light it scatters and dims what is behind only slightly.
    o_color = vec4(min(v_color * a, vec3(60000.0)), a * 0.15);
    o_reactive = vec4(a, 0.0, 0.0, a);
}
