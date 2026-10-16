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
layout(push_constant) uniform Push { FrameBuffer frame; uint depth; uint pad; LightBuffer lights; uint light_count; uint pad2; } pc;
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
        // Local lights (nearest first): halos and light cones in the haze around street lamps. Isotropic-ish phase
        // with a forward lobe; shadow-mapped lights get one shadow tap.
        vec3 local = vec3(0.0);
        for (uint li = 0u; li < pc.light_count; ++li) {
            LightGPU l = pc.lights.l[li];
            vec3 d = l.pos_range.xyz - x;
            float d2 = dot(d, d), r = l.pos_range.w;
            if (d2 > r * r) continue;
            float win = clamp(1.0 - d2 * d2 / (r * r * r * r), 0.0, 1.0);
            float atten = win * win / max(d2, 0.25);
            if (l.spot.w >= -1.0)
                atten *= mix(l.extra.y, 1.0, smoothstep(l.spot.w, max(l.extra.x, l.spot.w + 1e-3), dot(-normalize(d), l.spot.xyz)));
            // Shadowed lights cast shafts through the haze (fire escapes, awnings, cars).
            if (l.extra.z >= 0.0 && atten > 0.0) atten *= local_shadow(f, int(l.extra.z), x, vec3(0.0), length(d), false);
            float cosv = dot(normalize(d), dir);
            float phase = 0.012 + 0.08 * pow(max(cosv, 0.0), 8.0);
            local += l.color.rgb * atten * phase;
        }
        inscatter += (amb + sun * (hg * vis) + local) * (t_prev - t_next);
        t_prev = t_next;
    }
    o_vol = vec4(inscatter * f.exposure.x, t_prev);
}
