#ifndef TEFRA_FOG_GLSL
#define TEFRA_FOG_GLSL
// Requires gpu_types.glsl. Radiance units: un-exposed (multiply by exposure afterwards).
// Exponential height fog: density(h) = fog.x * exp(-fog.y * (h - fog.z)), albedo 1 (scattering = extinction).
const float FOG_PI = 3.14159265358979;

// Camera-to-distance transmittance along dir (exact integral of the exponential density).
float fog_transmittance(Frame f, vec3 dir, float dist) {
    float dens = f.fog.x;
    if (dens <= 0.0) return 1.0;
    float b = f.fog.y;
    float h0 = f.camera_pos.y - f.fog.z;
    float k = dir.y * b * dist;
    float shape = abs(k) > 1e-4 ? (1.0 - exp(-k)) / k : 1.0 - 0.5 * k;
    return exp(-dens * exp(-b * h0) * dist * shape);
}
// Sky ambient of the fog: average of the SH irradiance up and down.
vec3 fog_ambient(Frame f) {
    return (sh9_eval(f, vec3(0, 1, 0)) + sh9_eval(f, vec3(0, -1, 0))) / (2.0 * FOG_PI) * f.exposure.y;
}
// Henyey-Greenstein phase for the sun (dir = view ray direction, away from the camera).
float fog_phase(Frame f, vec3 dir) {
    float g = f.fog.w, c = dot(dir, f.sun_dir.xyz);
    return (1.0 - g * g) / (4.0 * FOG_PI * pow(1.0 + g * g - 2.0 * g * c, 1.5));
}
// Uniform in-scattered radiance (sun term without shadowing) of the fog along dir.
vec3 fog_radiance(Frame f, vec3 dir) {
    return (fog_ambient(f) + f.sun_color.rgb * f.sun_dir.w * fog_phase(f, dir)) * f.fog_tint.rgb;
}
// Transmittance and in-scattered radiance of the exponential height fog along a ray of length dist.
void fog_terms(Frame f, vec3 dir, float dist, out float T, out vec3 inscatter) {
    T = fog_transmittance(f, dir, dist);
    inscatter = fog_radiance(f, dir) * (1.0 - T);
}
vec3 apply_fog(Frame f, vec3 color, vec3 dir, float dist) {
    float T;
    vec3 ins;
    fog_terms(f, dir, dist, T, ins);
    return color * T + ins;
}
#endif
