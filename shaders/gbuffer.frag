#version 460
#extension GL_GOOGLE_include_directive : require
#include "common/gpu_types.glsl"
#include "common/bindless.glsl"
layout(push_constant) uniform Push {
    FrameBuffer frame; VertexBuffer vertices; InstanceBuffer instances; MaterialBuffer materials; uint cascade;
} pc;
layout(location = 0) in vec3 v_pos;
layout(location = 1) in vec3 v_normal;
layout(location = 2) in vec4 v_tangent;
layout(location = 3) in vec2 v_uv;
layout(location = 4) flat in uint v_material;
layout(location = 5) flat in float v_rand; // per-instance random (vegetation), 0 for other meshes
layout(location = 0) out vec4 o_albedo;   // RGBA8_SRGB: base color, material AO
layout(location = 1) out vec4 o_normal;   // A2B10G10R10: oct normal, roughness, class
layout(location = 2) out vec4 o_material; // RGBA8: metallic, translucency, emissive, wetness
layout(location = 3) out vec2 o_velocity; // RG16F

// Material flag bit 2: bark. Bark gets a per-instance tint and moss (coverage in pad0, as float bits).
const uint kFlagBark = 4u;
const float kTrunkUvHeight = 1.6; // metres per unit of V on trunks (see add_trunk)

float hash13(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.zyx + 31.32);
    return fract((p.x + p.y) * p.z);
}
float value_noise(vec3 p) {
    vec3 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = mix(mix(hash13(i), hash13(i + vec3(1, 0, 0)), f.x), mix(hash13(i + vec3(0, 1, 0)), hash13(i + vec3(1, 1, 0)), f.x), f.y);
    float b = mix(mix(hash13(i + vec3(0, 0, 1)), hash13(i + vec3(1, 0, 1)), f.x), mix(hash13(i + vec3(0, 1, 1)), hash13(i + vec3(1, 1, 1)), f.x), f.y);
    return mix(a, b, f.z);
}
float fbm3(vec3 p) {
    return 0.55 * value_noise(p) + 0.30 * value_noise(p * 2.03 + 7.1) + 0.15 * value_noise(p * 4.1 + 3.7);
}

void main() {
    Material m = pc.materials.m[v_material];
    vec4 base = texture(TEX2D(m.base_color_tex, SMP_ANISO_REPEAT), v_uv) * m.base_color;
    if (m.emissive_cutoff.w > 0.0 && base.a < m.emissive_cutoff.w) discard;
    vec3 orm = texture(TEX2D(m.orm_tex, SMP_ANISO_REPEAT), v_uv).rgb;
    float ao = mix(1.0, orm.r, m.occlusion_strength);
    if ((m.flags & 1u) != 0u) ao *= clamp(abs(v_tangent.w), 0.0, 1.0); // foliage: baked crown occlusion
    float rough = clamp(orm.g * m.roughness, 0.02, 1.0);
    float metal = clamp(orm.b * m.metallic, 0.0, 1.0);
    vec3 tn = texture(TEX2D(m.normal_tex, SMP_ANISO_REPEAT), v_uv).xyz * 2.0 - 1.0;
    tn.xy *= m.normal_scale;

    float moss = 0.0;
    if ((m.flags & kFlagBark) != 0u) {
        // Per-instance tint: slightly darker or lighter, warmer or cooler.
        base.rgb *= mix(vec3(0.86, 0.80, 0.74), vec3(1.10, 1.02, 0.94), v_rand);
        // Moss: periodic in angle (from the horizontal normal, so no seam), patchy, on the base and on the
        // north side (world -Z). Height comes from V (V = y / kTrunkUvHeight, the trunk axis).
        const float hgt = max(v_uv.y * kTrunkUvHeight, 0.0);
        const vec2 hn = normalize(v_normal.xz + vec2(1e-5));
        const float north = dot(hn, vec2(0.0, -1.0));
        const float orient = smoothstep(-0.2, 0.8, north);
        const vec3 q = vec3(hn.x, hn.y, hgt * 0.6) * 2.5 + vec3(v_rand * 7.3);
        const float patchy = fbm3(q);
        const float fine = value_noise(q * 4.0 + 11.0);
        const float base_w = 1.0 - smoothstep(1.0, 2.8, hgt + (patchy - 0.5) * 1.5);
        const float side_w = mix(0.4, 1.0, orient);
        const float clump = smoothstep(0.36, 0.52, 0.7 * patchy + 0.3 * fine);
        moss = clamp(uintBitsToFloat(m.pad0) * base_w * side_w * clump, 0.0, 1.0);
        const vec3 moss_col = mix(vec3(0.030, 0.075, 0.018), vec3(0.090, 0.150, 0.045), fine);
        base.rgb = mix(base.rgb, moss_col, moss * 0.9);
        tn = mix(tn, vec3(0.0, 0.0, 1.0), moss * 0.6); // moss is soft: it hides the bark relief
    }

    vec3 N = normalize(v_normal);
    if (!gl_FrontFacing) N = -N;
    vec3 T = normalize(v_tangent.xyz - N * dot(N, v_tangent.xyz));
    vec3 B = cross(N, T) * (v_tangent.w < 0.0 ? -1.0 : 1.0);
    N = normalize(T * tn.x + B * tn.y + N * max(tn.z, 1e-3));
    // Moss: rough and non-metallic, so it has almost no specular.
    rough = mix(rough, 0.95, moss);
    metal = mix(metal, 0.0, moss);
    // Geometric specular AA (Tokuyoshi & Kaplanyan 2019).
    vec3 dndx = dFdx(N), dndy = dFdy(N);
    float variance = 0.25 * (dot(dndx, dndx) + dot(dndy, dndy));
    float a2 = rough * rough * rough * rough;
    a2 = clamp(a2 + min(2.0 * variance, 0.18), 0.0, 1.0);
    rough = sqrt(sqrt(a2));
    float emissive = 0.0;
    o_albedo = vec4(base.rgb, ao);
    o_normal = vec4(oct_encode01(N), rough, (m.flags & 1u) != 0u ? 1.0 / 3.0 : 0.0);
    o_material = vec4(metal, m.translucency, emissive, 0.0);
    o_velocity = vec2(0.0);
}
