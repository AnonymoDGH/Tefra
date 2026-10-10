#version 460
#extension GL_GOOGLE_include_directive : require
#include "common/gpu_types.glsl"
#include "common/bindless.glsl"
#include "common/brdf.glsl"
#include "common/cube.glsl"
#include "common/atmosphere.glsl"
#include "common/fog.glsl"
layout(push_constant) uniform Push { FrameBuffer frame; uint depth; uint rt0; uint rt1; uint rt2; uint ao; } pc;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

// Interleaved gradient noise, animated per frame: decorrelates the per-pixel sample patterns so TAA averages them.
float frame_noise(vec2 px, float frame) {
    px += frame * vec2(5.588238, 5.588238);
    return fract(52.9829189 * fract(dot(px, vec2(0.06711056, 0.00583715))));
}

// Vogel (golden-angle) disk sample i of n, rotated by rot. Uniform density, cheap, and rotation-friendly.
vec2 vogel_disk(int i, int n, float rot) {
    float r = sqrt((float(i) + 0.5) / float(n));
    float th = float(i) * 2.399963 + rot;
    return r * vec2(cos(th), sin(th));
}

// Cascade shadow with PCSS (Fernando 2005). shadow_params: x,y = atlas texel size (uv), z = cascade count,
// w = cascade resolution (texels). Atlas depth is linear along the sun: meters = depth * zrange. 1 = lit.
float shadow_cascade(Frame f, vec3 p, vec3 n, int c, float noise) {
    vec2 texel = f.shadow_params.xy;
    mat4 m = f.cascade_view_proj[c];
    float radius = 1.0 / length(vec3(m[0][0], m[1][0], m[2][0]));
    float zrange = 1.0 / abs(m[2][2]); // m of depth per unit of depth (cascade near..far along the sun)
    float world_texel = 2.0 * radius / f.shadow_params.w;
    vec3 po = p + n * (world_texel * 1.5);
    vec4 s = m * vec4(po, 1.0);
    vec3 uvz = vec3(s.xy * 0.5 + 0.5, s.z);
    if (any(lessThan(uvz.xy, vec2(0.0))) || any(greaterThan(uvz.xy, vec2(1.0))) || uvz.z > 1.0) return 1.0;
    vec4 a = f.cascade_atlas[c];
    vec2 uv = uvz.xy * a.zw + a.xy;
    vec2 lo = a.xy + texel * 1.5, hi = a.xy + a.zw - texel * 1.5;
    float rz = uvz.z - world_texel / zrange; // depth bias: one texel, along the sun
    if ((f.shadow_flags & 1u) == 0u) {
        // Fixed 3x3 PCF (the pre-PCSS look; cheaper, used when pcss_shadows is off).
        float sum = 0.0;
        for (int y = -1; y <= 1; ++y)
            for (int x = -1; x <= 1; ++x)
                sum += texture(sampler2DShadow(g_textures[f.shadow_atlas], g_samplers[SMP_SHADOW]),
                               vec3(clamp(uv + vec2(x, y) * texel, lo, hi), rz));
        return sum / 9.0;
    }

    // Receiver-plane depth: a lateral offset (a, b) in the light plane (R, U in world) lands on the receiver's
    // surface at depth rz + dot(grad, (a, b)), grad = (n.R, n.U) / (n.L * zrange). Without this, wide kernels
    // compare flat ground against its own neighbours at grazing sun and self-shadow (acne).
    vec3 R = radius * vec3(m[0][0], m[1][0], m[2][0]);
    vec3 U = radius * vec3(m[0][1], m[1][1], m[2][1]);
    float nl = dot(n, f.sun_dir.xyz);
    if (abs(nl) < 0.15) nl = nl < 0.0 ? -0.15 : 0.15;
    vec2 grad = vec2(dot(n, R), dot(n, U)) / (nl * zrange);
    float rot = noise * 6.2831853;

    // Blocker search over the penumbra range: stored depth closer to the sun than the receiver plane there.
    float tan_sun = tan(f.sun_color.w);
    float max_pen_m = tan_sun * uvz.z * zrange * f.penumbra_scale;
    float search = clamp(max_pen_m / world_texel, 1.5, 32.0); // texels
    float blk = 0.0, nb = 0.0;
    for (int i = 0; i < 16; ++i) {
        vec2 o = vogel_disk(i, 16, rot) * search;
        float d = textureLod(TEX2D(f.shadow_atlas, SMP_NEAREST_CLAMP), clamp(uv + o * texel, lo, hi), 0.0).r;
        if (d < rz + dot(grad, o * world_texel)) { blk += d; nb += 1.0; }
    }
    if (nb == 0.0) return 1.0; // no occluder closer to the sun: fully lit
    // Penumbra half-width at the receiver (m) = distance to the blocker * tan(angular radius).
    float pen_m = (uvz.z - blk / nb) * zrange * tan_sun * f.penumbra_scale;
    float filter_texels = clamp(pen_m / world_texel, 0.75, 24.0);

    float sum = 0.0;
    for (int i = 0; i < 16; ++i) {
        vec2 o = vogel_disk(i, 16, rot + 1.7) * filter_texels;
        sum += textureLod(sampler2DShadow(g_textures[f.shadow_atlas], g_samplers[SMP_SHADOW]),
                          vec3(clamp(uv + o * texel, lo, hi), rz + dot(grad, o * world_texel)), 0.0);
    }
    return sum / 16.0;
}

float sun_shadow(Frame f, vec3 p, vec3 n, float view_depth, float noise) {
    int count = int(f.shadow_params.z);
    for (int c = 0; c < count; ++c) {
        if (view_depth < f.cascade_splits[c]) {
            float s = shadow_cascade(f, p, n, c, noise);
            float fade_start = f.cascade_splits[c] * 0.85;
            if (view_depth > fade_start) {
                float t = (view_depth - fade_start) / (f.cascade_splits[c] - fade_start);
                float next = c + 1 < count ? shadow_cascade(f, p, n, c + 1, noise) : 1.0;
                s = mix(s, next, t);
            }
            return s;
        }
    }
    return 1.0;
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
        float t = (float(i) + noise) / float(steps) * f.contact_length;
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
    color += (prefiltered * fss * spec_occ + (fms * ems + kd) * irradiance / BRDF_PI * ao) * f.exposure.y;
    color = apply_fog(f, color, -V, length(p));
    o_color = vec4(min(color * f.exposure.x, vec3(60000.0)), 1.0); // pre-exposed
}
