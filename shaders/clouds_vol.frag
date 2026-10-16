#version 460
#extension GL_GOOGLE_include_directive : require
#include "common/gpu_types.glsl"
#include "common/bindless.glsl"
#include "common/brdf.glsl"
#include "common/clouds.glsl"
// Volumetric cumulus (reduced resolution, sky pixels only), after Schneider 2015 / Hillaire 2016:
// - the 2D coverage field of the cloud layer (clouds.glsl) is the weather map, a height profile gives the heaps
//   their rounded bases and domed tops, 3D value noise erodes the edges;
// - view ray marched through the slab [altitude, altitude + thickness] with per-pixel jitter (TAA resolves it);
// - sun light: a few cone samples towards the light, Beer-Powder, dual-lobe Henyey-Greenstein; sky ambient grows
//   with height inside the cloud.
// Output: rgb = in-scattered radiance (not exposed), a = transmittance. Distant clouds fade into the haze.
layout(push_constant) uniform Push { FrameBuffer frame; uint depth; uint pad; } pc;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_clouds;

float vc_hash(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.zyx + 31.32);
    return fract((p.x + p.y) * p.z);
}
float vc_noise(vec3 p) {
    vec3 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = mix(mix(vc_hash(i), vc_hash(i + vec3(1, 0, 0)), f.x), mix(vc_hash(i + vec3(0, 1, 0)), vc_hash(i + vec3(1, 1, 0)), f.x), f.y);
    float b = mix(mix(vc_hash(i + vec3(0, 0, 1)), vc_hash(i + vec3(1, 0, 1)), f.x), mix(vc_hash(i + vec3(0, 1, 1)), vc_hash(i + vec3(1, 1, 1)), f.x), f.y);
    return mix(a, b, f.z);
}
float vc_fbm(vec3 p) { return 0.55 * vc_noise(p) + 0.3 * vc_noise(p * 2.07 + 5.3) + 0.15 * vc_noise(p * 4.3 + 1.7); }
float ign(vec2 p) { return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715)))); }

// Density at world point wp (h = 0 at the cloud base, 1 at the top). cheap: skip the erosion (light samples).
float vc_density(Frame f, vec3 wp, float h, bool cheap) {
    if (h <= 0.0 || h >= 1.0) return 0.0;
    vec2 q = wp.xz + vec2(f.clouds.w, f.clouds.w * 0.3);
    float cov = cl_density(f, q, cheap ? 3 : 4);
    if (cov <= 0.0) return 0.0;
    // Denser coverage builds taller heaps: the top of this column is at `top` (fraction of the slab).
    float top = mix(0.25, 1.0, cov);
    float prof = smoothstep(0.0, 0.08, h) * (1.0 - smoothstep(top * 0.55, top, h));
    float d = cov * prof;
    if (d <= 0.0 || cheap) return d;
    // Erosion: billowy 3D noise eats the edges (more at the bottom: wispy bases, crisp domes).
    vec3 p = vec3(q.x, wp.y, q.y) / (f.clouds.z * 0.13);
    float n = vc_fbm(p);
    return max(d - (1.0 - n) * mix(0.45, 0.25, h) * (1.0 - d * 0.5), 0.0);
}

void main() {
    Frame f = pc.frame.f;
    o_clouds = vec4(0.0, 0.0, 0.0, 1.0);
    // Only where the sky shows (any depth = geometry in front).
    float depth = textureLod(TEX2D(pc.depth, SMP_NEAREST_CLAMP), v_uv, 0.0).r;
    if (depth > 0.0) return;
    vec2 ndc = v_uv * 2.0 - 1.0;
    vec4 hp = f.inv_view_proj * vec4(ndc, 1e-7, 1.0);
    vec3 dir = normalize(hp.xyz / hp.w);
    if (dir.y <= 0.01) return;

    const float base = f.clouds.y, thick = f.vclouds.y, top = base + thick;
    const float cy = f.camera_pos.y;
    float t0 = max((base - cy) / dir.y, 0.0), t1 = (top - cy) / dir.y;
    if (t1 <= t0) return;
    // Long grazing paths are capped: far clouds fade into the haze anyway.
    t1 = min(t1, t0 + max(thick * 4.0, 6000.0));
    const int steps = int(f.vclouds.z);
    const float dt = (t1 - t0) / float(steps);
    float t = t0 + dt * ign(gl_FragCoord.xy + f.exposure.w * 5.588);

    const vec3 L = f.sun_dir.xyz;
    const vec3 sunE = f.sun_color.rgb * f.sun_dir.w;
    const vec3 sky_amb = sh9_eval(f, vec3(0, 1, 0)) / BRDF_PI * f.exposure.y;
    const vec3 tint = f.cloud_look.rgb;
    const float sigma = 0.035 * max(f.cloud_look.w, 0.05); // extinction per unit density (1/m)
    const float mu = dot(dir, L);
    const float g1 = 0.7, g2 = -0.25;
    const float hg1 = (1.0 - g1 * g1) / (4.0 * BRDF_PI * pow(1.0 + g1 * g1 - 2.0 * g1 * mu, 1.5));
    const float hg2 = (1.0 - g2 * g2) / (4.0 * BRDF_PI * pow(1.0 + g2 * g2 - 2.0 * g2 * mu, 1.5));
    const float phase = mix(hg2, hg1, 0.65);
    const float light_step = thick * 0.09;

    vec3 scat = vec3(0.0);
    float T = 1.0;
    float first = -1.0;
    for (int i = 0; i < steps && T > 0.02; ++i, t += dt) {
        vec3 wp = f.camera_pos.xyz + dir * t;
        float h = (wp.y - base) / thick;
        float d = vc_density(f, wp, h, false);
        if (d <= 0.001) continue;
        if (first < 0.0) first = t;
        // Light towards the sun: optical depth from a few samples, the last one far up the cone.
        float od = 0.0;
        for (int k = 1; k <= 4; ++k) {
            vec3 lp = wp + L * light_step * float(k * k) * 0.6;
            od += vc_density(f, lp, (lp.y - base) / thick, true) * light_step * float(2 * k - 1) * 0.6;
        }
        float Tl = exp(-sigma * od) + 0.25 * exp(-sigma * od * 0.25); // multiple-scattering approximation
        float powder = 1.0 - exp(-sigma * d * dt * 2.0);
        // Sun (single + approximate multiple scattering), sky light from above, and light bounced off the sunlit
        // ground into the cloud bases (grey bottoms, not black ones).
        vec3 ground = sunE * max(L.y, 0.0) * (0.25 / BRDF_PI) * (1.0 - h) * 0.6;
        vec3 S = (sunE * Tl * phase * mix(1.0, powder, 0.5) * 2.2 + sky_amb * mix(0.65, 1.25, h) + ground) * tint;
        float Ts = exp(-sigma * d * dt);
        scat += T * S * (1.0 - Ts);
        T *= Ts;
    }
    if (first < 0.0) return;
    // Aerial perspective: distant clouds fade into the sky (the composite shows the sky through them).
    float fade = exp(-first / 30000.0) * smoothstep(0.01, 0.07, dir.y);
    o_clouds = vec4(min(scat * fade, vec3(60000.0)), mix(1.0, T, fade));
}
