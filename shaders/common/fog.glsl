#ifndef TEFRA_FOG_GLSL
#define TEFRA_FOG_GLSL
// Requires gpu_types.glsl. Radiance units: un-exposed (multiply by exposure afterwards).
const float FOG_PI = 3.14159265358979;
// Transmittance and in-scattered radiance of the exponential height fog along a ray of length dist.
void fog_terms(Frame f, vec3 dir, float dist, out float T, out vec3 inscatter) {
    float dens = f.fog.x;
    if (dens <= 0.0) { T = 1.0; inscatter = vec3(0.0); return; }
    float b = f.fog.y;
    float h0 = f.camera_pos.y - f.fog.z;
    float k = dir.y * b * dist;
    float shape = abs(k) > 1e-4 ? (1.0 - exp(-k)) / k : 1.0 - 0.5 * k;
    T = exp(-dens * exp(-b * h0) * dist * shape);
    vec3 ambient = (sh9_eval(f, vec3(0, 1, 0)) + sh9_eval(f, vec3(0, -1, 0))) / (2.0 * FOG_PI) * f.exposure.y;
    float g = f.fog.w, c = dot(dir, f.sun_dir.xyz);
    float hg = (1.0 - g * g) / (4.0 * FOG_PI * pow(1.0 + g * g - 2.0 * g * c, 1.5));
    inscatter = (ambient + f.sun_color.rgb * f.sun_dir.w * hg) * (1.0 - T);
}
vec3 apply_fog(Frame f, vec3 color, vec3 dir, float dist) {
    float T;
    vec3 ins;
    fog_terms(f, dir, dist, T, ins);
    return color * T + ins;
}
#endif
