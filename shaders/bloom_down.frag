#version 460
#extension GL_GOOGLE_include_directive : require
#include "common/bindless.glsl"
// Bloom downsample: 13-tap filter of Jimenez (SIGGRAPH 2014, "Next Generation Post Processing in Call of Duty:
// Advanced Warfare"). Five 2x2 groups (centre 0.5, four corners 0.125 each) sum to 1: mean energy is preserved.
// karis != 0 (first level only): each group is weighted by 1/(1+luma), which damps single-pixel spikes.
layout(push_constant) uniform Push { uint src; uint karis; } pc;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

void main() {
    const vec2 ts = 1.0 / vec2(textureSize(TEX2D(pc.src, SMP_LINEAR_CLAMP), 0));
    // Offsets are in source texels from the destination texel centre. Bilinear taps.
    vec3 a = textureLod(TEX2D(pc.src, SMP_LINEAR_CLAMP), v_uv + vec2(-2.0, 2.0) * ts, 0.0).rgb;
    vec3 b = textureLod(TEX2D(pc.src, SMP_LINEAR_CLAMP), v_uv + vec2(0.0, 2.0) * ts, 0.0).rgb;
    vec3 c = textureLod(TEX2D(pc.src, SMP_LINEAR_CLAMP), v_uv + vec2(2.0, 2.0) * ts, 0.0).rgb;
    vec3 d = textureLod(TEX2D(pc.src, SMP_LINEAR_CLAMP), v_uv + vec2(-2.0, 0.0) * ts, 0.0).rgb;
    vec3 e = textureLod(TEX2D(pc.src, SMP_LINEAR_CLAMP), v_uv, 0.0).rgb;
    vec3 f = textureLod(TEX2D(pc.src, SMP_LINEAR_CLAMP), v_uv + vec2(2.0, 0.0) * ts, 0.0).rgb;
    vec3 g = textureLod(TEX2D(pc.src, SMP_LINEAR_CLAMP), v_uv + vec2(-2.0, -2.0) * ts, 0.0).rgb;
    vec3 h = textureLod(TEX2D(pc.src, SMP_LINEAR_CLAMP), v_uv + vec2(0.0, -2.0) * ts, 0.0).rgb;
    vec3 i = textureLod(TEX2D(pc.src, SMP_LINEAR_CLAMP), v_uv + vec2(2.0, -2.0) * ts, 0.0).rgb;
    vec3 j = textureLod(TEX2D(pc.src, SMP_LINEAR_CLAMP), v_uv + vec2(-1.0, 1.0) * ts, 0.0).rgb;
    vec3 k = textureLod(TEX2D(pc.src, SMP_LINEAR_CLAMP), v_uv + vec2(1.0, 1.0) * ts, 0.0).rgb;
    vec3 l = textureLod(TEX2D(pc.src, SMP_LINEAR_CLAMP), v_uv + vec2(-1.0, -1.0) * ts, 0.0).rgb;
    vec3 m = textureLod(TEX2D(pc.src, SMP_LINEAR_CLAMP), v_uv + vec2(1.0, -1.0) * ts, 0.0).rgb;

    const vec3 g_center = (j + k + l + m) * 0.25;
    const vec3 g_tl = (a + b + d + e) * 0.25;
    const vec3 g_tr = (b + c + e + f) * 0.25;
    const vec3 g_bl = (d + e + g + h) * 0.25;
    const vec3 g_br = (e + f + h + i) * 0.25;

    vec3 r;
    if (pc.karis != 0u) {
        const float wc = 0.5 / (1.0 + luma(g_center));
        const float w1 = 0.125 / (1.0 + luma(g_tl));
        const float w2 = 0.125 / (1.0 + luma(g_tr));
        const float w3 = 0.125 / (1.0 + luma(g_bl));
        const float w4 = 0.125 / (1.0 + luma(g_br));
        r = (g_center * wc + g_tl * w1 + g_tr * w2 + g_bl * w3 + g_br * w4) / (wc + w1 + w2 + w3 + w4);
    } else {
        r = g_center * 0.5 + (g_tl + g_tr + g_bl + g_br) * 0.125;
    }
    o_color = vec4(r, 1.0);
}
