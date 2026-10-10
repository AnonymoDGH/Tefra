#version 460
#extension GL_GOOGLE_include_directive : require
#include "common/gpu_types.glsl"
#include "common/bindless.glsl"
#include "common/brdf.glsl"
#include "common/cube.glsl"
#include "common/atmosphere.glsl"
#include "common/fog.glsl"
#include "common/shadow.glsl"
layout(push_constant) uniform Push { FrameBuffer frame; uint depth; uint rt0; uint rt1; uint rt2; uint ao; uint vol; } pc;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

// Linear view distance from a reverse-Z depth value (infinite far plane: depth = near / distance).
float linear_depth(Frame f, float d) { return d > 0.0 ? f.proj[3][2] / (d + f.proj[2][2]) : 1e6; }

// Volumetric fog (half resolution, see volumetric.frag), upsampled with a depth-aware 2x2 filter. Returns rgb
// in-scatter (pre-exposed) and a = transmittance up to min(dist, 150 m).
vec4 volumetric_upsample(Frame f, float depth_c, vec2 uv) {
    const ivec2 ext = ivec2(f.viewport.xy);
    const ivec2 hsz = textureSize(TEX2D(pc.vol, SMP_NEAREST_CLAMP), 0);
    const vec2 fp = uv * vec2(hsz) - 0.5;
    const ivec2 base = ivec2(floor(fp));
    const vec2 w = fract(fp);
    const float lc = linear_depth(f, depth_c);
    vec4 sum = vec4(0.0);
    float wsum = 0.0;
    for (int k = 0; k < 4; ++k) {
        const ivec2 o = ivec2(k & 1, k >> 1);
        const ivec2 q = clamp(base + o, ivec2(0), hsz - 1);
        const float dn = texelFetch(TEX2D(pc.depth, SMP_NEAREST_CLAMP), clamp(q * 2 + 1, ivec2(0), ext - 1), 0).r;
        const float ln = linear_depth(f, dn);
        const float wb = (o.x == 0 ? 1.0 - w.x : w.x) * (o.y == 0 ? 1.0 - w.y : w.y);
        const float wd = exp(-abs(ln - lc) / (0.05 * min(ln, lc) + 0.05));
        const float wt = wb * wd + 1e-4 * wb;
        sum += texelFetch(TEX2D(pc.vol, SMP_LINEAR_CLAMP), q, 0) * wt;
        wsum += wt;
    }
    return sum / wsum;
}

// Fog with the volumetric in-scatter replacing the analytic integral up to 150 m; beyond that the uniform
// in-scatter of the analytic fog covers the remainder. Input color is un-exposed; output is pre-exposed.
vec3 apply_fog_volumetric(Frame f, vec3 color, vec3 dir, float dist, vec4 vol) {
    const float T = fog_transmittance(f, dir, dist);
    const vec3 L = fog_radiance(f, dir) * f.exposure.x;
    return color * f.exposure.x * T + vol.rgb + L * max(vol.a - T, 0.0);
}

// Canopy occlusion (canopy.h). G channel encodes the canopy top height: h = base + g * range (must match C++).
const float kCanopyYBase = -100.0;
const float kCanopyYRange = 800.0;
// Fraction of the ambient light that reaches p (p is camera-relative). Cover above p's height, from the map.
float canopy_visibility(Frame f, vec3 p) {
    if (f.canopy_map == 0xFFFFFFFFu) return 1.0;
    vec3 wp = p + f.camera_pos.xyz;
    vec2 uv = (wp.xz - f.canopy.xy) / f.canopy.z;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) return 1.0;
    // The map fades out over its outer 10 % so the window edge is never visible.
    float fade = (1.0 - smoothstep(0.9, 1.0, max(abs(uv.x - 0.5), abs(uv.y - 0.5)) * 2.0));
    vec2 g = textureLod(TEX2D(f.canopy_map, SMP_LINEAR_CLAMP), uv, 0.0).rg;
    float top = g.g * kCanopyYRange + kCanopyYBase;
    // Crowns cover everything below their top; the top 10 m of a crown thins out to nothing.
    float above = smoothstep(0.0, 10.0, top - wp.y);
    return 1.0 - f.canopy.w * fade * g.r * above;
}

// Screen-space contact shadow: march from the surface towards the sun through the depth buffer. Catches thin
// occluders below the shadow-map texel size (grass, stones, the trunk flare). Reverse-Z infinite projection:
// view distance = proj[3][2] / depth. 1 = lit.
float contact_shadow(Frame f, vec3 p, vec3 n, vec3 L, uint depth_idx, float noise) {
    if ((f.shadow_flags & 2u) == 0u) return 1.0;
    const int steps = 10;
    const float thickness = 0.15; // m: a sample this far behind the visible surface counts as occluded
    const float near_plane = f.proj[3][2];
    const vec2 size = vec2(textureSize(TEX2D(depth_idx, SMP_NEAREST_CLAMP), 0));
    vec3 o = p + n * (0.01 + 0.002 * length(p));
    float occ = 0.0;
    for (int i = 0; i < steps; ++i) {
        float t = (float(i) + noise) / float(steps) * f.shadow_soft.y;
        vec4 c = f.view_proj * vec4(o + L * t, 1.0);
        if (c.w <= 0.0) break;
        vec2 suv = c.xy / c.w * 0.5 + 0.5;
        if (any(lessThan(suv, vec2(0.0))) || any(greaterThan(suv, vec2(1.0)))) break;
        float sd = texelFetch(TEX2D(depth_idx, SMP_NEAREST_CLAMP), ivec2(suv * size), 0).r;
        if (sd <= 0.0) continue; // sky
        float gap = c.w - near_plane / sd; // > 0: the sample is behind the visible surface
        if (gap > 0.01 + 0.002 * c.w && gap < thickness) occ += 1.0;
    }
    return 1.0 - 0.9 * clamp(occ / 3.0, 0.0, 1.0);
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
    const bool use_vol = pc.vol != 0xFFFFFFFFu;
    if (depth <= 0.0) {
        vec3 dir = -V;
        if (use_vol) {
            const vec4 vol = volumetric_upsample(f, depth, v_uv);
            o_color = vec4(min(apply_fog_volumetric(f, sky_color(f, dir), dir, 1e5, vol), vec3(60000.0)), 1.0);
        } else {
            vec3 sky = apply_fog(f, sky_color(f, dir), dir, 1e5);
            o_color = vec4(min(sky * f.exposure.x, vec3(60000.0)), 1.0); // pre-exposed
        }
        return;
    }
    vec4 g0 = texelFetch(TEX2D(pc.rt0, SMP_NEAREST_CLAMP), px, 0);
    vec4 g1 = texelFetch(TEX2D(pc.rt1, SMP_NEAREST_CLAMP), px, 0);
    vec4 g2 = texelFetch(TEX2D(pc.rt2, SMP_NEAREST_CLAMP), px, 0);
    vec3 albedo = g0.rgb;
    float ao = g0.a;
    if (pc.ao != 0xFFFFFFFFu) {
        // Screen-space AO (half resolution, bilinear) multiplies the material AO; it only affects indirect light.
        float ssao = textureLod(TEX2D(pc.ao, SMP_LINEAR_CLAMP), v_uv, 0.0).r;
        ao *= ssao;
    }
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
    float noise = frame_noise(gl_FragCoord.xy, f.exposure.w);
    if ((f.shadow_flags & 4u) != 0u) { // diagnostic: sun visibility for every pixel (not only N.L > 0)
        float vd = -(f.view * vec4(p, 1.0)).z;
        o_color = vec4(vec3(sun_shadow(f, p, N, vd, noise) * contact_shadow(f, p, N, L, pc.depth, noise)), 1.0);
        return;
    }
    if (ndl > 0.0) {
        float view_depth = -(f.view * vec4(p, 1.0)).z;
        float sh = sun_shadow(f, p, N, view_depth, noise) * contact_shadow(f, p, N, L, pc.depth, noise);
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
        float sh = sun_shadow(f, p, -N, view_depth, noise);
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
    float canopy = canopy_visibility(f, p);
    color += (prefiltered * fss * spec_occ + (fms * ems + kd) * irradiance / BRDF_PI * ao) * f.exposure.y * canopy;
    if (use_vol) {
        const vec4 vol = volumetric_upsample(f, depth, v_uv);
        o_color = vec4(min(apply_fog_volumetric(f, color, -V, length(p), vol), vec3(60000.0)), 1.0);
        return;
    }
    color = apply_fog(f, color, -V, length(p));
    o_color = vec4(min(color * f.exposure.x, vec3(60000.0)), 1.0); // pre-exposed
}
