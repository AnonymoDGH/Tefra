#include "common/gpu_types.glsl"
#include "common/bindless.glsl"
#include "common/terrain.glsl"
layout(location = 0) out vec3 v_pos;   // camera-relative
layout(location = 1) out vec2 v_world; // world xz
void main() {
    Frame f = pc.frame.f;
    TerrainData t = pc.terrain.t;
    vec4 g = pc.grid.v[gl_VertexIndex];
    vec4 p = pc.patches.v[gl_InstanceIndex];
    vec2 rel = p.xy + g.xy * p.z;
    vec2 world = rel + f.camera_pos.xz;
    float h = terrain_height(t, world) - g.z * p.z * 0.08; // skirts hide cracks between LOD rings
    v_pos = vec3(rel.x, h - f.camera_pos.y, rel.y);
    v_world = world;
#ifdef SHADOW_PASS
    gl_Position = f.cascade_view_proj[pc.cascade] * vec4(v_pos, 1.0);
#else
    gl_Position = f.view_proj * vec4(v_pos, 1.0);
#endif
}
