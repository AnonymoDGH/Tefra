#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest.h>

#include "cgltf.h"
#include "tefra/core/fs.h"
#include "tefra/core/image.h"
#include "tefra/renderer/model_import.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using namespace tefra;
using namespace tefra::render;

namespace {

std::string scratch_dir() {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "tefra_model_import_test";
    std::filesystem::create_directories(dir);
    return dir.string();
}

void write_text(const std::string& path, const std::string& text) {
    CHECK(file_write_all(path.c_str(), text.data(), text.size()));
}

void write_bytes(const std::string& path, const std::vector<u8>& bytes) {
    CHECK(file_write_all(path.c_str(), bytes.data(), bytes.size()));
}

// What a loader sees in the converted file: triangles, attributes present, and bounds in world space (metres).
struct GlbInfo {
    bool ok = false;
    u32 primitives = 0;
    u64 triangles = 0;
    bool normals = false;
    bool uvs = false;
    bool colors = false;
    f32 mn[3] = {0, 0, 0};
    f32 mx[3] = {0, 0, 0};
};

GlbInfo inspect_glb(const std::string& path) {
    GlbInfo info;
    cgltf_options opt{};
    cgltf_data* data = nullptr;
    if (cgltf_parse_file(&opt, path.c_str(), &data) != cgltf_result_success) return info;
    if (cgltf_load_buffers(&opt, data, path.c_str()) != cgltf_result_success) {
        cgltf_free(data);
        return info;
    }
    info.ok = true;
    bool first = true;
    for (usize ni = 0; ni < data->nodes_count; ++ni) {
        const cgltf_node* node = &data->nodes[ni];
        if (!node->mesh) continue;
        cgltf_float world[16];
        cgltf_node_transform_world(node, world);
        for (usize pi = 0; pi < node->mesh->primitives_count; ++pi) {
            const cgltf_primitive& prim = node->mesh->primitives[pi];
            ++info.primitives;
            if (prim.indices) info.triangles += prim.indices->count / 3;
            for (usize ai = 0; ai < prim.attributes_count; ++ai) {
                const cgltf_attribute& a = prim.attributes[ai];
                if (a.type == cgltf_attribute_type_normal) info.normals = true;
                if (a.type == cgltf_attribute_type_texcoord) info.uvs = true;
                if (a.type == cgltf_attribute_type_color) info.colors = true;
                if (a.type != cgltf_attribute_type_position || !a.data) continue;
                const cgltf_accessor* acc = a.data;
                // Transform the 8 corners of the local box by the node's world matrix.
                for (int corner = 0; corner < 8; ++corner) {
                    const f32 local[3] = {(corner & 1) ? acc->max[0] : acc->min[0],
                                          (corner & 2) ? acc->max[1] : acc->min[1],
                                          (corner & 4) ? acc->max[2] : acc->min[2]};
                    f32 w[3];
                    for (int r = 0; r < 3; ++r)
                        w[r] = world[0 * 4 + r] * local[0] + world[1 * 4 + r] * local[1] + world[2 * 4 + r] * local[2] +
                               world[3 * 4 + r];
                    for (int r = 0; r < 3; ++r) {
                        if (first) {
                            info.mn[r] = info.mx[r] = w[r];
                        } else {
                            info.mn[r] = std::min(info.mn[r], w[r]);
                            info.mx[r] = std::max(info.mx[r], w[r]);
                        }
                    }
                    first = false;
                }
            }
        }
    }
    cgltf_free(data);
    return info;
}

bool near(f32 a, f32 b, f32 eps = 1e-4f) { return std::fabs(a - b) < eps; }

// Unit cube made of 12 triangles with normals and UVs (OBJ, 1 unit = 1 m).
const char* kCubeObj = R"(# cube
o Cube
v 0 0 0
v 1 0 0
v 1 1 0
v 0 1 0
v 0 0 1
v 1 0 1
v 1 1 1
v 0 1 1
vt 0 0
vt 1 0
vt 1 1
vt 0 1
vn 0 0 -1
vn 0 0 1
vn 0 -1 0
vn 0 1 0
vn -1 0 0
vn 1 0 0
f 1/1/1 4/4/1 3/3/1 2/2/1
f 5/1/2 6/2/2 7/3/2 8/4/2
f 1/1/3 2/2/3 6/3/3 5/4/3
f 3/1/4 4/2/4 8/3/4 7/4/4
f 1/1/5 5/2/5 8/3/5 4/4/5
f 2/1/6 3/2/6 7/3/6 6/4/6
)";

// A 2 x 2 m square in the XZ plane, Z-up (a CAD file): 2 triangles, ASCII STL.
const char* kSquareStlAscii = R"(solid square
  facet normal 0 0 1
    outer loop
      vertex 0 0 0
      vertex 2 0 0
      vertex 2 2 0
    endloop
  endfacet
  facet normal 0 0 1
    outer loop
      vertex 0 0 0
      vertex 2 2 0
      vertex 0 2 0
    endloop
  endfacet
endsolid square
)";

// PLY quad (2 triangles) with normals, UVs and uchar colors. ASCII.
const char* kQuadPlyAscii = R"(ply
format ascii 1.0
comment a quad
element vertex 4
property float x
property float y
property float z
property float nx
property float ny
property float nz
property float s
property float t
property uchar red
property uchar green
property uchar blue
element face 1
property list uchar int vertex_indices
end_header
0 0 0 0 0 1 0 0 255 0 0
1 0 0 0 0 1 1 0 0 255 0
1 1 0 0 0 1 1 1 0 0 255
0 1 0 0 0 1 0 1 255 255 0
4 0 1 2 3
)";

// A 10 x 10 grid of quads (200 triangles) in the XY plane, for the simplification test (OBJ).
std::string grid_obj(int n) {
    std::string s;
    for (int y = 0; y <= n; ++y)
        for (int x = 0; x <= n; ++x) s += "v " + std::to_string(x) + " " + std::to_string(y) + " 0\n";
    auto id = [n](int x, int y) { return y * (n + 1) + x + 1; };
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x)
            s += "f " + std::to_string(id(x, y)) + " " + std::to_string(id(x + 1, y)) + " " +
                 std::to_string(id(x + 1, y + 1)) + " " + std::to_string(id(x, y + 1)) + "\n";
    return s;
}

void put_f32(std::vector<u8>& out, f32 v) {
    u8 b[4];
    std::memcpy(b, &v, 4);
    out.insert(out.end(), b, b + 4);
}

void put_u32(std::vector<u8>& out, u32 v) {
    for (int k = 0; k < 4; ++k) out.push_back(static_cast<u8>((v >> (8 * k)) & 0xffu));
}

// Binary STL of one triangle (in the XY plane, Z-up), 2 m wide, with a face normal of +Z.
std::vector<u8> binary_stl_triangle() {
    std::vector<u8> out(80, 0);
    put_u32(out, 1);
    const f32 rec[12] = {0, 0, 1, 0, 0, 0, 2, 0, 0, 0, 2, 0};
    for (f32 v : rec) put_f32(out, v);
    out.push_back(0);
    out.push_back(0);
    return out;
}

// Binary PLY of one triangle (little endian, XY plane) with per-vertex normals.
std::vector<u8> binary_ply_triangle() {
    const std::string header = "ply\nformat binary_little_endian 1.0\nelement vertex 3\nproperty float x\n"
                               "property float y\nproperty float z\nproperty float nx\nproperty float ny\n"
                               "property float nz\nelement face 1\nproperty list uchar uint vertex_indices\n"
                               "end_header\n";
    std::vector<u8> out(header.begin(), header.end());
    const f32 verts[18] = {0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 1};
    for (f32 v : verts) put_f32(out, v);
    out.push_back(3);
    put_u32(out, 0);
    put_u32(out, 1);
    put_u32(out, 2);
    return out;
}

} // namespace

TEST_CASE("model import: format detection") {
    CHECK(model_import_format("a/b/chair.FBX") == "fbx");
    CHECK(model_import_format("cube.obj") == "obj");
    CHECK(model_import_format("part.stl") == "stl");
    CHECK(model_import_format("scan.ply") == "ply");
    CHECK(model_import_format("model.glb").empty());
    CHECK(model_import_format("model.dae").empty());
}

TEST_CASE("model import: OBJ with MTL-less cube converts to a metre glb") {
    const std::string dir = scratch_dir();
    const std::string src = dir + "/cube.obj";
    const std::string dst = dir + "/cube_out.glb";
    write_text(src, kCubeObj);
    ModelImportOptions opt;
    ModelImportReport rep;
    std::string err;
    REQUIRE(convert_to_glb(src.c_str(), dst.c_str(), opt, &err, &rep));
    CHECK(rep.format == "obj");
    CHECK(rep.triangles_out == 12);
    const GlbInfo info = inspect_glb(dst);
    REQUIRE(info.ok);
    CHECK(info.triangles == 12);
    CHECK(info.normals);
    CHECK(info.uvs);
    CHECK(near(info.mn[0], 0.0f));
    CHECK(near(info.mx[0], 1.0f));
    CHECK(near(info.mx[1], 1.0f));
    CHECK(near(info.mx[2], 1.0f));
}

TEST_CASE("model import: OBJ unit_scale converts centimetres to metres") {
    const std::string dir = scratch_dir();
    const std::string src = dir + "/cube_cm.obj";
    const std::string dst = dir + "/cube_cm.glb";
    write_text(src, kCubeObj);
    ModelImportOptions opt;
    opt.unit_scale = 0.01f; // the file is in centimetres: a 100 cm cube
    std::string err;
    REQUIRE(convert_to_glb(src.c_str(), dst.c_str(), opt, &err, nullptr));
    // The unit cube read as centimetres is 1 cm; scaled to metres it is 0.01 m across.
    const GlbInfo info = inspect_glb(dst);
    REQUIRE(info.ok);
    CHECK(near(info.mx[0] - info.mn[0], 0.01f));
}

// ASCII FBX: a 100-unit cube (8 vertices, 6 quads) moved 100 units along X. UnitScaleFactor 1 means one unit is one
// centimetre (the FBX default), so the glb must hold a 1 m cube centred at x = 1 m.
const char* kCubeFbxCm = R"(FBXHeaderExtension:  {
	FBXHeaderVersion: 1003
	FBXVersion: 7400
}
GlobalSettings:  {
	Version: 1000
	Properties70:  {
		P: "UpAxis", "int", "Integer", "",1
		P: "UpAxisSign", "int", "Integer", "",1
		P: "FrontAxis", "int", "Integer", "",2
		P: "FrontAxisSign", "int", "Integer", "",1
		P: "CoordAxis", "int", "Integer", "",0
		P: "CoordAxisSign", "int", "Integer", "",1
		P: "UnitScaleFactor", "double", "Number", "",1
	}
}
Objects:  {
	Geometry: 1001, "Geometry::Cube", "Mesh" {
		Vertices: *24 {
			a: -50,-50,-50, 50,-50,-50, 50,50,-50, -50,50,-50, -50,-50,50, 50,-50,50, 50,50,50, -50,50,50
		}
		PolygonVertexIndex: *24 {
			a: 0,1,2,-4, 4,7,6,-6, 0,4,5,-2, 1,5,6,-3, 2,6,7,-4, 3,7,4,-1
		}
		GeometryVersion: 124
	}
	Model: 2001, "Model::Cube", "Mesh" {
		Version: 232
		Properties70:  {
			P: "Lcl Translation", "Lcl Translation", "", "A",100,0,0
		}
		Shading: T
		Culling: "CullingOff"
	}
}
Connections:  {
	C: "OO",2001,0
	C: "OO",1001,2001
}
)";

TEST_CASE("model import: ASCII FBX in centimetres becomes metres") {
    const std::string dir = scratch_dir();
    const std::string src = dir + "/cube_cm.fbx";
    const std::string dst = dir + "/cube_fbx.glb";
    write_text(src, kCubeFbxCm);
    ModelImportOptions opt;
    ModelImportReport rep;
    std::string err;
    REQUIRE(convert_to_glb(src.c_str(), dst.c_str(), opt, &err, &rep));
    CHECK(rep.format == "fbx");
    const GlbInfo info = inspect_glb(dst);
    REQUIRE(info.ok);
    CHECK(info.triangles == 12);
    CHECK(info.normals);
    CHECK(near(info.mx[0] - info.mn[0], 1.0f, 1e-3f));
    CHECK(near((info.mx[0] + info.mn[0]) * 0.5f, 1.0f, 1e-3f)); // translated 100 cm = 1 m
    CHECK(near(info.mx[1] - info.mn[1], 1.0f, 1e-3f));
}

TEST_CASE("model import: ASCII STL (Z-up) becomes +Y up in metres with flat normals") {
    const std::string dir = scratch_dir();
    const std::string src = dir + "/square.stl";
    const std::string dst = dir + "/square.glb";
    write_text(src, kSquareStlAscii);
    ModelImportOptions opt;
    ModelImportReport rep;
    std::string err;
    REQUIRE(convert_to_glb(src.c_str(), dst.c_str(), opt, &err, &rep));
    CHECK(rep.format == "stl");
    const GlbInfo info = inspect_glb(dst);
    REQUIRE(info.ok);
    CHECK(info.triangles == 2);
    CHECK(info.normals);
    // Z-up source: (x, y, z) -> (x, z, -y). The square lies in the file's XY plane (z = 0), so in the glb it lies in
    // the XZ plane: X 0..2 and Z -2..0, with Y flat at 0.
    CHECK(near(info.mx[0], 2.0f));
    CHECK(near(info.mx[1], 0.0f));
    CHECK(near(info.mn[2], -2.0f));
    CHECK(near(info.mx[2], 0.0f));
}

TEST_CASE("model import: binary STL with an explicit Y-up source") {
    const std::string dir = scratch_dir();
    const std::string src = dir + "/tri_bin.stl";
    const std::string dst = dir + "/tri_bin.glb";
    write_bytes(src, binary_stl_triangle());
    ModelImportOptions opt;
    opt.source_up = ModelUpAxis::Y; // the file's Y is taken as up: no rotation, the triangle stays in XY
    std::string err;
    REQUIRE(convert_to_glb(src.c_str(), dst.c_str(), opt, &err, nullptr));
    const GlbInfo info = inspect_glb(dst);
    REQUIRE(info.ok);
    CHECK(info.triangles == 1);
    CHECK(near(info.mx[0], 2.0f));
    CHECK(near(info.mx[1], 2.0f));
}

TEST_CASE("model import: ASCII PLY quad with normals, UVs and colors") {
    const std::string dir = scratch_dir();
    const std::string src = dir + "/quad.ply";
    const std::string dst = dir + "/quad.glb";
    write_text(src, kQuadPlyAscii);
    ModelImportOptions opt;
    ModelImportReport rep;
    std::string err;
    REQUIRE(convert_to_glb(src.c_str(), dst.c_str(), opt, &err, &rep));
    CHECK(rep.format == "ply");
    const GlbInfo info = inspect_glb(dst);
    REQUIRE(info.ok);
    CHECK(info.triangles == 2);
    CHECK(info.normals);
    CHECK(info.uvs);
    CHECK(info.colors);
    CHECK(near(info.mx[0], 1.0f));
    // PLY is Z-up by default: the quad (z = 0) lies in the glb's XZ plane, its Y (0..1 m) becoming -Z.
    CHECK(near(info.mx[1], 0.0f));
    CHECK(near(info.mn[2], -1.0f));
}

TEST_CASE("model import: binary little endian PLY") {
    const std::string dir = scratch_dir();
    const std::string src = dir + "/tri.ply";
    const std::string dst = dir + "/tri_ply.glb";
    write_bytes(src, binary_ply_triangle());
    ModelImportOptions opt;
    std::string err;
    REQUIRE(convert_to_glb(src.c_str(), dst.c_str(), opt, &err, nullptr));
    const GlbInfo info = inspect_glb(dst);
    REQUIRE(info.ok);
    CHECK(info.triangles == 1);
    CHECK(info.normals);
}

TEST_CASE("model import: max_triangles simplifies a heavy primitive") {
    const std::string dir = scratch_dir();
    const std::string src = dir + "/grid.obj";
    const std::string dst = dir + "/grid.glb";
    write_text(src, grid_obj(10)); // 200 triangles
    ModelImportOptions opt;
    opt.max_triangles = 40;
    ModelImportReport rep;
    std::string err;
    REQUIRE(convert_to_glb(src.c_str(), dst.c_str(), opt, &err, &rep));
    CHECK(rep.triangles_in == 200);
    CHECK(rep.triangles_out <= 40);
    CHECK(rep.triangles_out > 0);
    const GlbInfo info = inspect_glb(dst);
    REQUIRE(info.ok);
    CHECK(info.triangles <= 40);
}

TEST_CASE("model import: OBJ + MTL textures are embedded, or written beside the glb") {
    const std::string dir = scratch_dir();
    ImageRGBA8 img;
    img.width = 4;
    img.height = 4;
    img.pixels.assign(4 * 4 * 4, 200);
    REQUIRE(image_write_png((dir + "/tex.png").c_str(), img));
    write_text(dir + "/mat.mtl", "newmtl red\nKd 1 0 0\nmap_Kd tex.png\n");
    std::string obj = std::string("mtllib mat.mtl\nusemtl red\n") + kCubeObj;
    write_text(dir + "/textured.obj", obj);

    // Embedded (default): one image in the BIN chunk, referenced by the base color texture.
    {
        const std::string dst = dir + "/tex_embedded.glb";
        ModelImportOptions opt;
        ModelImportReport rep;
        std::string err;
        REQUIRE(convert_to_glb((dir + "/textured.obj").c_str(), dst.c_str(), opt, &err, &rep));
        CHECK(rep.textures == 1);
        CHECK(rep.materials == 1);
        cgltf_options co{};
        cgltf_data* data = nullptr;
        REQUIRE(cgltf_parse_file(&co, dst.c_str(), &data) == cgltf_result_success);
        REQUIRE(cgltf_load_buffers(&co, data, dst.c_str()) == cgltf_result_success);
        REQUIRE(data->images_count == 1);
        CHECK(data->images[0].buffer_view != nullptr);
        CHECK(std::string(data->images[0].mime_type) == "image/png");
        REQUIRE(data->materials_count == 1);
        const cgltf_material& m = data->materials[0];
        REQUIRE(m.has_pbr_metallic_roughness);
        CHECK(m.pbr_metallic_roughness.base_color_texture.texture != nullptr);
        CHECK(near(m.pbr_metallic_roughness.base_color_factor[0], 1.0f));
        CHECK(near(m.pbr_metallic_roughness.base_color_factor[1], 0.0f));
        cgltf_free(data);
    }
    // Not embedded: the texture is written next to the glb and referenced by a relative URI.
    {
        const std::string dst = dir + "/tex_external.glb";
        ModelImportOptions opt;
        opt.embed_textures = false;
        std::string err;
        REQUIRE(convert_to_glb((dir + "/textured.obj").c_str(), dst.c_str(), opt, &err, nullptr));
        const std::string written = dir + "/tex_external_tex0.png";
        CHECK(file_exists(written.c_str()));
        cgltf_options co{};
        cgltf_data* data = nullptr;
        REQUIRE(cgltf_parse_file(&co, dst.c_str(), &data) == cgltf_result_success);
        REQUIRE(data->images_count == 1);
        CHECK(std::string(data->images[0].uri) == "tex_external_tex0.png");
        cgltf_free(data);
    }
}

TEST_CASE("model import: unsupported or missing inputs fail with a message") {
    const std::string dir = scratch_dir();
    std::string err;
    CHECK_FALSE(
        convert_to_glb((dir + "/x.dae").c_str(), (dir + "/x.glb").c_str(), ModelImportOptions{}, &err, nullptr));
    CHECK(err.find("unsupported") != std::string::npos);
    err.clear();
    CHECK_FALSE(
        convert_to_glb((dir + "/missing.obj").c_str(), (dir + "/m.glb").c_str(), ModelImportOptions{}, &err, nullptr));
    CHECK_FALSE(err.empty());
}
