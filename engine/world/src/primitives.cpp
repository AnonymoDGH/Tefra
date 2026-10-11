#include "primitives.h"

#include <cmath>

namespace tefra::world {

render::MeshData make_unit_cylinder(u32 segments) {
    render::MeshData m;
    const f32 r = 0.5f, h = 0.5f;
    // Side.
    for (u32 i = 0; i <= segments; ++i) {
        const f32 u = f32(i) / f32(segments), a = u * kTwoPi;
        const vec3 n{std::cos(a), 0.0f, std::sin(a)};
        const vec3 t{-std::sin(a), 0.0f, std::cos(a)};
        m.vertices.push_back(render::make_vertex({n.x * r, -h, n.z * r}, n, {u * 3.0f, 1.0f}, t, 1.0f));
        m.vertices.push_back(render::make_vertex({n.x * r, h, n.z * r}, n, {u * 3.0f, 0.0f}, t, 1.0f));
    }
    for (u32 i = 0; i < segments; ++i) {
        const u32 a = i * 2, b = a + 1, c = a + 2, d = a + 3;
        m.indices.insert(m.indices.end(), {a, b, c, c, b, d});
    }
    // Caps.
    for (int cap = 0; cap < 2; ++cap) {
        const f32 y = cap ? h : -h;
        const vec3 n{0.0f, cap ? 1.0f : -1.0f, 0.0f};
        const u32 centre = static_cast<u32>(m.vertices.size());
        m.vertices.push_back(render::make_vertex({0, y, 0}, n, {0.5f, 0.5f}, {1, 0, 0}, 1.0f));
        for (u32 i = 0; i <= segments; ++i) {
            const f32 a = f32(i) / f32(segments) * kTwoPi;
            const f32 x = std::cos(a) * r, z = std::sin(a) * r;
            m.vertices.push_back(render::make_vertex({x, y, z}, n, {x + 0.5f, z + 0.5f}, {1, 0, 0}, 1.0f));
        }
        for (u32 i = 0; i < segments; ++i) {
            const u32 a = centre + 1 + i, b = a + 1;
            if (cap)
                m.indices.insert(m.indices.end(), {centre, b, a});
            else
                m.indices.insert(m.indices.end(), {centre, a, b});
        }
    }
    return m;
}

render::MeshData make_unit_wedge() {
    render::MeshData m;
    auto quad = [&](vec3 a, vec3 b, vec3 c, vec3 d, vec3 n, vec3 t) { // counter-clockwise seen from n
        const u32 base = static_cast<u32>(m.vertices.size());
        m.vertices.push_back(render::make_vertex(a, n, {0, 1}, t, 1.0f));
        m.vertices.push_back(render::make_vertex(b, n, {1, 1}, t, 1.0f));
        m.vertices.push_back(render::make_vertex(c, n, {1, 0}, t, 1.0f));
        m.vertices.push_back(render::make_vertex(d, n, {0, 0}, t, 1.0f));
        m.indices.insert(m.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    };
    auto tri = [&](vec3 a, vec3 b, vec3 c, vec3 n, vec3 t) {
        const u32 base = static_cast<u32>(m.vertices.size());
        m.vertices.push_back(render::make_vertex(a, n, {0, 1}, t, 1.0f));
        m.vertices.push_back(render::make_vertex(b, n, {1, 1}, t, 1.0f));
        m.vertices.push_back(render::make_vertex(c, n, {1, 0}, t, 1.0f));
        m.indices.insert(m.indices.end(), {base, base + 1, base + 2});
    };
    const f32 s = 0.5f;
    // Bottom (y = -s), back (z = +s), slope (from front-bottom to back-top), sides.
    quad({-s, -s, s}, {-s, -s, -s}, {s, -s, -s}, {s, -s, s}, {0, -1, 0}, {1, 0, 0});
    quad({s, -s, s}, {s, s, s}, {-s, s, s}, {-s, -s, s}, {0, 0, 1}, {-1, 0, 0});
    const vec3 sn = normalize(vec3{0, 1, -1});
    quad({-s, -s, -s}, {-s, s, s}, {s, s, s}, {s, -s, -s}, sn, {1, 0, 0});
    tri({s, -s, -s}, {s, s, s}, {s, -s, s}, {1, 0, 0}, {0, 0, 1});
    tri({-s, -s, s}, {-s, s, s}, {-s, -s, -s}, {-1, 0, 0}, {0, 0, -1});
    return m;
}

} // namespace tefra::world
