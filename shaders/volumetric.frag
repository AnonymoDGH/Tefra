#version 460
#extension GL_GOOGLE_include_directive : require
#include "common/gpu_types.glsl"
#include "common/bindless.glsl"
#include "common/fog.glsl"
#include "common/shadow.glsl"
// Volumetric fog / god rays at half resolution (Wronski 2014; Hillaire 2015 step integration).
// Marches the view ray up to min(scene depth, kMaxDist). Output: rgb = in-scatter (pre-exposed), a = transmittance.
// Per-step transmittance comes from the exact exponential-density integral, so the result is energy-consistent
// with the analytic height fog when the sun term is unshadowed.
layout(push_constant) uniform Push { FrameBuffer frame; uint depth; } pc;
layout(location = 0) out vec4 o_vol;

const float kMaxDist = 150.0; // m
const int kSteps = 16;

float ign(vec2 p) { return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715)))); }

void main() {
    Frame f = pc.frame.f;
    const ivec2 ext = ivec2(f.viewport.xy);
    const ivec2 hq = ivec2(gl_FragCoord.xy);
    // Depth is read at the bottom-right full-resolution pixel of the 2x2 block (the lighting pass upsamples with
    // the same mapping).
    const ivec2 fp = clamp(hq * 2 + 1, ivec2(0), ext - 1);
    const float depth = texelFetch(TEX2D(pc.depth, SMP_NEAREST_CLAMP), fp, 0).r;
    const vec2 ndc = (vec2(fp) + 0.5) / f.viewport.xy * 2.0 - 1.0;
    const vec4 hp = f.inv_view_proj * vec4(ndc, max(depth, 1e-7), 1.0);
    const vec3 P = hp.xyz / hp.w;
    const vec3 dir = normalize(P);
    const float dist = depth > 0.0 ? length(P) : 1e5; // sky: infinite ray
    const float dmax = min(dist, kMaxDist);

    // Per-pixel and per-frame jitter of the sample position inside each step; TAA integrates it over frames.
    const float jit = fract(ign(vec2(hq)) + f.exposure.w * 0.618034);
    const float dt = dmax / float(kSteps);
    const vec3 amb = fog_ambient(f);
    const vec3 sun = f.sun_color.rgb * f.sun_dir.w;
    const float hg = fog_phase(f, dir);
    const bool sun_on = f.sun_dir.y > -0.05 && f.sun_dir.w > 0.0;
    vec3 inscatter = vec3(0.0);
    float t_prev = fog_transmittance(f, dir, 0.0); // = 1
    for (int i = 0; i < kSteps; ++i) {
        const float t_next = fog_transmittance(f, dir, float(i + 1) * dt);
        const vec3 x = dir * ((float(i) + jit) * dt); // camera-relative sample position
        float vis = 0.0;
        if (sun_on) {
            const float view_depth = -(f.view * vec4(x, 1.0)).z;
            vis = sun_shadow_tap(f, x, view_depth);
        }
        // Integral of sigma(s) * T(s) over the step equals T(start) - T(end) (exact for exponential density).
        inscatter += (amb + sun * (hg * vis)) * (t_prev - t_next);
        t_prev = t_next;
    }
    o_vol = vec4(inscatter * f.exposure.x, t_prev);
}
