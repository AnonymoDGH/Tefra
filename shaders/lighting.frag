#version 460
#extension GL_GOOGLE_include_directive : require
#include "common/gpu_types.glsl"
#include "common/bindless.glsl"
#include "common/brdf.glsl"
#include "common/cube.glsl"
#include "common/atmosphere.glsl"
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

// Exponential height fog, integrated analytically along the view ray (spec 2 §2).
vec3 apply_fog(Frame f, vec3 color, vec3 dir, float dist) {
    float dens = f.fog.x;
    if (dens <= 0.0) return color;
    float b = f.fog.y;
    float h0 = f.camera_pos.y - f.fog.z;
    float k = dir.y * b * dist;
    float shape = abs(k) > 1e-4 ? (1.0 - exp(-k)) / k : 1.0 - 0.5 * k;
    float T = exp(-dens * exp(-b * h0) * dist * shape);
    vec3 ambient = (sh9_eval(f, vec3(0, 1, 0)) + sh9_eval(f, vec3(0, -1, 0))) / (2.0 * BRDF_PI) * f.exposure.y;
    float g = f.fog.w, c = dot(dir, f.sun_dir.xyz);
    float hg = (1.0 - g * g) / (4.0 * BRDF_PI * pow(1.0 + g * g - 2.0 * g * c, 1.5));
    vec3 sun = f.sun_color.rgb * f.sun_dir.w * hg;
    return color * T + (ambient + sun) * (1.0 - T);
}

vec3 sky_color(Frame f, vec3 dir) {
    if (f.atmo.skyview_lut == 0xFFFFFFFFu)
        return textureLod(TEX2D(f.env_background, SMP_LINEAR_REPEAT), equirect_uv(dir), 0.0).rgb * f.exposure.y;
    Atmosphere a = f.atmo;
    vec3 s = a.sun.xyz;
    vec3 L = textureLod(TEX2D(a.skyview_lut, SMP_LINEAR_CLAMP), skyview_uv(dir, s), 0.0).rgb * a.ground.w;
    // Sun disk with limb darkening, attenuated by the atmosphere.
    float cos_d = dot(dir, s), cos_r = cos(a.sun.w);
    if (cos_d > cos_r) {
        float r = a.radii.x + a.radii.w + max(f.camera_pos.y, 0.0);
        vec3 T = atmo_transmittance(a, r, dir.y);
        float x = clamp(acos(min(cos_d, 1.0)) / a.sun.w, 0.0, 1.0);
        float limb = 1.0 - 0.6 * (1.0 - sqrt(1.0 - x * x));
        L += T * a.ground.w / (ATMO_PI * a.sun.w * a.sun.w) * limb;
    }
    return L;
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
        vec3 sky = apply_fog(f, sky_color(f, dir), dir, 1e5);
        o_color = vec4(min(sky * f.exposure.x, vec3(60000.0)), 1.0); // pre-exposed
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
    float translucency = g2.g;
    bool foliage = g1.w > 0.2 && g1.w < 0.5; // class 1/3
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
    if (foliage && translucency > 0.0) {
        // Thin-leaf transmission: light through the needles towards the viewer (strongest against the sun).
        float view_depth = -(f.view * vec4(p, 1.0)).z;
        float sh = sun_shadow(f, p, -N, view_depth);
        float back = max(dot(-N, L), 0.0);
        float forward = pow(max(dot(-V, L), 0.0), 6.0);
        vec3 trans_col = albedo * vec3(1.1, 1.25, 0.6); // light through chlorophyll is yellow-green
        color += trans_col * translucency * (0.35 * back + 1.6 * forward) / BRDF_PI * (f.sun_color.rgb * f.sun_dir.w) * sh * ao;
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
    color = apply_fog(f, color, -V, length(p));
    o_color = vec4(min(color * f.exposure.x, vec3(60000.0)), 1.0); // pre-exposed
}
