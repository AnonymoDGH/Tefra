#version 460
#extension GL_GOOGLE_include_directive : require
#include "common/bindless.glsl"
// Auto exposure reduction: each output texel is the mean of a 4x4 block of the previous level. Four bilinear taps
// placed on the block's interior boundaries average exactly 2x2 texels each, so the four taps cover the 4x4 block.
layout(push_constant) uniform Push { uint src; } pc;
layout(location = 0) out float o_log2;

void main() {
    const vec2 size = vec2(textureSize(TEX2D(pc.src, SMP_LINEAR_CLAMP), 0));
    const vec2 p0 = floor(gl_FragCoord.xy) * 4.0 + 1.0; // boundary between texels 4o and 4o+1
    float sum = 0.0;
    sum += textureLod(TEX2D(pc.src, SMP_LINEAR_CLAMP), p0 / size, 0.0).r;
    sum += textureLod(TEX2D(pc.src, SMP_LINEAR_CLAMP), (p0 + vec2(2.0, 0.0)) / size, 0.0).r;
    sum += textureLod(TEX2D(pc.src, SMP_LINEAR_CLAMP), (p0 + vec2(0.0, 2.0)) / size, 0.0).r;
    sum += textureLod(TEX2D(pc.src, SMP_LINEAR_CLAMP), (p0 + vec2(2.0, 2.0)) / size, 0.0).r;
    o_log2 = sum * 0.25;
}
