#version 460
#extension GL_GOOGLE_include_directive : require
#include "common/gpu_types.glsl"
#include "common/bindless.glsl"
#include "common/brdf.glsl"
#include "common/fog.glsl"
struct Particle {
    vec4 pos_size;
    vec4 color;
    vec4 vel_rot;
    uint shape;
    float emission;
    float stretch;
    float brightness;
};
layout(buffer_reference, std430, buffer_reference_align = 16) readonly buffer Particles { Particle p[]; };
layout(push_constant) uniform Push { FrameBuffer frame; Particles parts; uint depth; } pc;
layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;
layout(location = 2) in vec3 v_pos;
layout(location = 3) flat in uint v_shape;
layout(location = 4) flat in vec3 v_params;
layout(location = 5) flat in float v_seed;
layout(location = 0) out vec4 o_color;
layout(location = 1) out vec4 o_reactive; // TAA: trust the current frame here

float hash12(vec2 p) {
    vec3 q = fract(vec3(p.xyx) * 0.1031);
    q += dot(q, q.yzx + 33.33);
    return fract((q.x + q.y) * q.z);
}
float vnoise(vec2 p) {
    vec2 i = floor(p), u = fract(p);
    u = u * u * (3.0 - 2.0 * u);
    return mix(mix(hash12(i), hash12(i + vec2(1, 0)), u.x), mix(hash12(i + vec2(0, 1)), hash12(i + vec2(1, 1)), u.x), u.y);
}

void main() {
    Frame f = pc.frame.f;
    float r = length(v_uv);
    if (r > 1.0) discard;
    float a;
    vec3 tint = vec3(1.0);
    float glow = 0.0; // extra emissive core
    if (v_shape == 1u) { // smoke: soft puff broken up by noise
        vec2 q = v_uv * 1.7 + v_seed * 31.0;
        float n = 0.6 * vnoise(q) + 0.3 * vnoise(q * 2.3) + 0.1 * vnoise(q * 5.1);
        a = smoothstep(1.0, 0.2, r) * smoothstep(0.25, 0.75, n + 0.35 * (1.0 - r));
    } else if (v_shape == 2u) { // spark: hot core, quick falloff
        a = exp(-r * r * 9.0);
        glow = exp(-r * r * 40.0);
    } else if (v_shape == 3u) { // star: four points plus a core
        float arms = max(exp(-abs(v_uv.x) * 14.0) * (1.0 - abs(v_uv.y)), exp(-abs(v_uv.y) * 14.0) * (1.0 - abs(v_uv.x)));
        a = max(arms, exp(-r * r * 12.0));
        glow = exp(-r * r * 30.0);
    } else if (v_shape == 4u) { // fire: flickering tongue, white-yellow core to red edge
        vec2 q = vec2(v_uv.x * 2.0, v_uv.y * 1.2 - f.camera_pos.w * 2.5) + v_seed * 17.0;
        float n = vnoise(q * 2.0) * 0.6 + vnoise(q * 4.3) * 0.4;
        float shape = smoothstep(1.0, 0.1, r + (n - 0.5) * 0.6 - v_uv.y * 0.25);
        a = shape;
        tint = mix(vec3(1.0, 0.25, 0.05), vec3(1.0, 0.85, 0.45), smoothstep(0.2, 0.9, shape));
        glow = shape * shape;
    } else if (v_shape == 5u) { // snow: small hard flake
        a = smoothstep(1.0, 0.6, r);
    } else if (v_shape == 6u) { // ring
        a = exp(-pow((r - 0.75) * 9.0, 2.0));
    } else { // circle
        a = smoothstep(1.0, 0.0, r);
        a *= a;
    }
    a *= v_color.a;
    // Soft particles: fade where the quad meets the scene.
    vec2 uv = gl_FragCoord.xy * f.viewport.zw;
    float scene_depth = texelFetch(TEX2D(pc.depth, SMP_NEAREST_CLAMP), ivec2(gl_FragCoord.xy), 0).r;
    vec4 sp = f.inv_view_proj * vec4(uv * 2.0 - 1.0, max(scene_depth, 1e-7), 1.0);
    float behind = scene_depth <= 0.0 ? 1e4 : length(sp.xyz / sp.w);
    float dist = length(v_pos);
    a *= clamp((behind - dist) / max(v_params.z * 0.5, 0.05), 0.0, 1.0);
    // Near the camera: fade instead of filling the screen.
    a *= smoothstep(0.2, 1.0, dist);
    if (a < 0.002) discard;

    vec3 base = v_color.rgb * tint;
    // Lit (smoke, dust): sky ambient plus wrapped sun.
    vec3 L = f.sun_dir.xyz;
    vec3 ambient = sh9_eval(f, vec3(0, 1, 0)) / BRDF_PI * f.exposure.y;
    vec3 lit = base * (ambient + f.sun_color.rgb * f.sun_dir.w * (0.35 + 0.4 * max(L.y, 0.0)) / BRDF_PI) * f.exposure.x;
    // Emissive: display-referred so it glows at any exposure (2 = clearly brighter than a sunlit white wall).
    vec3 emit = base * v_params.y * (1.5 + 3.0 * glow);
    float e = v_params.x;
    vec3 col = mix(lit, emit, e);
    float Tf;
    vec3 ins;
    fog_terms(f, normalize(v_pos), dist, Tf, ins);
    col = col * Tf + ins * f.exposure.x * (1.0 - e);
    // Premultiplied: additive share grows with LightEmission (alpha = 0 is pure additive).
    o_color = vec4(min(col * a, vec3(60000.0)), a * (1.0 - e));
    float m = clamp(a * (0.25 + 1.5 * e), 0.0, 1.0);
    o_reactive = vec4(m, 0.0, 0.0, m);
}
