#include "tefra/core/math.h"

#include <cstring>

namespace tefra {

u16 f32_to_f16(f32 value) {
    u32 x;
    std::memcpy(&x, &value, 4);
    const u32 sign = (x >> 16) & 0x8000u;
    const u32 mant = x & 0x007FFFFFu;
    const i32 exp = static_cast<i32>((x >> 23) & 0xFFu);
    if (exp == 0xFF) return static_cast<u16>(sign | 0x7C00u | (mant ? 0x200u : 0u)); // inf / nan
    i32 e = exp - 127 + 15;
    if (e >= 0x1F) return static_cast<u16>(sign | 0x7C00u); // overflow -> inf
    if (e <= 0) {
        if (e < -10) return static_cast<u16>(sign); // underflow -> signed zero
        const u32 m = mant | 0x00800000u;
        const u32 shift = static_cast<u32>(14 - e);
        u32 h = m >> shift;
        const u32 rem = m & ((1u << shift) - 1u), half = 1u << (shift - 1u);
        if (rem > half || (rem == half && (h & 1u))) ++h;
        return static_cast<u16>(sign | h);
    }
    u32 h = (static_cast<u32>(e) << 10) | (mant >> 13);
    const u32 rem = mant & 0x1FFFu;
    if (rem > 0x1000u || (rem == 0x1000u && (h & 1u))) ++h; // may carry into the exponent: still correct
    return static_cast<u16>(sign | h);
}

f32 f16_to_f32(u16 h) {
    const u32 sign = (static_cast<u32>(h) & 0x8000u) << 16;
    u32 exp = (h >> 10) & 0x1Fu;
    u32 mant = h & 0x3FFu;
    u32 x;
    if (exp == 0) {
        if (mant == 0) {
            x = sign;
        } else {
            exp = 127 - 15 + 1;
            while (!(mant & 0x400u)) {
                mant <<= 1;
                --exp;
            }
            mant &= 0x3FFu;
            x = sign | (exp << 23) | (mant << 13);
        }
    } else if (exp == 0x1F) {
        x = sign | 0x7F800000u | (mant << 13);
    } else {
        x = sign | ((exp - 15 + 127) << 23) | (mant << 13);
    }
    f32 out;
    std::memcpy(&out, &x, 4);
    return out;
}

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
