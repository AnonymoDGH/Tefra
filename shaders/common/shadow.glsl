#ifndef TEFRA_SHADOW_GLSL
#define TEFRA_SHADOW_GLSL
// Sun shadow cascades in the shadow atlas. Requires gpu_types.glsl and bindless.glsl.
// shadow_params: x,y = atlas texel size (uv), z = cascade count, w = cascade resolution (texels).

// One cascade: pcf = true gives a 3x3 hardware-compare average, false a single compare tap.
float shadow_cascade(Frame f, vec3 p, vec3 n, int c, bool pcf) {
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
    if (!pcf)
        return texture(sampler2DShadow(g_textures[f.shadow_atlas], g_samplers[SMP_SHADOW]),
                       vec3(clamp(uv, lo, hi), uvz.z));
    float sum = 0.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
            sum += texture(sampler2DShadow(g_textures[f.shadow_atlas], g_samplers[SMP_SHADOW]),
                           vec3(clamp(uv + vec2(x, y) * texel, lo, hi), uvz.z));
    return sum / 9.0;
}

// Visibility of the sun at camera-relative position p (view_depth = view-space distance along -Z).
float sun_shadow(Frame f, vec3 p, vec3 n, float view_depth, bool pcf) {
    int count = int(f.shadow_params.z);
    for (int c = 0; c < count; ++c) {
        if (view_depth < f.cascade_splits[c]) {
            float s = shadow_cascade(f, p, n, c, pcf);
            float fade_start = f.cascade_splits[c] * 0.85;
            if (view_depth > fade_start) {
                float t = (view_depth - fade_start) / (f.cascade_splits[c] - fade_start);
                float next = c + 1 < count ? shadow_cascade(f, p, n, c + 1, pcf) : 1.0;
                s = mix(s, next, t);
            }
            return s;
        }
    }
    return 1.0;
}
#endif
