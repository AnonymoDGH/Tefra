#include "tefra/renderer/skin.h"

namespace tefra::render {

Vertex skin_vertex_reference(const Vertex& v, const SkinAttr& s, const std::vector<mat4>& palette) {
    vec4 p{0.0f, 0.0f, 0.0f, 0.0f};
    vec3 n{}, t{};
    f32 wsum = 0.0f;
    const vec3 bind_n = oct_decode(v.normal);
    const vec3 bind_t = oct_decode(v.tangent);
    const vec3 bind_p{v.pos[0], v.pos[1], v.pos[2]};
    for (int k = 0; k < 4; ++k) {
        const f32 w = s.weights[k];
        if (w <= 0.0f) continue;
        const mat4& m = palette[s.joints[k]];
        p = p + (m * vec4(bind_p, 1.0f)) * w;
        n = n + transform_dir(m, bind_n) * w;
        t = t + transform_dir(m, bind_t) * w;
        wsum += w;
    }
    Vertex out = v;
    if (wsum <= 0.0f) return out;
    out.pos[0] = p.x;
    out.pos[1] = p.y;
    out.pos[2] = p.z;
    out.normal = oct_encode(length(n) > 1e-12f ? normalize(n) : bind_n);
    out.tangent = oct_encode(length(t) > 1e-12f ? normalize(t) : bind_t);
    return out;
}

MeshData skin_mesh_reference(const MeshData& mesh, const std::vector<mat4>& palette) {
    MeshData out = mesh;
    for (usize i = 0; i < mesh.vertices.size() && i < mesh.skin.size(); ++i)
        out.vertices[i] = skin_vertex_reference(mesh.vertices[i], mesh.skin[i], palette);
    return out;
}

} // namespace tefra::render
