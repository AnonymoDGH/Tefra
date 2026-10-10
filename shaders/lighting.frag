#version 460
#extension GL_GOOGLE_include_directive : require
#include "common/gpu_types.glsl"
#include "common/bindless.glsl"
#include "common/brdf.glsl"
#include "common/cube.glsl"
layout(push_constant) uniform Push { FrameBuffer frame; uint depth; uint rt0; uint rt1; uint rt2; } pc;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

// shadow_params: x,y = atlas texel size (uv), z = cascade count, w = cascade resolution (texels).
float shadow_cascade(Frame f, vec3 p, vec3 n, int c) {
    vec2 texel = f.shadow_params.xy;
    mat4 m = f.cascade_view_proj[c];
    float radius = 1.0 / length(vec3(m[0][0], m[1][0], m[2][0]));
    float world_texel = 2.0 * radius / f.shadow_params.w;
    vec3 po = p + n * (world_texel * 1.5);
    vec4 s = m * vec4(po, 1.0);
    vec3 uvz = vec3(s.xy * 0.5 + 0.5, s.z);
    if (any(lessThan(uvz.xy, vec2(0.0))) || any(greaterThan(uvz.xy, vec2(1.0))) || uvz.z > 1.0) return 1.0;
    vec4 a = f.cascade_atlas[c];
    vec2 uv = uvz.xy * a.zw + a.xy;
    vec2 lo = a.xy + texel * 1.5, hi = a.xy + a.zw - texel * 1.5;
    float sum = 0.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
            sum += texture(sampler2DShadow(g_textures[f.shadow_atlas], g_samplers[SMP_SHADOW]),
                           vec3(clamp(uv + vec2(x, y) * texel, lo, hi), uvz.z));
    return sum / 9.0;
}

float sun_shadow(Frame f, vec3 p, vec3 n, float view_depth) {
    int count = int(f.shadow_params.z);
    for (int c = 0; c < count; ++c) {
        if (view_depth < f.cascade_splits[c]) {
            float s = shadow_cascade(f, p, n, c);
            float fade_start = f.cascade_splits[c] * 0.85;
            if (view_depth > fade_start) {
                float t = (view_depth - fade_start) / (f.cascade_splits[c] - fade_start);
                float next = c + 1 < count ? shadow_cascade(f, p, n, c + 1) : 1.0;
                s = mix(s, next, t);
            }
            return s;
        }
    }
    return 1.0;
}

void main() {
    Frame f = pc.frame.f;
    ivec2 px = ivec2(gl_FragCoord.xy);
    float depth = texelFetch(TEX2D(pc.depth, SMP_NEAREST_CLAMP), px, 0).r;
    vec2 ndc = v_uv * 2.0 - 1.0;
    vec4 hp = f.inv_view_proj * vec4(ndc, max(depth, 1e-7), 1.0);
    vec3 p = hp.xyz / hp.w;
    vec3 V = -normalize(p);
    if (depth <= 0.0) {
        vec3 dir = -V;
        o_color = vec4(textureLod(TEX2D(f.env_background, SMP_LINEAR_REPEAT), equirect_uv(dir), 0.0).rgb * f.exposure.y, 1.0);
        return;
    }
    vec4 g0 = texelFetch(TEX2D(pc.rt0, SMP_NEAREST_CLAMP), px, 0);
    vec4 g1 = texelFetch(TEX2D(pc.rt1, SMP_NEAREST_CLAMP), px, 0);
    vec4 g2 = texelFetch(TEX2D(pc.rt2, SMP_NEAREST_CLAMP), px, 0);
    vec3 albedo = g0.rgb;
    float ao = g0.a;
    vec3 N = oct_decode01(g1.xy);
    float rough = max(g1.z, 0.03);
    float metal = g2.r;
    float a = rough * rough;
    vec3 f0 = mix(vec3(0.04), albedo, metal);
    vec3 diff_col = albedo * (1.0 - metal);
    float ndv = max(dot(N, V), 1e-4);

    // Sun.
    vec3 L = f.sun_dir.xyz;
    float ndl = dot(N, L);
    vec3 color = vec3(0.0);
    if (ndl > 0.0) {
        float view_depth = -(f.view * vec4(p, 1.0)).z;
        float sh = sun_shadow(f, p, N, view_depth);
        vec3 H = normalize(V + L);
        float ndh = max(dot(N, H), 0.0), vdh = max(dot(V, H), 0.0);
        vec3 F = F_Schlick(f0, vdh);
        vec3 spec = D_GGX(ndh, a) * V_SmithGGX(ndv, ndl, a) * F;
        vec3 diff = diff_col / BRDF_PI * (1.0 - F);
        color += (diff + spec) * (f.sun_color.rgb * f.sun_dir.w) * ndl * sh;
    }

    // Image-based lighting (split-sum, multi-scatter compensation).
    vec2 ab = texture(TEX2D(f.brdf_lut, SMP_LINEAR_CLAMP), vec2(ndv, rough)).rg;
    vec3 fss = f0 * ab.x + ab.y;
    float ems = 1.0 - (ab.x + ab.y);
    vec3 favg = f0 + (1.0 - f0) / 21.0;
    vec3 fms = fss * favg / (1.0 - ems * favg);
    vec3 R = reflect(-V, N);
    vec3 prefiltered = textureLod(TEXCUBE(f.env_cube, SMP_LINEAR_CLAMP_MIP), R, rough * float(f.env_mips - 1u)).rgb;
    vec3 irradiance = sh9_eval(f, N);
    vec3 kd = diff_col * (1.0 - fss - fms * ems);
    float spec_occ = clamp(pow(ndv + ao, exp2(-16.0 * a - 1.0)) - 1.0 + ao, 0.0, 1.0);
    color += (prefiltered * fss * spec_occ + (fms * ems + kd) * irradiance / BRDF_PI * ao) * f.exposure.y;
    o_color = vec4(color, 1.0);
}
