#version 460
#extension GL_GOOGLE_include_directive : require
#include "common/gpu_types.glsl"
#include "common/bindless.glsl"
#include "common/brdf.glsl"
#include "common/fog.glsl"
layout(buffer_reference, std430, buffer_reference_align = 16) readonly buffer WaterVerts { vec4 v[]; };
layout(push_constant) uniform Push { FrameBuffer frame; WaterVerts verts; uint depth; } pc;
layout(location = 0) in vec3 v_pos;
layout(location = 1) in vec2 v_world;
layout(location = 2) in float v_flow;
layout(location = 0) out vec4 o_color;

vec2 hash22(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.xx + p3.yz) * p3.zy) * 2.0 - 1.0;
}
float gnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(dot(hash22(i), f), dot(hash22(i + vec2(1, 0)), f - vec2(1, 0)), u.x),
               mix(dot(hash22(i + vec2(0, 1)), f - vec2(0, 1)), dot(hash22(i + vec2(1, 1)), f - vec2(1, 1)), u.x), u.y);
}
float ripples(vec2 p, float t, float flow) {
    vec2 q = p + vec2(0.0, -flow * t);
    return gnoise(q * 1.7 + t * 0.15) * 0.5 + gnoise(q * 4.3 - t * 0.35) * 0.25 + gnoise(q * 11.0 + t * 0.6) * 0.1;
}

void main() {
    Frame f = pc.frame.f;
    float t = f.camera_pos.w;
    // Normal from the ripple height field (finite differences).
    float e = 0.05;
    float h0 = ripples(v_world, t, v_flow);
    float hx = ripples(v_world + vec2(e, 0), t, v_flow), hz = ripples(v_world + vec2(0, e), t, v_flow);
    // Ripple strength fades with distance: sub-pixel ripples would alias into noise (TAA comes later).
    float k = 0.06 * clamp(12.0 / max(length(v_pos), 1.0), 0.15, 1.0);
    vec3 N = normalize(vec3(-(hx - h0) / e * k, 1.0, -(hz - h0) / e * k));
    vec3 V = -normalize(v_pos);
    float ndv = max(dot(N, V), 1e-3);
    // Fresnel (water F0 = 0.02).
    float F = 0.02 + 0.98 * pow(1.0 - ndv, 5.0);
    vec3 R = reflect(-V, N);
    R.y = abs(R.y);
    vec3 refl = textureLod(TEXCUBE(f.env_cube, SMP_LINEAR_CLAMP_MIP), R, 0.6).rgb * f.exposure.y;
    // Until SSR: low reflection angles see the dark tree line on the banks, not the open sky.
    float sky_vis = smoothstep(0.08, 0.45, R.y);
    vec3 treeline = sh9_eval(f, vec3(0, 1, 0)) / BRDF_PI * f.exposure.y * vec3(0.025, 0.03, 0.022);
    refl = mix(treeline, refl, sky_vis);
    // Sun glint.
    vec3 L = f.sun_dir.xyz;
    vec3 H = normalize(V + L);
    float a = 0.06;
    float spec = D_GGX(max(dot(N, H), 0.0), a) * V_SmithGGX(ndv, max(dot(N, L), 0.0), a) * max(dot(N, L), 0.0);
    refl += f.sun_color.rgb * f.sun_dir.w * spec;
    // Thickness of water along the view ray, from the scene depth behind the surface.
    vec2 uv = gl_FragCoord.xy * f.viewport.zw;
    float scene_depth = texelFetch(TEX2D(pc.depth, SMP_NEAREST_CLAMP), ivec2(gl_FragCoord.xy), 0).r;
    vec4 sp = f.inv_view_proj * vec4(uv * 2.0 - 1.0, max(scene_depth, 1e-7), 1.0);
    float behind = scene_depth <= 0.0 ? 1e4 : length(sp.xyz / sp.w);
    float thickness = max(behind - length(v_pos), 0.0);
    // Turbid, tannin-dark water: strong absorption, brownish-green scattering lit by sky and sun.
    vec3 sigma_a = vec3(0.45, 0.28, 0.32);
    vec3 T = exp(-sigma_a * thickness);
    vec3 ambient = sh9_eval(f, vec3(0, 1, 0)) / BRDF_PI * f.exposure.y;
    vec3 scatter_col = vec3(0.020, 0.024, 0.016) * (ambient + f.sun_color.rgb * f.sun_dir.w * max(L.y, 0.0) / BRDF_PI);
    vec3 body = scatter_col * (1.0 - T);
    // Premultiplied: final = dst * T(1-F) + body(1-F) + refl F.
    vec3 src = body * (1.0 - F) + refl * F;
    float alpha = 1.0 - dot(T, vec3(0.333)) * (1.0 - F);
    // Height fog between the camera and the surface.
    float Tf;
    vec3 ins;
    fog_terms(f, -V, length(v_pos), Tf, ins);
    src = src * Tf + ins * alpha;
    o_color = vec4(min(src * f.exposure.x, vec3(60000.0)), alpha);
}
