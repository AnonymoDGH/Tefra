#version 460
#extension GL_GOOGLE_include_directive : require
#include "common/bindless.glsl"
// Bloom upsample: 3x3 tent of the coarser level, blended 50/50 with the same-level downsample. A uniform image
// stays uniform through every level (the blend weights sum to 1), so the bloom adds no net energy.
layout(push_constant) uniform Push { uint coarse; uint fine; } pc;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

vec3 tap(vec2 o, vec2 ts) { return textureLod(TEX2D(pc.coarse, SMP_LINEAR_CLAMP), v_uv + o * ts, 0.0).rgb; }

void main() {
    const vec2 ts = 1.0 / vec2(textureSize(TEX2D(pc.coarse, SMP_LINEAR_CLAMP), 0));
    vec3 t = tap(vec2(0.0), ts) * 4.0;
    t += (tap(vec2(-1.0, 0.0), ts) + tap(vec2(1.0, 0.0), ts) + tap(vec2(0.0, -1.0), ts) + tap(vec2(0.0, 1.0), ts)) * 2.0;
    t += tap(vec2(-1.0, -1.0), ts) + tap(vec2(1.0, -1.0), ts) + tap(vec2(-1.0, 1.0), ts) + tap(vec2(1.0, 1.0), ts);
    t *= 1.0 / 16.0;
    const vec3 f = textureLod(TEX2D(pc.fine, SMP_LINEAR_CLAMP), v_uv, 0.0).rgb;
    o_color = vec4(mix(f, t, 0.5), 1.0);
}
