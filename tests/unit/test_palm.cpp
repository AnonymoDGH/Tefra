#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest.h>

#include "tefra/core/image.h"
#include "tefra/renderer/palm.h"

#include <cmath>
#include <cstdlib>

using namespace tefra;

namespace {

// Coverage (alpha) of one 512x256 tile of the 1024x512 frond atlas.
f32 tile_coverage(const std::vector<u8>& albedo, u32 tile) {
    const u32 ox = (tile % 2) * 512, oy = (tile / 2) * 256;
    f64 sum = 0.0;
    for (u32 y = 0; y < 256; ++y)
        for (u32 x = 0; x < 512; ++x) sum += albedo[(usize(oy + y) * 1024 + ox + x) * 4 + 3];
    return static_cast<f32>(sum / (512.0 * 256.0 * 255.0));
}

} // namespace

TEST_CASE("palm LOD budgets: LOD0 < 3000 triangles, LOD1 < 600") {
    for (u32 seed : {3u, 17u, 42u}) {
        for (f32 h : {9.0f, 12.0f, 15.0f}) {
            const render::PalmMeshes pm = render::make_palm(seed, h);
            const usize lod0 = (pm.trunk_lod0.indices.size() + pm.crown_lod0.indices.size()) / 3;
            const usize lod1 = (pm.trunk_lod1.indices.size() + pm.crown_lod1.indices.size()) / 3;
            MESSAGE("palm seed ", seed, " h ", h, ": LOD0 tris ", lod0, " LOD1 tris ", lod1);
            CHECK(lod0 < 3000u);
            CHECK(lod0 > 1000u);
            CHECK(lod1 < 600u);
            CHECK(lod1 > 100u);
            CHECK(pm.crown_radius > 2.5f);
            CHECK(pm.crown_radius < 7.0f);
            CHECK(pm.canopy_radius > 0.0f);
            CHECK(pm.canopy_radius <= pm.crown_radius);
        }
    }
}

TEST_CASE("palm trunk leans with a 5-15 deg top tangent and no NaNs") {
    const f32 h = 12.0f;
    for (u32 seed : {3u, 17u, 42u}) {
        const render::PalmMeshes pm = render::make_palm(seed, h);
        // Average position of the top ring: the axis offset at ~98.5 % height (quadratic bend).
        f64 sx = 0.0, sz = 0.0;
        u32 n = 0;
        for (const render::Vertex& v : pm.trunk_lod0.vertices)
            if (v.pos[1] > h * 0.975f && v.pos[1] < h * 0.99f) {
                sx += v.pos[0];
                sz += v.pos[2];
                ++n;
            }
        REQUIRE(n > 0u);
        const f32 ox = static_cast<f32>(sx / n), oz = static_cast<f32>(sz / n);
        const f32 off = std::sqrt(ox * ox + oz * oz) / h;
        // Offset ratio = tan(tilt) / 2 at the top: tilt 5-15 deg gives 0.044-0.134.
        CHECK(off > 0.035f);
        CHECK(off < 0.15f);
        for (const render::Vertex& v : pm.crown_lod0.vertices) {
            CHECK(std::isfinite(v.pos[0]));
            CHECK(std::isfinite(v.pos[1]));
            CHECK(std::isfinite(v.pos[2]));
        }
    }
}

TEST_CASE("frond atlas: coverage, normals and AO are in range") {
    std::vector<u8> albedo, normal;
    render::make_frond_textures(5, 1024, 512, albedo, normal);
    REQUIRE(albedo.size() == 1024u * 512u * 4u);
    REQUIRE(normal.size() == albedo.size());

    // Living leaflet tiles cover a useful share of their tile; the dead tile is sparser.
    for (u32 t = 0; t < 3; ++t) {
        const f32 c = tile_coverage(albedo, t);
        MESSAGE("frond tile ", t, " coverage ", c);
        CHECK(c > 0.12f);
        CHECK(c < 0.6f);
    }
    CHECK(tile_coverage(albedo, 3) < tile_coverage(albedo, 0));
    CHECK(tile_coverage(albedo, 3) > 0.03f);

    // Covered texels carry unit normals facing the viewer, and AO in range.
    u32 hits = 0;
    for (usize i = 0; i < albedo.size(); i += 4) {
        if (albedo[i + 3] < 200) continue;
        ++hits;
        const f32 nx = normal[i + 0] / 127.5f - 1.0f, ny = normal[i + 1] / 127.5f - 1.0f,
                  nz = normal[i + 2] / 127.5f - 1.0f;
        CHECK(std::fabs(std::sqrt(nx * nx + ny * ny + nz * nz) - 1.0f) < 0.02f);
        CHECK(nz > 0.0f);
        const f32 ao = normal[i + 3] / 255.0f;
        CHECK(ao >= 0.25f);
        CHECK(ao <= 1.0f);
    }
    CHECK(hits > 10000u);

    if (const char* dump = std::getenv("TEFRA_FROND_DUMP")) {
        ImageRGBA8 img;
        img.width = 1024;
        img.height = 512;
        img.pixels = albedo;
        CHECK(image_write_png(dump, img));
    }
}
