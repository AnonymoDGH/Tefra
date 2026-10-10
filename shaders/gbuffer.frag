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
layout(location = 0) out vec4 o_albedo;   // RGBA8_SRGB: base color, material AO
layout(location = 1) out vec4 o_normal;   // A2B10G10R10: oct normal, roughness, class
layout(location = 2) out vec4 o_material; // RGBA8: metallic, translucency, emissive, wetness
layout(location = 3) out vec2 o_velocity; // RG16F
void main() {
    Material m = pc.materials.m[v_material];
    vec4 base = texture(TEX2D(m.base_color_tex, SMP_ANISO_REPEAT), v_uv) * m.base_color;
    if (m.emissive_cutoff.w > 0.0 && base.a < m.emissive_cutoff.w) discard;
    vec3 orm = texture(TEX2D(m.orm_tex, SMP_ANISO_REPEAT), v_uv).rgb;
    float ao = mix(1.0, orm.r, m.occlusion_strength);
    float rough = clamp(orm.g * m.roughness, 0.02, 1.0);
    float metal = clamp(orm.b * m.metallic, 0.0, 1.0);
    vec3 tn = texture(TEX2D(m.normal_tex, SMP_ANISO_REPEAT), v_uv).xyz * 2.0 - 1.0;
    tn.xy *= m.normal_scale;
    vec3 N = normalize(v_normal);
    if (!gl_FrontFacing) N = -N;
    vec3 T = normalize(v_tangent.xyz - N * dot(N, v_tangent.xyz));
    vec3 B = cross(N, T) * v_tangent.w;
    N = normalize(T * tn.x + B * tn.y + N * max(tn.z, 1e-3));
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
