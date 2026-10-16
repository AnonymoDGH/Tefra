#ifndef TEFRA_SHADOW_GLSL
#define TEFRA_SHADOW_GLSL
// Sun shadow cascades in the shadow atlas. Requires gpu_types.glsl and bindless.glsl.
// shadow_params: x,y = atlas texel size (uv), z = cascade count, w = cascade resolution (texels).

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
    float max_pen_m = tan_sun * uvz.z * zrange * f.shadow_soft.x;
    float search = clamp(max_pen_m / world_texel, 1.5, 32.0); // texels
    float blk = 0.0, nb = 0.0;
    for (int i = 0; i < 16; ++i) {
        vec2 o = vogel_disk(i, 16, rot) * search;
        float d = textureLod(TEX2D(f.shadow_atlas, SMP_NEAREST_CLAMP), clamp(uv + o * texel, lo, hi), 0.0).r;
        if (d < rz + dot(grad, o * world_texel)) { blk += d; nb += 1.0; }
    }
    if (nb == 0.0) return 1.0; // no occluder closer to the sun: fully lit
    // Penumbra half-width at the receiver (m) = distance to the blocker * tan(angular radius).
    float pen_m = (uvz.z - blk / nb) * zrange * tan_sun * f.shadow_soft.x;
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

// Single hardware-compare tap (volumetric fog: many samples per pixel, TAA does the filtering).
float shadow_cascade_tap(Frame f, vec3 p, vec3 n, int c) {
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
    return texture(sampler2DShadow(g_textures[f.shadow_atlas], g_samplers[SMP_SHADOW]), vec3(clamp(uv, lo, hi), uvz.z));
}
float sun_shadow_tap(Frame f, vec3 p, float view_depth) {
    int count = int(f.shadow_params.z);
    for (int c = 0; c < count; ++c)
        if (view_depth < f.cascade_splits[c]) return shadow_cascade_tap(f, p, vec3(0.0), c);
    return 1.0;
}

// Shadow of a local light (perspective map in the local shadow atlas). p, n camera-relative; dist = distance to the
// light (normal offset grows with it, the texels do too). taps: 4 (surfaces) or 1 (volumetric fog).
float local_shadow(Frame f, int k, vec3 p, vec3 n, float dist, bool soft) {
    if (f.local_shadow_tex == 0xFFFFFFFFu) return 1.0;
    vec4 s = f.local_shadow_vp[k] * vec4(p + n * (0.015 + dist * 0.012), 1.0);
    if (s.w <= 1e-4) return 1.0;
    vec3 c = s.xyz / s.w;
    vec2 uv = c.xy * 0.5 + 0.5;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))) || c.z > 1.0) return 1.0;
    vec4 a = f.local_shadow_atlas[k];
    vec2 texel = vec2(f.local_shadow_texel, f.local_shadow_texel * 2.0);
    vec2 lo = a.xy + texel * 1.5, hi = a.xy + a.zw - texel * 1.5;
    vec2 at = uv * a.zw + a.xy;
    float ref = c.z - 2e-5;
    if (!soft)
        return texture(sampler2DShadow(g_textures[f.local_shadow_tex], g_samplers[SMP_SHADOW]), vec3(clamp(at, lo, hi), ref));
    float sum = 0.0;
    for (int i = 0; i < 4; ++i) {
        vec2 o = vec2((i & 1) == 0 ? -0.75 : 0.75, (i & 2) == 0 ? -0.75 : 0.75) * texel;
        sum += texture(sampler2DShadow(g_textures[f.local_shadow_tex], g_samplers[SMP_SHADOW]), vec3(clamp(at + o, lo, hi), ref));
    }
    return sum * 0.25;
}

#endif
