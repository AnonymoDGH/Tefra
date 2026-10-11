#include "tefra/renderer/mesh.h"

#include <cmath>

namespace tefra::render {

namespace {
i16 to_snorm16(f32 v) { return static_cast<i16>(std::lround(clamp(v, -1.0f, 1.0f) * 32767.0f)); }
f32 from_snorm16(i16 v) { return max(static_cast<f32>(v) / 32767.0f, -1.0f); }
f32 sign_nz(f32 v) { return v >= 0.0f ? 1.0f : -1.0f; }
} // namespace

u32 oct_encode(vec3 n) {
    const f32 l1 = std::fabs(n.x) + std::fabs(n.y) + std::fabs(n.z);
    vec2 e{n.x / l1, n.y / l1};
    if (n.z < 0.0f) e = {(1.0f - std::fabs(e.y)) * sign_nz(e.x), (1.0f - std::fabs(e.x)) * sign_nz(e.y)};
    const u16 x = static_cast<u16>(to_snorm16(e.x));
    const u16 y = static_cast<u16>(to_snorm16(e.y));
    return static_cast<u32>(x) | (static_cast<u32>(y) << 16);
}

vec3 oct_decode(u32 packed) {
    const f32 ex = from_snorm16(static_cast<i16>(packed & 0xFFFFu));
    const f32 ey = from_snorm16(static_cast<i16>(packed >> 16));
    vec3 n{ex, ey, 1.0f - std::fabs(ex) - std::fabs(ey)};
    const f32 t = max(-n.z, 0.0f);
    n.x += n.x >= 0.0f ? -t : t;
    n.y += n.y >= 0.0f ? -t : t;
    return normalize(n);
}

Vertex make_vertex(vec3 pos, vec3 normal, vec2 uv, vec3 tangent, f32 tangent_sign) {
    Vertex v{};
    v.pos[0] = pos.x;
    v.pos[1] = pos.y;
    v.pos[2] = pos.z;
    v.normal = oct_encode(normalize(normal));
    v.uv[0] = uv.x;
    v.uv[1] = uv.y;
    v.tangent = oct_encode(normalize(tangent));
    v.tangent_sign = tangent_sign;
    return v;
}

MeshData make_sphere(f32 radius, u32 segments, u32 rings) {
    MeshData m;
    for (u32 r = 0; r <= rings; ++r) {
        const f32 v = static_cast<f32>(r) / static_cast<f32>(rings);
        const f32 theta = v * kPi; // 0 at +Y
        for (u32 s = 0; s <= segments; ++s) {
            const f32 u = static_cast<f32>(s) / static_cast<f32>(segments);
            const f32 phi = u * kTwoPi;
            const vec3 n{std::sin(theta) * std::cos(phi), std::cos(theta), -std::sin(theta) * std::sin(phi)};
            vec3 t{-std::sin(phi), 0.0f, -std::cos(phi)}; // d(position)/du direction
            if (length(t) < 1e-6f) t = {1, 0, 0};
            m.vertices.push_back(make_vertex(n * radius, n, {u * 2.0f, v}, t, 1.0f));
        }
    }
    const u32 stride = segments + 1;
    for (u32 r = 0; r < rings; ++r) {
        for (u32 s = 0; s < segments; ++s) {
            const u32 a = r * stride + s, b = a + stride, c = a + 1, d = b + 1;
            m.indices.insert(m.indices.end(), {a, b, c, c, b, d});
        }
    }
    return m;
}

MeshData make_plane(f32 sx, f32 sz, f32 uv_scale) {
    MeshData m;
    const f32 hx = sx * 0.5f, hz = sz * 0.5f;
    const vec3 n{0, 1, 0}, t{1, 0, 0};
    m.vertices.push_back(make_vertex({-hx, 0, -hz}, n, {-hx / uv_scale, -hz / uv_scale}, t, 1.0f));
    m.vertices.push_back(make_vertex({-hx, 0, hz}, n, {-hx / uv_scale, hz / uv_scale}, t, 1.0f));
    m.vertices.push_back(make_vertex({hx, 0, hz}, n, {hx / uv_scale, hz / uv_scale}, t, 1.0f));
    m.vertices.push_back(make_vertex({hx, 0, -hz}, n, {hx / uv_scale, -hz / uv_scale}, t, 1.0f));
    m.indices = {0, 1, 2, 0, 2, 3};
    return m;
}

MeshData make_box(vec3 h) {
    MeshData m;
    struct Face {
        vec3 n, t, b;
    };
    const Face faces[6] = {{{1, 0, 0}, {0, 0, -1}, {0, 1, 0}}, {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
                           {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}}, {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
                           {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},  {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}}};
    for (const Face& f : faces) {
        const u32 base = static_cast<u32>(m.vertices.size());
        const vec3 c = f.n * vec3{h.x, h.y, h.z};
        const vec3 tu = f.t * (std::fabs(f.t.x) * h.x + std::fabs(f.t.y) * h.y + std::fabs(f.t.z) * h.z);
        const vec3 tv = f.b * (std::fabs(f.b.x) * h.x + std::fabs(f.b.y) * h.y + std::fabs(f.b.z) * h.z);
        const vec2 uvs[4] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
        const vec3 ps[4] = {c - tu - tv, c + tu - tv, c + tu + tv, c - tu + tv};
        for (int i = 0; i < 4; ++i) m.vertices.push_back(make_vertex(ps[i], f.n, uvs[i], f.t, 1.0f));
        m.indices.insert(m.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
    return m;
}

void compute_tangents(MeshData& mesh) {
    const usize n = mesh.vertices.size();
    std::vector<vec3> tan(n), bitan(n);
    for (usize i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const u32 ia = mesh.indices[i], ib = mesh.indices[i + 1], ic = mesh.indices[i + 2];
        const Vertex &a = mesh.vertices[ia], &b = mesh.vertices[ib], &c = mesh.vertices[ic];
        const vec3 pa{a.pos[0], a.pos[1], a.pos[2]}, pb{b.pos[0], b.pos[1], b.pos[2]}, pc{c.pos[0], c.pos[1], c.pos[2]};
        const vec3 e1 = pb - pa, e2 = pc - pa;
        const f32 du1 = b.uv[0] - a.uv[0], dv1 = b.uv[1] - a.uv[1];
        const f32 du2 = c.uv[0] - a.uv[0], dv2 = c.uv[1] - a.uv[1];
        const f32 det = du1 * dv2 - du2 * dv1;
        if (std::fabs(det) < 1e-12f) continue;
        const f32 r = 1.0f / det;
        const vec3 t = (e1 * dv2 - e2 * dv1) * r;
        // Bitangent points towards decreasing v (up in the image), the glTF / MikkTSpace convention
        // for normal maps with +Y = green up.
        const vec3 bt = (e1 * du2 - e2 * du1) * r;
        for (u32 idx : {ia, ib, ic}) {
            tan[idx] += t;
            bitan[idx] += bt;
        }
    }
    for (usize i = 0; i < n; ++i) {
        Vertex& v = mesh.vertices[i];
        const vec3 nrm = oct_decode(v.normal);
        vec3 t = tan[i] - nrm * dot(nrm, tan[i]);
        if (length(t) < 1e-8f) {
            // Arbitrary tangent perpendicular to the normal.
            t = std::fabs(nrm.y) < 0.99f ? cross(vec3{0, 1, 0}, nrm) : cross(vec3{1, 0, 0}, nrm);
        }
        t = normalize(t);
        v.tangent = oct_encode(t);
        v.tangent_sign = dot(cross(nrm, t), bitan[i]) < 0.0f ? -1.0f : 1.0f;
    }
}

Bounds compute_bounds(const MeshData& mesh) {
    if (mesh.vertices.empty()) return {{}, 0.0f};
    vec3 lo{1e30f}, hi{-1e30f};
    for (const Vertex& v : mesh.vertices) {
        lo = {min(lo.x, v.pos[0]), min(lo.y, v.pos[1]), min(lo.z, v.pos[2])};
        hi = {max(hi.x, v.pos[0]), max(hi.y, v.pos[1]), max(hi.z, v.pos[2])};
    }
    const vec3 c = (lo + hi) * 0.5f;
    f32 r = 0.0f;
    for (const Vertex& v : mesh.vertices) r = max(r, length(vec3{v.pos[0], v.pos[1], v.pos[2]} - c));
    return {c, r};
}

} // namespace tefra::render
