#version 460
#extension GL_GOOGLE_include_directive : require
#include "common/gpu_types.glsl"
#include "common/bindless.glsl"
#include "common/agx.glsl"
#include "common/common.glsl"
// Push layout must match PostPush in renderer.cpp (std430, 32 bytes).
layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer ExposureBuffer { float ev; uint last_frame; uint valid; uint pad; };
layout(push_constant) uniform Push {
    FrameBuffer frame;
    uint hdr;
    uint bloom;
    ExposureBuffer exposure;
    uint flags; // kFlagBloom | kFlagAutoExposure | kFlagFilm
    float bloom_mix;
} pc;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

const uint kFlagBloom = 1u, kFlagAutoExposure = 2u, kFlagFilm = 4u;
const float kVignette = 0.35;      // blend towards the physical cos^4 falloff (1 = full)
const float kCaPixels = 1.0;      // lateral chromatic aberration at the corners, in pixels
const float kGrainShadow = 0.05;  // grain amplitude (relative) in shadows
const float kGrainHighlight = 0.015;

vec3 hdr_load(ivec2 p) { return texelFetch(TEX2D(pc.hdr, SMP_NEAREST_CLAMP), p, 0).rgb; }

void main() {
    const bool film = (pc.flags & kFlagFilm) != 0u;
    const ivec2 px = ivec2(gl_FragCoord.xy);
    vec3 c;
    if (film) {
        // Lateral chromatic aberration: red and blue sample radially shifted positions, shift ~ r^2 (zero at centre).
        const vec2 size = vec2(textureSize(TEX2D(pc.hdr, SMP_NEAREST_CLAMP), 0));
        const vec2 rel = gl_FragCoord.xy - 0.5 * size;
        const float rn = length(rel) / length(0.5 * size); // 0 at the centre, 1 at the corners
        const vec2 dir = rel / max(length(rel), 1e-4);
        const vec2 shift = dir * (kCaPixels * rn * rn) / size;
        c.r = textureLod(TEX2D(pc.hdr, SMP_LINEAR_CLAMP), v_uv + shift, 0.0).r;
        c.g = hdr_load(px).g;
        c.b = textureLod(TEX2D(pc.hdr, SMP_LINEAR_CLAMP), v_uv - shift, 0.0).b;
    } else {
        c = hdr_load(px); // pre-exposed
    }
    // Auto exposure: EV compensation from the adapted state (already clamped to +-2 EV by the adapt pass).
    // The buffer always exists (per slot), so the pointer is always valid even when the flag is off.
    float comp = 1.0;
    if ((pc.flags & kFlagAutoExposure) != 0u) comp = exp2(pc.exposure.ev);
    c *= comp;
    if ((pc.flags & kFlagBloom) != 0u) {
        const vec3 b = textureLod(TEX2D(pc.bloom, SMP_LINEAR_CLAMP), v_uv, 0.0).rgb * comp;
        c = mix(c, b, pc.bloom_mix);
    }
    if (film) {
        // Optical falloff: a thin lens with a flat field loses cos^4(theta) of the light. The projection gives tan.
        const vec2 t = vec2((2.0 * v_uv.x - 1.0) / pc.frame.f.proj[0][0], (2.0 * v_uv.y - 1.0) / pc.frame.f.proj[1][1]);
        const float cos2 = 1.0 / (1.0 + dot(t, t));
        c *= mix(1.0, cos2 * cos2, kVignette);
    }
    c = agx(c); // input is pre-exposed by the lighting pass
    if (film) {
        // Grain on the display-referred value: stronger in shadows, animated per frame (triangular noise, [-1, 1]).
        const float y = dot(c, vec3(0.2126, 0.7152, 0.0722));
        const float amp = mix(kGrainShadow, kGrainHighlight, smoothstep(0.0, 0.6, y));
        const uint fi = uint(pc.frame.f.exposure.w);
        const uvec2 p = uvec2(gl_FragCoord.xy) + uvec2(fi * 0x9E3779B9u, fi * 0x85EBCA6Bu);
        const float g = hash_2d(p) + hash_2d(p.yx ^ 0x5bd1e995u) - 1.0;
        c += g * amp * sqrt(c + 0.02);
    }
    // Triangular dither (1 LSB) against banding in dark gradients.
    float n = hash_2d(uvec2(gl_FragCoord.xy)) + hash_2d(uvec2(gl_FragCoord.yx) + 7u) - 1.0;
    c = max(c + n * (1.0 / 255.0) * sqrt(c + 1e-4), vec3(0.0));
    o_color = vec4(c, 1.0);
}
