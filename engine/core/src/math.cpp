#include "tefra/core/math.h"

namespace tefra {

mat4 transpose(const mat4& m) {
    mat4 r;
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 4; ++col) r.at(row, col) = m.at(col, row);
    return r;
}

mat4 inverse(const mat4& mat) {
    const f32* m = mat.data();
    f32 inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] +
             m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] -
             m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] +
             m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] -
              m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] -
             m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] +
             m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] -
             m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] +
              m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] +
             m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] -
             m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] +
              m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] -
              m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] -
             m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] +
             m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] -
              m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] -
              m[8] * m[2] * m[5];

    const f32 det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (std::fabs(det) < 1e-30f) return mat4::identity();
    const f32 inv_det = 1.0f / det;
    mat4 r;
    f32* out = &r.c[0].x;
    for (int i = 0; i < 16; ++i) out[i] = inv[i] * inv_det;
    return r;
}

mat4 translate(vec3 t) {
    mat4 r;
    r.c[3] = vec4(t, 1.0f);
    return r;
}

mat4 scale(vec3 s) {
    mat4 r;
    r.c[0].x = s.x;
    r.c[1].y = s.y;
    r.c[2].z = s.z;
    return r;
}

mat4 mat4_from_quat(quat q) {
    const f32 xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    const f32 xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    const f32 wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
    mat4 r;
    r.c[0] = {1.0f - 2.0f * (yy + zz), 2.0f * (xy + wz), 2.0f * (xz - wy), 0.0f};
    r.c[1] = {2.0f * (xy - wz), 1.0f - 2.0f * (xx + zz), 2.0f * (yz + wx), 0.0f};
    r.c[2] = {2.0f * (xz + wy), 2.0f * (yz - wx), 1.0f - 2.0f * (xx + yy), 0.0f};
    return r;
}

mat4 compose_trs(vec3 t, quat q, vec3 s) {
    mat4 r = mat4_from_quat(q);
    r.c[0] = r.c[0] * s.x;
    r.c[1] = r.c[1] * s.y;
    r.c[2] = r.c[2] * s.z;
    r.c[3] = vec4(t, 1.0f);
    return r;
}

mat4 look_at(vec3 eye, vec3 target, vec3 up) {
    const vec3 f = normalize(target - eye);
    const vec3 s = normalize(cross(f, up));
    const vec3 u = cross(s, f);
    mat4 r;
    r.c[0] = {s.x, u.x, -f.x, 0.0f};
    r.c[1] = {s.y, u.y, -f.y, 0.0f};
    r.c[2] = {s.z, u.z, -f.z, 0.0f};
    r.c[3] = {-dot(s, eye), -dot(u, eye), dot(f, eye), 1.0f};
    return r;
}

mat4 perspective_reverse_z(f32 fovy_rad, f32 aspect, f32 z_near) {
    const f32 f = 1.0f / std::tan(fovy_rad * 0.5f);
    mat4 r;
    r.c[0] = {f / aspect, 0.0f, 0.0f, 0.0f};
    r.c[1] = {0.0f, -f, 0.0f, 0.0f}; // Vulkan NDC: +y points down
    r.c[2] = {0.0f, 0.0f, 0.0f, -1.0f};
    r.c[3] = {0.0f, 0.0f, z_near, 0.0f};
    return r;
}

} // namespace tefra
