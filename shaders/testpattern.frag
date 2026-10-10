#version 460
#extension GL_GOOGLE_include_directive : require
// M0 test pattern: analytic hazy sky + 1 m checker ground plane (y = 0) with distance fog.
// Placeholder for the physical atmosphere of M2; here it validates axes, scale and the output path.
#include "common/common.glsl"
#include "common/color.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(push_constant) uniform Push {
    mat4 inv_view_proj; // camera-relative
    vec4 camera;        // xyz world position, w time
    vec4 sun;           // xyz towards the sun, w exposure
    vec4 params;        // x checker size (m)
} pc;

const vec3 kSunColor = vec3(1.0, 0.88, 0.72);
const vec3 kZenith = vec3(0.10, 0.18, 0.34);
const vec3 kHorizon = vec3(0.55, 0.60, 0.66);
const float kFogDensity = 0.0045; // 1/m: cold morning haze (visibility ~ 650 m)

float hg_phase(float mu, float g) {
    float g2 = g * g;
    return (1.0 - g2) / (4.0 * PI * pow(1.0 + g2 - 2.0 * g * mu, 1.5));
}

vec3 sky_radiance(vec3 d, vec3 s, bool with_disk) {
    float mu = dot(d, s);
    float h = max(d.y, 0.0);
    vec3 base = mix(kHorizon, kZenith, pow(h, 0.4));
    base *= 0.9 + 0.1 * (0.75 * (1.0 + mu * mu)); // Rayleigh-like brightening
    base += kSunColor * hg_phase(mu, 0.8) * 0.25 * (1.0 - 0.7 * h); // Mie forward glow
    // Sun disk: angular radius 0.265 deg (cos = 0.9999893), slightly soft edge.
    if (with_disk) base += kSunColor * smoothstep(0.999986, 0.999991, mu) * 60.0;
    return base;
}

// Box-filtered checkerboard (anti-aliased with screen-space derivatives).
float checker(vec2 p, vec2 ddx, vec2 ddy) {
    vec2 w = max(abs(ddx), abs(ddy)) + 1e-4;
    vec2 i = 2.0 * (abs(fract((p - 0.5 * w) * 0.5) - 0.5) - abs(fract((p + 0.5 * w) * 0.5) - 0.5)) / w;
    return 0.5 - 0.5 * i.x * i.y;
}

// Filtered line of half-width hw around coordinate 0.
float line_mask(float x, float dx, float hw) {
    float w = abs(dx) + 1e-4;
    return clamp((hw + 0.5 * w - abs(x)) / w, 0.0, 1.0);
}

void main() {
    vec2 ndc = v_uv * 2.0 - 1.0;
    vec4 p = pc.inv_view_proj * vec4(ndc, 1.0, 1.0); // depth 1 = near plane (reverse-Z)
    vec3 dir = normalize(p.xyz / p.w);
    vec3 s = normalize(pc.sun.xyz);
    float cam_h = pc.camera.y;

    vec3 color;
    if (dir.y < -1e-5 && cam_h > 0.0) {
        float t = cam_h / -dir.y;
        vec2 xz = pc.camera.xz + dir.xz * t;
        vec2 cell = xz / pc.params.x;
        float c = checker(cell, dFdx(cell), dFdy(cell));
        vec3 albedo = mix(vec3(0.13, 0.12, 0.11), vec3(0.24, 0.23, 0.21), c);
        // Axis markers: +X red line (z = 0), -Z blue line (x = 0).
        albedo = mix(albedo, vec3(0.45, 0.06, 0.04), line_mask(xz.y, fwidth(xz.y), 0.03) * step(0.0, xz.x));
        albedo = mix(albedo, vec3(0.04, 0.10, 0.45), line_mask(xz.x, fwidth(xz.x), 0.03) * step(xz.y, 0.0));
        float ndl = max(s.y, 0.0);
        vec3 ambient = mix(kHorizon, kZenith, 0.5) * 0.9;
        color = albedo * (kSunColor * 2.2 * ndl + ambient);
        float fog = 1.0 - exp(-t * kFogDensity);
        vec3 haze = sky_radiance(normalize(vec3(dir.x, 0.0, dir.z)), s, false);
        color = mix(color, haze, fog);
    } else {
        color = sky_radiance(dir, s, true);
    }

    color = tonemap_aces_fit(color * pc.sun.w);
    // Tiny deterministic dither against 8-bit banding in the sky gradient.
    float n = hash_2d(uvec2(gl_FragCoord.xy)) - 0.5;
    color += n * (1.5 / 255.0) * color;
    o_color = vec4(color, 1.0);
}
