#version 460
#extension GL_GOOGLE_include_directive : require
#include "common/ocean.glsl"
#include "common/brdf.glsl"
#include "common/fog.glsl"
#include "common/shadow.glsl"
#include "common/clouds.glsl"
layout(location = 0) in vec3 v_pos;
layout(location = 1) in vec2 v_xz;
layout(location = 2) in vec4 v_shore;
layout(location = 0) out vec4 o_color;

// Foam lace: thresholded two-scale noise advected towards the shore. coverage 0..1 -> fraction of white.
float foam_lace(vec2 p, vec2 flow, float t, float coverage) {
    vec2 q = p + flow * t * 0.6;
    float n = 0.55 * ocean_vnoise(q * 1.3) + 0.3 * ocean_vnoise(q * 3.7 + 11.0 - flow * t * 0.4) +
              0.15 * ocean_vnoise(q * 9.1 + 5.0);
    float thr = 1.0 - coverage;
    return smoothstep(thr - 0.08, thr + 0.12, n + coverage * 0.25) * smoothstep(0.0, 0.15, coverage);
}

void main() {
    Frame f = pc.frame.f;
    const float t = pc.time;
    const float dist = length(v_pos);
    const vec3 V = -v_pos / dist;
    Shore sh;
    sh.depth = v_shore.x;
    sh.dist = v_shore.y;
    sh.to_shore = normalize(v_shore.zw + vec2(1e-6));

    // Pixel footprint on the water (m): filters waves that would alias into sparkle noise.
    const float footprint = max(length(dFdx(v_xz)), length(dFdy(v_xz)));
    OceanSample o = ocean_eval(v_xz, t, sh, true, footprint);
    vec2 slope = o.slope;
    float lost = o.lost_var;
    for (uint i = 0u; i < OCEAN_DETAIL_WAVES; ++i) {
        OceanWave w = pc.waves.w[OCEAN_GEOMETRY_WAVES + i];
        float k = w.dir_k.z, A = w.amp.x;
        float th = k * dot(w.dir_k.xy, v_xz) - w.dir_k.w * t + w.amp.z;
        float keep = ocean_keep(k, footprint);
        slope += k * A * cos(th) * w.dir_k.xy * keep;
        lost += 0.5 * (k * A) * (k * A) * (1.0 - keep);
    }
    // Capillary micro detail: three octaves of drifting noise slopes (no periodic interference pattern).
    {
        const vec3 oct[5] = vec3[](vec3(7.0, 0.035, 0.35), vec3(2.6, 0.035, 0.55), vec3(0.55, 0.035, 0.9),
                                   vec3(0.22, 0.03, 1.4), vec3(0.09, 0.025, 2.1));
        for (int i = 0; i < 5; ++i) {
            float scale = oct[i].x; // m per noise cell
            float keep = 1.0 - smoothstep(0.3, 0.8, footprint / scale);
            vec2 drift = vec2(0.31, 0.95) * t * oct[i].z * 0.12;
            vec3 n1 = ocean_noised(v_xz / scale + drift);
            vec3 n2 = ocean_noised(v_xz / scale * 1.37 - drift.yx + 17.0);
            vec2 g = (n1.yz + n2.yz * 1.37) * oct[i].y; // slope (noise cell -> metres cancels with the 1/scale)
            slope += g * keep;
            lost += dot(oct[i].y, oct[i].y) * 2.0 * (1.0 - keep);
        }
    }
    // Far away every wave is filtered: the slope variance tends to the Cox-Munk total for a ~6 m/s wind.
    lost = max(lost, 0.034 * smoothstep(0.02, 1.5, footprint));
    vec3 N = normalize(vec3(-slope.x, 1.0, -slope.y));
    if (dot(N, V) < 0.02) N = normalize(N + V * (0.02 - dot(N, V))); // no back-facing normals at grazing angles
    // GGX roughness from the filtered slope variance (sigma^2 ~ alpha^2 / 2).
    const float a = clamp(sqrt(0.012 * 0.012 + 2.0 * lost), 0.012, 0.6);
    const float ndv = clamp(dot(N, V), 1e-3, 1.0);
    const vec3 L = f.sun_dir.xyz;

    // Local water depth under the moving surface: thin water near the swash line fades out and foams.
    const float local_depth = sh.depth + o.disp.y;
    const float wet_edge = smoothstep(0.0, 0.06, local_depth);

    // Rough-surface Fresnel (masking keeps grazing rough water from turning into a mirror).
    float F = (0.02 + (max(1.0 - a * 1.6, 0.02) - 0.02) * pow(1.0 - ndv, 5.0)) * wet_edge;
    vec3 R = reflect(-V, N);
    // Facets tilted towards the viewer dominate rough water: the mean reflected ray climbs into bluer sky.
    R.y = abs(R.y) + a * 0.35;
    R = normalize(R);
    vec3 refl = textureLod(TEXCUBE(f.env_cube, SMP_LINEAR_CLAMP_MIP), R, sqrt(a) * float(f.env_mips - 1u)).rgb *
                f.exposure.y;
    // Clouds in the reflection (the IBL cube is cloudless); rough water blurs them away.
    {
        vec4 cl = clouds_eval(f, R, refl / max(f.exposure.y, 1e-4));
        refl = mix(refl, cl.rgb * f.exposure.y, cl.a * (1.0 - smoothstep(0.08, 0.35, a)));
    }
    // Sun: GGX glint, shadowed by the cascades (rocks, palms).
    const float view_depth = -(f.view * vec4(v_pos, 1.0)).z;
    const float vis = sun_shadow_tap(f, v_pos, view_depth);
    const vec3 sunE = f.sun_color.rgb * f.sun_dir.w * vis;
    vec3 H = normalize(V + L);
    float ndl = max(dot(N, L), 0.0);
    float Fh = 0.02 + 0.98 * pow(1.0 - max(dot(V, H), 0.0), 5.0);
    vec3 spec = sunE * D_GGX(max(dot(N, H), 0.0), a) * V_SmithGGX(ndv, ndl, a) * ndl * Fh;

    // Water body: per-channel extinction along the refracted path, single scattering of sun and sky.
    const vec3 sig_a = f.water_absorption.xyz, sig_s = f.water_scattering.xyz, sig_t = sig_a + sig_s;
    vec2 uv = gl_FragCoord.xy * f.viewport.zw;
    float d0 = texelFetch(TEX2D(pc.depth, SMP_NEAREST_CLAMP), ivec2(gl_FragCoord.xy), 0).r;
    vec4 sp = f.inv_view_proj * vec4(uv * 2.0 - 1.0, max(d0, 1e-7), 1.0);
    float behind = d0 <= 0.0 ? 1e4 : length(sp.xyz / sp.w);
    float thickness = max(behind - dist, 0.0);
    // Refraction: shift the lookup along the surface slope, more for thicker water; keep it behind the surface.
    vec2 ruv = uv + N.xz * 0.045 * clamp(thickness, 0.0, 3.0) / (1.0 + dist * 0.06);
    ivec2 rpx = clamp(ivec2(ruv * f.viewport.xy), ivec2(0), ivec2(f.viewport.xy) - 1);
    float d1 = texelFetch(TEX2D(pc.depth, SMP_NEAREST_CLAMP), rpx, 0).r;
    vec4 sp1 = f.inv_view_proj * vec4(ruv * 2.0 - 1.0, max(d1, 1e-7), 1.0);
    float behind1 = d1 <= 0.0 ? 1e4 : length(sp1.xyz / sp1.w);
    if (behind1 < dist) {
        rpx = ivec2(gl_FragCoord.xy);
    } else {
        thickness = max(behind1 - dist, 0.0);
    }
    vec3 bg = texelFetch(TEX2D(pc.hdr, SMP_NEAREST_CLAMP), rpx, 0).rgb; // pre-exposed, sea floor already lit
    vec3 T = exp(-sig_t * thickness);
    vec3 skyE = sh9_eval(f, vec3(0, 1, 0)) * f.exposure.y;
    // Light reaching the scatterers: sun refracted down (and attenuated over ~1/3 of the column) plus sky.
    float col = min(max(sh.depth, 0.0), 30.0);
    vec3 E_in = sunE * max(L.y, 0.0) * exp(-sig_t * col * 0.33) * (1.0 - F) + skyE * exp(-sig_t * col * 0.2);
    // Diffuse reflectance of a deep water body ~ 0.33 b_b / a, with b_b ~ 0.1 b (water scatters forward).
    vec3 body = 0.33 * 0.1 * sig_s / sig_t * (1.0 - T) * E_in / BRDF_PI * 4.0;
    // Light through thin wave crests towards a viewer facing the sun.
    float through = pow(max(dot(-V, L), 0.0), 4.0) * smoothstep(0.0, 0.6, o.disp.y) * (1.0 - F);
    body += sunE * vec3(0.10, 0.55, 0.50) * through * 0.05;

    // Foam: breaking crests, the swash line, wind whitecaps. Lit as a rough white diffuser.
    // Breaking foam is never a solid band: patches of it drift with the crest.
    float patches = ocean_vnoise(v_xz / 6.0 + vec2(t * 0.05, 0.0)) * 0.6 + ocean_vnoise(v_xz / 2.2 - t * 0.08) * 0.4;
    float cov = o.break_foam * mix(0.15, 0.8, patches * patches);
    // Swash: a thin lace line at the leading edge of the water sheet, broken up along the shore.
    cov = max(cov, (1.0 - smoothstep(0.015, 0.09, local_depth)) * o.surf * mix(0.25, 0.85, patches));
    cov = max(cov, smoothstep(0.22, 0.02, o.jacobian) * 0.6 * smoothstep(3.0, 10.0, sh.depth));
    // Near: the lace pattern. Far (sub-pixel lace): its average coverage, so foam does not alias into a white sheet.
    float far = smoothstep(40.0, 250.0, dist);
    float foam = mix(foam_lace(v_xz, sh.to_shore, t, cov), cov * cov * 0.6, far) * wet_edge;
    vec3 foamE = sunE * max(dot(N, L) * 0.7 + 0.3, 0.0) + skyE;
    vec3 foam_col = vec3(0.82, 0.86, 0.86) * foamE / BRDF_PI;

    vec3 surface = refl * F + spec * wet_edge + body;
    surface = mix(surface, foam_col, foam);
    float Tf;
    vec3 ins;
    fog_terms(f, -V, dist, Tf, ins);
    vec3 c = (surface * Tf + ins) * f.exposure.x;
    c += bg * T * (1.0 - F) * (1.0 - foam);
    // Drivers differ on NaN/Inf (legacy AMD propagates them where Intel clamps): never let one reach TAA, which would
    // smear it into a black sea.
    if (any(isnan(c)) || any(isinf(c))) c = (refl * F + body) * f.exposure.x;
    if (any(isnan(c)) || any(isinf(c))) c = vec3(0.02, 0.05, 0.07) * skyE * f.exposure.x;
    o_color = vec4(clamp(c, vec3(0.0), vec3(60000.0)), 1.0);
}
