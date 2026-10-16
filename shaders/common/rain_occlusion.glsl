#ifndef TEFRA_RAIN_OCCLUSION_GLSL
#define TEFRA_RAIN_OCCLUSION_GLSL
// Top-down depth of the scene around the camera (orthographic, tile 7 of the local shadow atlas, rendered when it
// rains): 1 where the sky is open above camera-relative point p, 0 under a roof, a balcony or a tree. Rain streaks
// and the wet look of surfaces use it, so it does not rain indoors.
float rain_visibility(Frame f, vec3 p) {
    if (f.rain_occlusion == 0u || f.local_shadow_tex == 0xFFFFFFFFu) return 1.0;
    vec4 s = f.local_shadow_vp[7] * vec4(p, 1.0);
    vec2 uv = s.xy * 0.5 + 0.5;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))) || s.z < 0.0 || s.z > 1.0) return 1.0;
    vec4 a = f.local_shadow_atlas[7];
    return texture(sampler2DShadow(g_textures[f.local_shadow_tex], g_samplers[SMP_SHADOW]),
                   vec3(uv * a.zw + a.xy, s.z - 0.004));
}
#endif
