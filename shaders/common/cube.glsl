#ifndef TEFRA_CUBE_GLSL
#define TEFRA_CUBE_GLSL
// Direction for cube face `face` at st in [-1,1] (Vulkan/GL major-axis convention).
vec3 cube_dir(uint face, vec2 st) {
    float s = st.x, t = st.y;
    vec3 d;
    if (face == 0u) d = vec3(1.0, -t, -s);
    else if (face == 1u) d = vec3(-1.0, -t, s);
    else if (face == 2u) d = vec3(s, 1.0, t);
    else if (face == 3u) d = vec3(s, -1.0, -t);
    else if (face == 4u) d = vec3(s, -t, 1.0);
    else d = vec3(-s, -t, -1.0);
    return normalize(d);
}
vec2 equirect_uv(vec3 d) {
    const float PI_ = 3.14159265358979;
    return vec2(atan(d.x, -d.z) / (2.0 * PI_) + 0.5, acos(clamp(d.y, -1.0, 1.0)) / PI_);
}
#endif
