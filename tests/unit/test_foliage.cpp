#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest.h>

#include "tefra/core/image.h"
#include "tefra/renderer/vegetation.h"

#include <cmath>
#include <cstdlib>

using namespace tefra;

namespace {

// Coverage (alpha) and mean alpha over one 512x256 tile of the 1024x512 atlas.
f32 tile_coverage(const std::vector<u8>& albedo, u32 tile) {
    const u32 ox = (tile % 2) * 512, oy = (tile / 2) * 256;
    f64 sum = 0.0;
    for (u32 y = 0; y < 256; ++y)
        for (u32 x = 0; x < 512; ++x) sum += albedo[(usize(oy + y) * 1024 + ox + x) * 4 + 3];
    return static_cast<f32>(sum / (512.0 * 256.0 * 255.0));
}

} // namespace

TEST_CASE("needle atlas: coverage, normals and depth AO are in range") {
    std::vector<u8> albedo, normal;
    render::make_needle_textures(7, 1024, 512, albedo, normal);
    REQUIRE(albedo.size() == 1024u * 512u * 4u);
    REQUIRE(normal.size() == albedo.size());

    // Living twigs cover a useful share of their tile; the dead twig is sparser.
    for (u32 t = 0; t < 3; ++t) {
        const f32 c = tile_coverage(albedo, t);
        CHECK(c > 0.12f);
        CHECK(c < 0.6f);
    }
    CHECK(tile_coverage(albedo, 3) < tile_coverage(albedo, 0));

    // Where a needle is hit, the baked normal is unit length (after decoding) and faces the viewer.
    u32 hits = 0;
    for (usize i = 0; i < albedo.size(); i += 4) {
        if (albedo[i + 3] < 200) continue;
        ++hits;
        const f32 nx = normal[i + 0] / 127.5f - 1.0f, ny = normal[i + 1] / 127.5f - 1.0f,
                  nz = normal[i + 2] / 127.5f - 1.0f;
        CHECK(std::fabs(std::sqrt(nx * nx + ny * ny + nz * nz) - 1.0f) < 0.02f);
        CHECK(nz > -0.01f);
        const f32 ao = normal[i + 3] / 255.0f;
        CHECK(ao >= 0.25f);
        CHECK(ao <= 1.0f);
    }
    CHECK(hits > 10000u);
    // Atlas tiles are not empty: the top-left (living) tile has needles.
    CHECK(tile_coverage(albedo, 0) > 0.0f);

    if (const char* dump = std::getenv("TEFRA_ATLAS_DUMP")) {
        ImageRGBA8 img;
        img.width = 1024;
        img.height = 512;
        img.pixels = albedo;
        CHECK(image_write_png(dump, img));
    }
}

TEST_CASE("conifer LOD0 stays under the triangle budget") {
    const struct {
        u32 seed;
        f32 h, r;
    } variants[3] = {{11, 30.0f, 0.55f}, {23, 24.0f, 0.42f}, {37, 36.0f, 0.68f}};
    for (const auto& v : variants) {
        const render::ConiferMeshes cm = render::make_conifer(v.seed, v.h, v.r);
        const usize tris = (cm.trunk_lod0.indices.size() + cm.crown_lod0.indices.size()) / 3;
        MESSAGE("conifer LOD0 tris: ", tris);
        CHECK(tris < 4500u); // trunk ~1.5k (buttresses, roots) + crown
        CHECK(tris > 800u);
    }
}
