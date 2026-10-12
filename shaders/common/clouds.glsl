#ifndef TEFRA_CLOUDS_GLSL
#define TEFRA_CLOUDS_GLSL
// Fair-weather cumulus as a lit 2D layer (requires gpu_types.glsl, brdf.glsl for sh9_eval). Shared by the sky in the
// lighting pass and the ocean reflections. f.clouds: x coverage (0 = off), y base altitude (m), z feature size (m),
// w drift (m, wind * time); cloud_wind xy direction, cloud_look rgb tint and w optical depth scale. Un-exposed.

// Sin-free hash (Hoskins): sin() of large arguments loses precision on Intel and draws blocky patterns.
float cl_hash(vec2 p) { vec3 p3 = fract(vec3(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }
float cl_noise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(cl_hash(i), cl_hash(i + vec2(1, 0)), u.x), mix(cl_hash(i + vec2(0, 1)), cl_hash(i + vec2(1, 1)), u.x),
               u.y);
}
float cl_fbm(vec2 p, int oct) {
    float s = 0.0, a = 0.5;
    const mat2 r = mat2(0.8, 0.6, -0.6, 0.8);
    for (int i = 0; i < oct; ++i) {
        s += a * cl_noise(p);
        p = r * p * 2.03 + 7.1;
        a *= 0.5;
    }
    return s;
}
// Cloud density (0..1) at layer position q (m).
float cl_density(Frame f, vec2 q, int oct) {
    vec2 p = q / f.clouds.z;
    vec2 w = vec2(cl_fbm(p * 0.5 + 3.0, 3), cl_fbm(p * 0.5 - 5.0, 3)) - 0.5; // billow warp
    // Rounded heaps: a large shape with billowy (abs) detail eating its edges.
    float shape = cl_fbm(p * 0.8 + w * 1.4, 3);
    float billow = 1.0 - abs(cl_fbm(p * 3.1 + w, oct - 2) * 2.0 - 1.0);
    float d = shape + (billow - 0.6) * 0.22;
    // Large-scale variation so the sky has clear gaps and denser fields.
    d += (cl_noise(q / (f.clouds.z * 6.0) + 11.0) - 0.5) * 0.35;
    float thr = 1.0 - f.clouds.x;
    return clamp((d - thr) / 0.12, 0.0, 1.0);
}

// Clouds seen along dir from the camera: rgb = radiance, a = opacity. sky = sky radiance behind (for the haze).
vec4 clouds_eval(Frame f, vec3 dir, vec3 sky) {
    if (f.clouds.x <= 0.0 || dir.y <= 0.01) return vec4(0.0);
    float t = (f.clouds.y - f.camera_pos.y) / dir.y;
    vec2 q = f.camera_pos.xz + dir.xz * t + f.cloud_wind.xy * f.clouds.w;
    int oct = t < 20000.0 ? 6 : 4;
    float c = cl_density(f, q, oct);
    if (c <= 0.0) return vec4(0.0);
    float tau = c * 9.0 * f.cloud_look.w;
    // Self-shadowing towards the sun (two taps through the layer), powder darkening of the thin edges.
    vec3 L = f.sun_dir.xyz;
    vec2 sq = q + L.xz / max(L.y, 0.15) * 250.0;
    float cs = 0.5 * cl_density(f, sq, 4) + 0.5 * cl_density(f, sq + L.xz / max(L.y, 0.15) * 400.0, 3);
    float Ts = exp(-cs * 5.0 - c * 1.2);
    float mu = dot(dir, L);
    float g1 = 0.65, g2 = -0.25;
    float hg1 = (1.0 - g1 * g1) / (4.0 * 3.14159265 * pow(1.0 + g1 * g1 - 2.0 * g1 * mu, 1.5));
    float hg2 = (1.0 - g2 * g2) / (4.0 * 3.14159265 * pow(1.0 + g2 * g2 - 2.0 * g2 * mu, 1.5));
    float phase = mix(hg2, hg1, 0.6);
    float powder = 1.0 - exp(-tau * 2.0);
    vec3 sunE = f.sun_color.rgb * f.sun_dir.w;
    vec3 sky_amb = sh9_eval(f, vec3(0, 1, 0)) / 3.14159265 * f.exposure.y;
    vec3 rad = (sunE * Ts * phase * powder * 3.0 + sky_amb * (0.6 + 0.4 * (1.0 - c))) * f.cloud_look.rgb;
    float alpha = 1.0 - exp(-tau);
    // Aerial perspective: distant clouds fade into the horizon haze.
    float haze = 1.0 - exp(-t / 28000.0);
    alpha *= smoothstep(0.01, 0.06, dir.y);
    return vec4(mix(rad, sky, haze), alpha);
}
#endif
