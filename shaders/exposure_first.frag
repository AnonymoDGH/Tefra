#version 460
#extension GL_GOOGLE_include_directive : require
#include "common/bindless.glsl"
// Auto exposure, first level: mean log2 luminance over each 4x4 block of the resolved HDR (16 point taps).
// The mean is taken in the log domain so that a few very bright pixels do not dominate the key.
layout(push_constant) uniform Push { uint src; } pc;
layout(location = 0) out float o_log2;

void main() {
    const ivec2 size = textureSize(TEX2D(pc.src, SMP_NEAREST_CLAMP), 0);
    const ivec2 base = ivec2(gl_FragCoord.xy) * 4;
    float sum = 0.0;
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            const ivec2 p = min(base + ivec2(x, y), size - 1);
            const vec3 c = texelFetch(TEX2D(pc.src, SMP_NEAREST_CLAMP), p, 0).rgb;
            sum += log2(max(dot(c, vec3(0.2126, 0.7152, 0.0722)), 1e-4));
        }
    }
    o_log2 = sum * (1.0 / 16.0);
}
