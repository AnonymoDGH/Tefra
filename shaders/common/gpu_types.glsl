#ifndef TEFRA_GPU_TYPES_GLSL
#define TEFRA_GPU_TYPES_GLSL
// Mirrors engine/renderer/include/tefra/renderer/gpu_types.h (std430).
#extension GL_EXT_buffer_reference : require

struct Vertex { vec3 pos; uint normal; vec2 uv; uint tangent; float tangent_sign; };
struct Material {
    vec4 base_color; vec4 emissive_cutoff;
    float roughness; float metallic; float normal_scale; float occlusion_strength;
    uint base_color_tex; uint normal_tex; uint orm_tex; uint emissive_tex;
    uint flags; float translucency; uint pad0; uint pad1;
};
struct Instance { vec4 model_rows[3]; vec4 normal_rows[3]; vec4 prev_rows[3]; uint material; uint flags; uint p1; uint p2; };
struct Atmosphere {
    vec4 rayleigh; vec4 mie; vec4 ozone; vec4 radii; vec4 ground; vec4 sun;
    uint transmittance_lut; uint multiscatter_lut; uint skyview_lut; uint pad;
};
struct Frame {
    mat4 view_proj; mat4 inv_view_proj; mat4 view; mat4 proj;
    mat4 cascade_view_proj[4];
    vec4 cascade_splits; vec4 cascade_atlas[4]; vec4 shadow_params;
    vec4 sun_dir; vec4 sun_color; vec4 camera_pos; vec4 exposure; vec4 viewport;
    vec4 sh[9];
    uint env_cube; uint env_mips; uint brdf_lut; uint shadow_atlas;
    uint env_background; uint canopy_map; uint shadow_flags; float veg_lod_scale; // canopy_map: bindless 2D, ~0u = none
    mat4 prev_view_proj; // previous frame, expressed in the current camera-relative space
    vec4 jitter;         // xy current jitter (NDC), zw previous
    vec4 fog; // x density (1/m) at reference height, y height falloff (1/m), z reference height (m), w anisotropy
    vec4 canopy; // canopy occlusion: xy world xz of the map's min corner (m), z size (m), w strength
    vec4 shadow_soft; // x penumbra scale, y contact shadow length (m)
    vec4 ocean;            // x still sea level (world y), y 1 = ocean present (underwater light + caustics)
    vec4 water_absorption; // xyz 1/m
    vec4 water_scattering; // xyz 1/m
    vec4 clouds;           // x coverage (0 = none), y base altitude (m), z feature size (m), w drift (m)
    Atmosphere atmo;
    vec4 celestial;  // x sun disc (0/1), y moon disc (0/1), z moon angular radius (rad), w sun glare strength
    vec4 moon;       // xyz towards the moon, w moon phase (0 new, 0.5 full)
    vec4 stars;      // x star count (whole sphere), y brightness, z night (0 day .. 1 night), w unused
    vec4 fog_tint;   // xyz colour of the height fog
    vec4 cloud_look; // xyz colour of the cumulus, w optical depth multiplier
    vec4 cloud_wind; // xy unit wind direction (world x, z)
    vec4 sun_true;   // xyz towards the sun itself (sun_dir is the moon at night)
};
layout(buffer_reference, std430, buffer_reference_align = 16) readonly buffer VertexBuffer { Vertex v[]; };
layout(buffer_reference, std430, buffer_reference_align = 16) readonly buffer MaterialBuffer { Material m[]; };
layout(buffer_reference, std430, buffer_reference_align = 16) readonly buffer InstanceBuffer { Instance i[]; };
layout(buffer_reference, std430, buffer_reference_align = 16) readonly buffer FrameBuffer { Frame f; };

vec3 oct_decode(uint p) {
    vec2 e = unpackSnorm2x16(p);
    vec3 n = vec3(e, 1.0 - abs(e.x) - abs(e.y));
    float t = max(-n.z, 0.0);
    n.x += n.x >= 0.0 ? -t : t;
    n.y += n.y >= 0.0 ? -t : t;
    return normalize(n);
}
vec2 oct_encode01(vec3 n) {
    n /= abs(n.x) + abs(n.y) + abs(n.z);
    vec2 e = n.z >= 0.0 ? n.xy : (1.0 - abs(n.yx)) * vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
    return e * 0.5 + 0.5;
}
vec3 oct_decode01(vec2 f) {
    vec2 e = f * 2.0 - 1.0;
    vec3 n = vec3(e, 1.0 - abs(e.x) - abs(e.y));
    float t = max(-n.z, 0.0);
    n.x += n.x >= 0.0 ? -t : t;
    n.y += n.y >= 0.0 ? -t : t;
    return normalize(n);
}
vec3 sh9_eval(Frame f, vec3 d) {
    vec3 r = f.sh[0].rgb * 0.282095
           + f.sh[1].rgb * (0.488603 * d.y) + f.sh[2].rgb * (0.488603 * d.z) + f.sh[3].rgb * (0.488603 * d.x)
           + f.sh[4].rgb * (1.092548 * d.x * d.y) + f.sh[5].rgb * (1.092548 * d.y * d.z)
           + f.sh[6].rgb * (0.315392 * (3.0 * d.z * d.z - 1.0)) + f.sh[7].rgb * (1.092548 * d.x * d.z)
           + f.sh[8].rgb * (0.546274 * (d.x * d.x - d.y * d.y));
    return max(r, vec3(0.0));
}
#endif
