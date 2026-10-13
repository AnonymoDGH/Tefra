#version 460
#extension GL_GOOGLE_include_directive : require
#include "common/gpu_types.glsl"
#include "common/bindless.glsl"
#include "common/brdf.glsl"
#include "common/cube.glsl"
#include "common/atmosphere.glsl"
#include "common/fog.glsl"
#include "common/shadow.glsl"
#include "common/clouds.glsl"
#include "common/sky.glsl"
layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer TileBuffer { uint v[]; };
layout(push_constant) uniform Push {
    FrameBuffer frame; uint depth; uint rt0; uint rt1; uint rt2; uint ao; uint vol; LightBuffer lights; uint light_count; uint clouds; TileBuffer tiles; uint tiles_x; uint history;
} pc;
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

// Caustics on the sea floor: the refracted sun is focused by the curvature of the surface. For a small-slope height
// field h, the intensity at depth d is ~ 1 / (1 + d (1 - 1/n) lap(h)), n = 1.33. A few short wind waves give lap(h).
float c_hash(vec2 p) { vec3 p3 = fract(vec3(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }
float c_noise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(c_hash(i), c_hash(i + vec2(1, 0)), u.x), mix(c_hash(i + vec2(0, 1)), c_hash(i + vec2(1, 1)), u.x), u.y);
}
float caustics(vec2 x, float depth, float t) {
    // Domain warp: real wave fields have no long-range order, so neither may the caustic net.
    x += 1.6 * vec2(c_noise(x / 5.0 + t * 0.03), c_noise(x / 5.0 + 9.7 - t * 0.03)) +
         0.6 * vec2(c_noise(x / 1.7 + 3.3), c_noise(x / 1.7 + 5.1));
    // Nine short waves on golden-angle directions (no visible lattice), wavelengths 0.45..2.4 m.
    float lap = 0.0;
    for (int i = 0; i < 9; ++i) {
        float fi = float(i);
        float ang = fi * 2.3999632 + 0.4;
        vec2 D = vec2(cos(ang), sin(ang));
        float lambda = 2.4 * pow(0.45 / 2.4, fi / 8.0);
        float k = 6.2831853 / lambda, A = 0.006 * lambda;
        lap -= A * k * k * sin(k * dot(D, x) - sqrt(9.81 * k) * t + fi * 1.7);
    }
    return clamp(1.0 / max(1.0 + depth * 0.25 * lap, 0.12), 0.0, 6.0);
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
    if (f.atmo.skyview_lut == 0xFFFFFFFFu) {
        // Image sky (Cubemap / HDRI): the background image, with the cumulus over it.
        vec3 bg = textureLod(TEX2D(f.env_background, SMP_LINEAR_REPEAT), equirect_uv(dir), 0.0).rgb * f.exposure.y;
        if (f.vclouds.x > 0.5) return bg; // volumetric clouds are composited in main()
        vec4 cl = clouds_eval(f, dir, bg);
        return mix(bg, cl.rgb, cl.a);
    }
    Atmosphere a = f.atmo;
    vec3 s = a.sun.xyz;
    vec3 L = textureLod(TEX2D(a.skyview_lut, SMP_LINEAR_CLAMP), skyview_uv(dir, s), 0.0).rgb * a.ground.w;
    float cos_d = dot(dir, s), cos_r = cos(a.sun.w);
    const bool sun_hit = cos_d > cos_r && f.celestial.x > 0.0;
    if (sun_hit || (f.celestial.w > 0.0 && cos_d > 0.0)) {
        float r = a.radii.x + a.radii.w + max(f.camera_pos.y, 0.0);
        vec3 T = atmo_transmittance(a, r, dir.y);
        // Sun disk with limb darkening, attenuated by the atmosphere.
        if (sun_hit) {
            float x = clamp(acos(min(cos_d, 1.0)) / a.sun.w, 0.0, 1.0);
            float limb = 1.0 - 0.6 * (1.0 - sqrt(1.0 - x * x));
            L += T * a.ground.w / (ATMO_PI * a.sun.w * a.sun.w) * limb;
        }
        // Forward-scattered halo around the sun (Atmosphere.Glare).
        if (f.celestial.w > 0.0) {
            float c = max(cos_d, 0.0);
            L += T * a.ground.w * f.celestial.w * (0.01 * pow(c, 6.0) + 0.08 * pow(c, 200.0));
        }
    }
    // Night: stars and the moon (sk_stars and sk_moon return 0 by day).
    L += sk_stars(f, dir) + sk_moon(f, dir);
    if (f.vclouds.x > 0.5) return L; // volumetric clouds are composited in main()
    vec4 cl = clouds_eval(f, dir, L);
    return mix(L, cl.rgb, cl.a);
}

// Screen-space reflections: march the reflected ray through the depth buffer, read the hit from last frame's
// resolved image (reprojected). Returns radiance (not exposed); conf = how much to trust it.
vec3 ssr_trace(Frame f, vec3 p, vec3 N, vec3 V, float rough, float jitter, out float conf) {
    conf = 0.0;
    vec3 R = reflect(-V, N);
    const int kSteps = 20;
    const float kMax = 40.0;
    vec3 start = p + N * 0.03;
    float prev_t = 0.0;
    for (int i = 1; i <= kSteps; ++i) {
        float s = (float(i) - 1.0 + jitter) / float(kSteps);
        float t = kMax * s * s + 0.05; // finer steps near the surface
        vec3 q = start + R * t;
        vec4 cq = f.view_proj * vec4(q, 1.0);
        if (cq.w <= 0.0) return vec3(0.0);
        vec2 uv = cq.xy / cq.w * 0.5 + 0.5;
        if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) return vec3(0.0);
        float sd = textureLod(TEX2D(pc.depth, SMP_NEAREST_CLAMP), uv, 0.0).r;
        float scene_vd = linear_depth(f, sd);
        float ray_vd = cq.w;
        float thick = 0.25 + 0.04 * ray_vd;
        if (ray_vd > scene_vd + 0.02 && ray_vd - scene_vd < thick) {
            // Binary refinement between the last miss and this hit.
            float a = prev_t, b = t;
            for (int k = 0; k < 4; ++k) {
                float m = 0.5 * (a + b);
                vec4 cm = f.view_proj * vec4(start + R * m, 1.0);
                vec2 um = cm.xy / cm.w * 0.5 + 0.5;
                float dm = linear_depth(f, textureLod(TEX2D(pc.depth, SMP_NEAREST_CLAMP), um, 0.0).r);
                if (cm.w > dm + 0.02) b = m; else a = m;
            }
            vec3 hit = start + R * b;
            vec4 pp = f.prev_view_proj * vec4(hit, 1.0);
            vec2 puv = pp.xy / pp.w * 0.5 + 0.5;
            if (any(lessThan(puv, vec2(0.0))) || any(greaterThan(puv, vec2(1.0)))) return vec3(0.0);
            vec2 edge = smoothstep(vec2(0.0), vec2(0.08), puv) * smoothstep(vec2(0.0), vec2(0.08), 1.0 - puv);
            conf = edge.x * edge.y * (1.0 - smoothstep(0.6, 1.0, s)) * (1.0 - smoothstep(0.15, 0.4, rough));
            return textureLod(TEX2D(pc.history, SMP_LINEAR_CLAMP), puv, 0.0).rgb / max(f.exposure.x, 1e-8);
        }
        prev_t = t;
    }
    return vec3(0.0);
}

// Glow of the nearest local lights in the haze (analytic single scattering along the view ray: the integral of
// 1 / distance^2 has a closed form), for when the raymarched fog is off. tmax: ray length (m).
vec3 light_halos(Frame f, vec3 dir, float tmax) {
    float sigma = max(f.fog.x, 0.0) * 1.5 + f.weather.z * 0.004; // rain makes the air glow too
    if (sigma <= 0.0 || pc.light_count == 0u) return vec3(0.0);
    vec3 sum = vec3(0.0);
    uint n = min(pc.light_count, 16u);
    for (uint i = 0u; i < n; ++i) {
        LightGPU l = pc.lights.l[i];
        vec3 c = l.pos_range.xyz;
        float s0 = dot(c, dir);
        float h2 = max(dot(c, c) - s0 * s0, 0.0) + 0.04;
        float r = l.pos_range.w;
        if (h2 > r * r) continue;
        float h = sqrt(h2);
        float integral = (atan((tmax - s0) / h) - atan(-s0 / h)) / h;
        float fade = 1.0 - smoothstep(0.4 * r * r, r * r, h2);
        sum += l.color.rgb * integral * fade;
    }
    return sum * sigma * (1.0 / (4.0 * BRDF_PI)) * 2.5;
}

// Sky with the volumetric clouds over it (reduced resolution, bilinear: clouds are soft).
vec3 sky_with_clouds(Frame f, vec3 dir) {
    vec3 sky = sky_color(f, dir);
    if (pc.clouds == 0xFFFFFFFFu) return sky;
    vec4 c = textureLod(TEX2D(pc.clouds, SMP_LINEAR_CLAMP), v_uv, 0.0);
    return sky * c.a + c.rgb;
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
            o_color = vec4(min(apply_fog_volumetric(f, sky_with_clouds(f, dir), dir, 1e5, vol), vec3(60000.0)), 1.0);
        } else {
            vec3 sky = apply_fog(f, sky_with_clouds(f, dir), dir, 1e5) + light_halos(f, dir, 400.0);
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
    // Under the sea (ocean.h): sun and sky reach the floor through the water column, focused into caustics. The
    // floor has no specular of its own there (the water surface reflects instead).
    vec3 sun_tint = vec3(1.0), amb_tint = vec3(1.0);
    float spec_keep = 1.0;
    if (f.ocean.y > 0.5) {
        vec3 wp = p + f.camera_pos.xyz;
        float uw = f.ocean.x - wp.y;
        if (uw > 0.0) {
            vec3 sig = f.water_absorption.xyz + f.water_scattering.xyz;
            float c = caustics(wp.xz + L.xz / max(L.y, 0.2) * uw, uw, f.camera_pos.w);
            float amount = smoothstep(0.03, 0.5, uw) * exp(-uw * 0.2);
            sun_tint = exp(-sig * uw / max(L.y, 0.15)) * mix(1.0, c, amount);
            amb_tint = exp(-sig * uw * 1.3);
            spec_keep = 1.0 - smoothstep(0.0, 0.08, uw);
        }
    }
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
        vec3 spec = D_GGX(ndh, a) * V_SmithGGX(ndv, ndl, a) * F * spec_keep;
        vec3 diff = diff_col / BRDF_PI * (1.0 - F);
        color += (diff + spec) * (f.sun_color.rgb * f.sun_dir.w) * ndl * sh * sun_tint;
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

    // Local lights (PointLight / SpotLight): GGX + Lambert, windowed inverse-square falloff (Karis 2013).
    // Tiled: only the lights of this pixel's 16x16 tile (light_cull.comp).
    uint tile_base = (uint(gl_FragCoord.y) / 16u * pc.tiles_x + uint(gl_FragCoord.x) / 16u) * 65u;
    uint tile_n = pc.light_count > 0u ? pc.tiles.v[tile_base] : 0u;
    for (uint ti = 0u; ti < tile_n; ++ti) {
        LightGPU lt = pc.lights.l[pc.tiles.v[tile_base + 1u + ti]];
        vec3 dl = lt.pos_range.xyz - p;
        float d2 = dot(dl, dl);
        float range = lt.pos_range.w;
        if (d2 >= range * range) continue;
        vec3 Ll = dl * inversesqrt(max(d2, 1e-8));
        float nl = dot(N, Ll);
        if (nl <= 0.0) continue;
        float win = clamp(1.0 - (d2 * d2) / (range * range * range * range), 0.0, 1.0);
        float atten = win * win / max(d2, 0.04);
        if (lt.spot.w >= -1.0) atten *= smoothstep(lt.spot.w, max(lt.extra.x, lt.spot.w + 1e-3), dot(-Ll, lt.spot.xyz));
        vec3 Hl = normalize(V + Ll);
        vec3 Fl = F_Schlick(f0, max(dot(V, Hl), 0.0));
        vec3 specl = D_GGX(max(dot(N, Hl), 0.0), a) * V_SmithGGX(ndv, nl, a) * Fl;
        color += (diff_col / BRDF_PI * (1.0 - Fl) + specl) * lt.color.rgb * (atten * nl);
    }
    // Emission: display-referred (glows the same at any exposure; 1 = a few times a sunlit white wall, so it blooms).
    color += albedo * g2.b * (1.6 / f.exposure.x);
    // Flat bounce light for interiors (Lighting.IndoorAmbient): diffuse only, occluded by AO.
    color += diff_col * f.ambient.xyz / BRDF_PI * ao;

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
    vec3 env_spec = prefiltered * f.exposure.y * canopy * amb_tint;
    if (pc.history != 0xFFFFFFFFu && rough < 0.4) {
        float conf;
        vec3 refl = ssr_trace(f, p, N, V, rough, noise, conf);
        env_spec = mix(env_spec, refl, conf);
    }
    color += env_spec * fss * spec_occ * spec_keep + (fms * ems + kd) * irradiance / BRDF_PI * ao * f.exposure.y *
             canopy * amb_tint;
    if (use_vol) {
        const vec4 vol = volumetric_upsample(f, depth, v_uv);
        o_color = vec4(min(apply_fog_volumetric(f, color, -V, length(p), vol), vec3(60000.0)), 1.0);
        return;
    }
    color = apply_fog(f, color, -V, length(p)) + light_halos(f, -V, length(p));
    o_color = vec4(min(color * f.exposure.x, vec3(60000.0)), 1.0); // pre-exposed
}
