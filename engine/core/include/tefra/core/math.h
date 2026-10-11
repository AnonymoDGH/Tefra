#pragma once

// Minimal linear algebra. Conventions (spec ADR-009): right-handed, +Y up, -Z forward,
// column-major matrices (same memory layout as GLSL), column vectors (v' = M * v),
// Vulkan clip space with reverse-Z depth (near = 1, far = 0).

#include "tefra/core/types.h"

#include <cmath>

namespace tefra {

constexpr f32 kPi = 3.14159265358979323846f;
constexpr f32 kTwoPi = 2.0f * kPi;
constexpr f32 kHalfPi = 0.5f * kPi;

inline f32 radians(f32 deg) { return deg * (kPi / 180.0f); }
inline f32 degrees(f32 rad) { return rad * (180.0f / kPi); }
template <typename T> constexpr T min(T a, T b) { return a < b ? a : b; }
template <typename T> constexpr T max(T a, T b) { return a > b ? a : b; }
template <typename T> constexpr T clamp(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline f32 lerp(f32 a, f32 b, f32 t) { return a + (b - a) * t; }
inline f32 saturate(f32 v) { return clamp(v, 0.0f, 1.0f); }

// IEEE 754 binary16 conversion (round to nearest even), for half-float texture uploads.
u16 f32_to_f16(f32 v);
f32 f16_to_f32(u16 h);

struct vec2 {
    f32 x = 0, y = 0;
    constexpr vec2() = default;
    constexpr vec2(f32 x_, f32 y_) : x(x_), y(y_) {}
    constexpr explicit vec2(f32 s) : x(s), y(s) {}
};

struct vec3 {
    f32 x = 0, y = 0, z = 0;
    constexpr vec3() = default;
    constexpr vec3(f32 x_, f32 y_, f32 z_) : x(x_), y(y_), z(z_) {}
    constexpr explicit vec3(f32 s) : x(s), y(s), z(s) {}
    f32& operator[](int i) { return (&x)[i]; }
    f32 operator[](int i) const { return (&x)[i]; }
};

struct vec4 {
    f32 x = 0, y = 0, z = 0, w = 0;
    constexpr vec4() = default;
    constexpr vec4(f32 x_, f32 y_, f32 z_, f32 w_) : x(x_), y(y_), z(z_), w(w_) {}
    constexpr vec4(const vec3& v, f32 w_) : x(v.x), y(v.y), z(v.z), w(w_) {}
    constexpr explicit vec4(f32 s) : x(s), y(s), z(s), w(s) {}
    f32& operator[](int i) { return (&x)[i]; }
    f32 operator[](int i) const { return (&x)[i]; }
    constexpr vec3 xyz() const { return {x, y, z}; }
};

// World-space position in double precision (CPU only; GPU gets camera-relative floats).
struct dvec3 {
    f64 x = 0, y = 0, z = 0;
    constexpr dvec3() = default;
    constexpr dvec3(f64 x_, f64 y_, f64 z_) : x(x_), y(y_), z(z_) {}
};

// vec2
constexpr vec2 operator+(vec2 a, vec2 b) { return {a.x + b.x, a.y + b.y}; }
constexpr vec2 operator-(vec2 a, vec2 b) { return {a.x - b.x, a.y - b.y}; }
constexpr vec2 operator*(vec2 a, f32 s) { return {a.x * s, a.y * s}; }
constexpr f32 dot(vec2 a, vec2 b) { return a.x * b.x + a.y * b.y; }

// vec3
constexpr vec3 operator+(vec3 a, vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
constexpr vec3 operator-(vec3 a, vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
constexpr vec3 operator-(vec3 a) { return {-a.x, -a.y, -a.z}; }
constexpr vec3 operator*(vec3 a, vec3 b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
constexpr vec3 operator*(vec3 a, f32 s) { return {a.x * s, a.y * s, a.z * s}; }
constexpr vec3 operator*(f32 s, vec3 a) { return a * s; }
constexpr vec3 operator/(vec3 a, f32 s) { return {a.x / s, a.y / s, a.z / s}; }
constexpr vec3 operator/(vec3 a, vec3 b) { return {a.x / b.x, a.y / b.y, a.z / b.z}; }
inline vec3& operator+=(vec3& a, vec3 b) { return a = a + b; }
inline vec3& operator-=(vec3& a, vec3 b) { return a = a - b; }
inline vec3& operator*=(vec3& a, f32 s) { return a = a * s; }
constexpr f32 dot(vec3 a, vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
constexpr vec3 cross(vec3 a, vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline f32 length(vec3 a) { return std::sqrt(dot(a, a)); }
inline vec3 normalize(vec3 a) {
    const f32 len = length(a);
    return len > 0.0f ? a / len : vec3{};
}
inline vec3 lerp(vec3 a, vec3 b, f32 t) { return a + (b - a) * t; }

// vec4
constexpr vec4 operator+(vec4 a, vec4 b) { return {a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w}; }
constexpr vec4 operator-(vec4 a, vec4 b) { return {a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w}; }
constexpr vec4 operator*(vec4 a, f32 s) { return {a.x * s, a.y * s, a.z * s, a.w * s}; }
constexpr f32 dot(vec4 a, vec4 b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }

// dvec3
constexpr dvec3 operator+(dvec3 a, dvec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
constexpr dvec3 operator-(dvec3 a, dvec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
// Camera-relative float position (the only form that reaches the GPU).
inline vec3 relative_to(dvec3 p, dvec3 origin) {
    return {static_cast<f32>(p.x - origin.x), static_cast<f32>(p.y - origin.y), static_cast<f32>(p.z - origin.z)};
}

// Quaternion (x, y, z, w), unit length for rotations.
struct quat {
    f32 x = 0, y = 0, z = 0, w = 1;
    constexpr quat() = default;
    constexpr quat(f32 x_, f32 y_, f32 z_, f32 w_) : x(x_), y(y_), z(z_), w(w_) {}
};

inline quat quat_axis_angle(vec3 axis, f32 angle) {
    const vec3 n = normalize(axis);
    const f32 s = std::sin(angle * 0.5f);
    return {n.x * s, n.y * s, n.z * s, std::cos(angle * 0.5f)};
}
constexpr quat operator*(quat a, quat b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
inline quat normalize(quat q) {
    const f32 len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    return len > 0.0f ? quat{q.x / len, q.y / len, q.z / len, q.w / len} : quat{};
}
inline vec3 rotate(quat q, vec3 v) {
    const vec3 u{q.x, q.y, q.z};
    const vec3 t = cross(u, v) * 2.0f;
    return v + t * q.w + cross(u, t);
}

// 4x4 column-major: c[col] is a column.
struct mat4 {
    vec4 c[4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
    static constexpr mat4 identity() { return {}; }
    f32& at(int row, int col) { return c[col][row]; }
    f32 at(int row, int col) const { return c[col][row]; }
    const f32* data() const { return &c[0].x; }
};

inline vec4 operator*(const mat4& m, vec4 v) { return m.c[0] * v.x + m.c[1] * v.y + m.c[2] * v.z + m.c[3] * v.w; }
inline mat4 operator*(const mat4& a, const mat4& b) {
    mat4 r;
    for (int i = 0; i < 4; ++i) r.c[i] = a * b.c[i];
    return r;
}
inline vec3 transform_point(const mat4& m, vec3 p) { return (m * vec4(p, 1.0f)).xyz(); }
inline vec3 transform_dir(const mat4& m, vec3 d) { return (m * vec4(d, 0.0f)).xyz(); }

mat4 transpose(const mat4& m);
// General inverse. Returns identity if singular.
mat4 inverse(const mat4& m);
mat4 translate(vec3 t);
mat4 scale(vec3 s);
mat4 mat4_from_quat(quat q);
// Translation * rotation * scale.
mat4 compose_trs(vec3 t, quat r, vec3 s);
// Right-handed view matrix (camera looks along -Z in view space).
mat4 look_at(vec3 eye, vec3 target, vec3 up);
// Reverse-Z, infinite far plane, Vulkan clip space (y down, depth 1 at near -> 0 at infinity).
mat4 perspective_reverse_z(f32 fovy_rad, f32 aspect, f32 z_near);

} // namespace tefra
