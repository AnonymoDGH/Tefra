#version 460
#extension GL_GOOGLE_include_directive : require
#include "common/gpu_types.glsl"
#include "common/bindless.glsl"
#include "common/agx.glsl"
#include "common/common.glsl"
layout(push_constant) uniform Push { FrameBuffer frame; uint hdr; } pc;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
void main() {
    vec3 c = texelFetch(TEX2D(pc.hdr, SMP_NEAREST_CLAMP), ivec2(gl_FragCoord.xy), 0).rgb;
    c = agx(c); // input is pre-exposed by the lighting pass
    // Triangular dither (1 LSB) against banding in dark gradients.
    float n = hash_2d(uvec2(gl_FragCoord.xy)) + hash_2d(uvec2(gl_FragCoord.yx) + 7u) - 1.0;
    c = max(c + n * (1.0 / 255.0) * sqrt(c + 1e-4), vec3(0.0));
    o_color = vec4(c, 1.0);
}
