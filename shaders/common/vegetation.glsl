#ifndef TEFRA_VEGETATION_GLSL
#define TEFRA_VEGETATION_GLSL
struct VegInstance { vec4 pos_scale; vec4 rot_proto; };
struct VegProto { vec4 bounds; vec4 lod_dist; uint draw00; uint draw01; uint draw10; uint draw11; float wind; uint base; uint count; uint casts; };
struct VegDraw { uint first_index; uint index_count; int vertex_offset; uint material; uint visible_base; uint proto; uint lod; uint submesh; };
struct DrawCmd { uint index_count; uint instance_count; uint first_index; int vertex_offset; uint first_instance; };
layout(buffer_reference, std430, buffer_reference_align = 16) readonly buffer VegInstanceBuffer { VegInstance i[]; };
layout(buffer_reference, std430, buffer_reference_align = 16) readonly buffer VegProtoBuffer { VegProto p[]; };
layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer VegDrawBuffer { VegDraw d[]; };
layout(buffer_reference, std430, buffer_reference_align = 4) buffer DrawCmdBuffer { DrawCmd c[]; };
layout(buffer_reference, std430, buffer_reference_align = 4) buffer IndexListBuffer { uint v[]; };
#endif
