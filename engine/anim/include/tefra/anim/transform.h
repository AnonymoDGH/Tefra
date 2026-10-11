#pragma once

// Rigid transforms (CFrame-like) for the animation system. Composition follows spec M3 §4:
// Part1 = Part0 * C0 * Transform(anim) * inverse(C1), where a*b applies b in a's space.

#include "tefra/core/math.h"

#include <cmath>

namespace tefra::anim {

struct CFrame {
    vec3 p{};
    quat q{};
};

inline quat conjugate(quat q) { return {-q.x, -q.y, -q.z, q.w}; }

inline CFrame operator*(const CFrame& a, const CFrame& b) { return {a.p + rotate(a.q, b.p), normalize(a.q * b.q)}; }

inline CFrame inverse(const CFrame& a) {
    const quat qi = conjugate(a.q);
    return {rotate(qi, -a.p), qi};
}

// Shortest-arc spherical interpolation; falls back to nlerp when the rotations are nearly equal.
inline quat slerp(quat a, quat b, f32 t) {
    f32 d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (d < 0.0f) {
        b = {-b.x, -b.y, -b.z, -b.w};
        d = -d;
    }
    if (d > 0.9995f) {
        return normalize(quat{lerp(a.x, b.x, t), lerp(a.y, b.y, t), lerp(a.z, b.z, t), lerp(a.w, b.w, t)});
    }
    const f32 theta = std::acos(clamp(d, -1.0f, 1.0f));
    const f32 s = std::sin(theta);
    const f32 wa = std::sin((1.0f - t) * theta) / s;
    const f32 wb = std::sin(t * theta) / s;
    return normalize(quat{a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb});
}

} // namespace tefra::anim
