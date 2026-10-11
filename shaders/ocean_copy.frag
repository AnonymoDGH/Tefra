#version 460
#extension GL_GOOGLE_include_directive : require
#include "common/ocean.glsl"
// Copies the lit image into the ocean pass target (the water then samples the original for refraction).
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
void main() { o_color = vec4(texelFetch(TEX2D(pc.hdr, SMP_NEAREST_CLAMP), ivec2(gl_FragCoord.xy), 0).rgb, 1.0); }
