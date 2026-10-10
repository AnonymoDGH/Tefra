#ifndef TEFRA_ATMOSPHERE_GLSL
#define TEFRA_ATMOSPHERE_GLSL
// GPU side of engine/renderer/src/atmosphere.cpp (Hillaire 2020). Radiance per unit top-of-atmosphere
// sun illuminance unless stated otherwise. Requires bindless.glsl and gpu_types.glsl (Atmosphere struct).
const float ATMO_PI = 3.14159265358979;

float atmo_dist_top(float r, float mu, float R) {
    float disc = r * r * (mu * mu - 1.0) + R * R;
    return max(0.0, -r * mu + sqrt(max(disc, 0.0)));
}
float atmo_dist_ground(float r, float mu, float Rb) {
    float disc = r * r * (mu * mu - 1.0) + Rb * Rb;
    return (mu < 0.0 && disc >= 0.0) ? -r * mu - sqrt(disc) : -1.0;
}
vec3 atmo_transmittance(Atmosphere a, float r, float mu) {
    float Rb = a.radii.x, Rt = a.radii.y;
    if (atmo_dist_ground(r, mu, Rb) >= 0.0) return vec3(0.0);
    float H = sqrt(Rt * Rt - Rb * Rb);
    float rho = sqrt(max(r * r - Rb * Rb, 0.0));
    float d = atmo_dist_top(r, mu, Rt);
    float dmin = Rt - r, dmax = rho + H;
    vec2 uv = vec2((d - dmin) / (dmax - dmin), rho / H);
    return textureLod(TEX2D(a.transmittance_lut, SMP_LINEAR_CLAMP), uv, 0.0).rgb;
}
vec3 atmo_multiscatter(Atmosphere a, float r, float mu_sun) {
    vec2 uv = vec2(mu_sun * 0.5 + 0.5, (r - a.radii.x) / (a.radii.y - a.radii.x));
    return textureLod(TEX2D(a.multiscatter_lut, SMP_LINEAR_CLAMP), uv, 0.0).rgb;
}
float atmo_phase_rayleigh(float c) { return 3.0 / (16.0 * ATMO_PI) * (1.0 + c * c); }
float atmo_phase_mie(float c, float g) {
    float g2 = g * g;
    float k = 3.0 / (8.0 * ATMO_PI) * (1.0 - g2) / (2.0 + g2);
    return k * (1.0 + c * c) / pow(max(1.0 + g2 - 2.0 * g * c, 1e-6), 1.5);
}
void atmo_medium(Atmosphere a, float h, out vec3 ray, out float mie, out vec3 ext) {
    float dr = exp(-h / a.rayleigh.w);
    float dm = exp(-h / a.mie.z);
    float doz = max(0.0, 1.0 - abs(h - a.ozone.w) / a.radii.z);
    ray = a.rayleigh.xyz * dr;
    mie = a.mie.x * dm;
    ext = ray + vec3(a.mie.y * dm) + a.ozone.xyz * doz;
}
// Sky radiance (per unit top illuminance) from altitude (m above sea level) along d.
vec3 atmo_sky(Atmosphere a, float altitude, vec3 d, vec3 s, int steps) {
    float Rb = a.radii.x, Rt = a.radii.y;
    float r = Rb + max(altitude, 1.0);
    float mu = d.y;
    float dg = atmo_dist_ground(r, mu, Rb);
    float len = min(dg >= 0.0 ? dg : atmo_dist_top(r, mu, Rt), 400e3);
    float c = dot(d, s);
    float pr = atmo_phase_rayleigh(c), pm = atmo_phase_mie(c, a.mie.w);
    vec3 thr = vec3(1.0), lum = vec3(0.0);
    float t_prev = 0.0;
    for (int i = 0; i < steps; ++i) {
        float k = float(i + 1) / float(steps);
        float t_next = len * k * k;
        float t = 0.5 * (t_prev + t_next), dt = t_next - t_prev;
        t_prev = t_next;
        vec3 pos = vec3(d.x * t, r + mu * t, d.z * t);
        float rs = length(pos);
        vec3 up = pos / rs;
        vec3 ray, ext;
        float mie;
        atmo_medium(a, rs - Rb, ray, mie, ext);
        float mu_sun = dot(up, s);
        vec3 S = atmo_transmittance(a, rs, mu_sun) * (ray * pr + mie * pm) + (ray + mie) * atmo_multiscatter(a, rs, mu_sun);
        vec3 ts = exp(-ext * dt);
        lum += thr * (S - S * ts) / max(ext, vec3(1e-12));
        thr *= ts;
    }
    if (dg >= 0.0 && dg <= 400e3) {
        vec3 gp = normalize(vec3(d.x * dg, r + mu * dg, d.z * dg));
        float ndl = max(dot(gp, s), 0.0);
        lum += thr * atmo_transmittance(a, Rb, ndl) * a.ground.xyz * (ndl / ATMO_PI);
    }
    return lum;
}
// Sky-view LUT parameterization: azimuth relative to the sun (u), sqrt-warped elevation (v).
vec2 skyview_uv(vec3 d, vec3 s) {
    float el = asin(clamp(d.y, -1.0, 1.0));
    float az = atan(d.x, -d.z) - atan(s.x, -s.z);
    az = az - 2.0 * ATMO_PI * floor(az / (2.0 * ATMO_PI));
    float v = 0.5 + 0.5 * sign(el) * sqrt(abs(el) / (0.5 * ATMO_PI));
    return vec2(az / (2.0 * ATMO_PI), v);
}
vec3 skyview_dir(vec2 uv, vec3 s) {
    float x = uv.y * 2.0 - 1.0;
    float el = sign(x) * x * x * 0.5 * ATMO_PI;
    float az = uv.x * 2.0 * ATMO_PI + atan(s.x, -s.z);
    return vec3(cos(el) * sin(az), sin(el), -cos(el) * cos(az));
}
#endif
