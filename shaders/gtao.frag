#version 460
#extension GL_GOOGLE_include_directive : require
#include "common/gpu_types.glsl"
#include "common/bindless.glsl"
// Ground-truth ambient occlusion (Jimenez et al. 2016), half resolution, 2 slices x 2 x 4 steps.
// Per-pixel rotation noise varies per frame; TAA integrates it.
layout(push_constant) uniform Push { FrameBuffer frame; uint depth; uint normals; float radius; vec4 sizes; } pc;
// sizes: xy = depth/normal (full) resolution, zw = AO target resolution
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_ao;
const float PI = 3.14159265358979;

vec3 view_pos(Frame f, vec2 uv, float d) {
    vec4 hp = f.inv_view_proj * vec4(uv * 2.0 - 1.0, max(d, 1e-7), 1.0);
    return (f.view * vec4(hp.xyz / hp.w, 1.0)).xyz; // camera-relative world -> view space
}
float ign(vec2 p) { return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715)))); }

void main() {
    Frame f = pc.frame.f;
    float d = textureLod(TEX2D(pc.depth, SMP_NEAREST_CLAMP), v_uv, 0.0).r;
    if (d <= 0.0) { o_ao = vec4(1.0); return; }
    vec3 P = view_pos(f, v_uv, d);
    vec3 V = normalize(-P);
    vec3 Nw = oct_decode01(textureLod(TEX2D(pc.normals, SMP_NEAREST_CLAMP), v_uv, 0.0).xy);
    vec3 N = normalize(mat3(f.view) * Nw);
    // World radius -> screen radius (uv units).
    float proj_scale = 0.5 * f.proj[1][1];
    float r_uv = pc.radius * proj_scale / max(-P.z, 0.1);
    if (r_uv < 1.0 / pc.sizes.y) { o_ao = vec4(1.0); return; }
    r_uv = min(r_uv, 0.15);
    float frame = f.exposure.w;
    float noise_a = ign(gl_FragCoord.xy + frame * 5.588238);
    float noise_s = ign(gl_FragCoord.yx * 1.31 + frame * 7.13);
    const int SLICES = 2, STEPS = 4;
    float vis = 0.0;
    for (int sl = 0; sl < SLICES; ++sl) {
        float phi = (float(sl) + noise_a) / float(SLICES) * PI;
        vec2 dir = vec2(cos(phi), sin(phi));
        vec3 dir3 = vec3(dir, 0.0);
        vec3 ortho = dir3 - dot(dir3, V) * V;
        vec3 axis = normalize(cross(dir3, V));
        vec3 projN = N - axis * dot(N, axis);
        float projLen = length(projN) + 1e-5;
        float cos_n = clamp(dot(projN, V) / projLen, -1.0, 1.0);
        float n = sign(dot(projN, ortho)) * acos(cos_n);
        float h[2];
        for (int side = 0; side < 2; ++side) {
            float sgn = side == 0 ? 1.0 : -1.0;
            float best = -1.0;
            for (int st = 0; st < STEPS; ++st) {
                float t = (float(st) + noise_s) / float(STEPS);
                t *= t; // denser near the pixel
                vec2 suv = v_uv + vec2(dir.x, -dir.y) * sgn * r_uv * t; // screen y is down
                if (any(lessThan(suv, vec2(0.0))) || any(greaterThan(suv, vec2(1.0)))) continue;
                float sd = textureLod(TEX2D(pc.depth, SMP_NEAREST_CLAMP), suv, 0.0).r;
                vec3 S = view_pos(f, suv, sd);
                vec3 dv = S - P;
                float len = length(dv);
                float c = dot(dv, V) / max(len, 1e-4);
                float fall = clamp(1.0 - len * len / (pc.radius * pc.radius), 0.0, 1.0);
                best = max(best, mix(-1.0, c, fall));
            }
            h[side] = best;
        }
        float h0 = n + max(-acos(clamp(h[1], -1.0, 1.0)) - n, -0.5 * PI);
        float h1 = n + min(acos(clamp(h[0], -1.0, 1.0)) - n, 0.5 * PI);
        float sin_n = sin(n);
        vis += projLen * 0.25 * ((-cos(2.0 * h0 - n) + cos_n + 2.0 * h0 * sin_n) + (-cos(2.0 * h1 - n) + cos_n + 2.0 * h1 * sin_n));
    }
    float ao = clamp(vis / float(SLICES), 0.0, 1.0);
    o_ao = vec4(ao, ao, ao, 1.0);
}
