#include "tefra/world/environment_sync.h"

#include "tefra/core/fs.h"
#include "tefra/core/image.h"
#include "tefra/core/log.h"
#include "tefra/renderer/gltf.h"
#include "tefra/renderer/ocean.h"
#include "tefra/renderer/palm.h"
#include "tefra/renderer/renderer.h"
#include "tefra/renderer/scene.h"
#include "tefra/renderer/terrain.h"
#include "tefra/renderer/vegetation.h"
#include "tefra/renderer/water.h"
#include "tefra/rhi/bindless.h"
#include "tefra/rhi/device.h"
#include "tefra/rhi/upload.h"

#include <bit>
#include <cmath>
#include <iterator>
#include <string>

namespace tefra::world {

namespace {

render::MaterialGPU plain(render::Scene& scene, vec3 color, f32 roughness, f32 metallic) {
    render::MaterialGPU m{};
    m.base_color = vec4(color, 1.0f);
    m.roughness = roughness;
    m.metallic = metallic;
    m.base_color_tex = scene.white_texture();
    m.normal_tex = scene.flat_normal_texture();
    m.orm_tex = scene.white_texture();
    m.emissive_tex = render::kNoTexture;
    m.occlusion_strength = 0.0f;
    return m;
}

u32 load_texture_file(render::Scene& scene, const std::string& path, bool srgb, u32 fallback) {
    ImageRGBA8 img;
    if (!image_read(path.c_str(), img)) return fallback;
    rhi::TextureUpload up;
    up.pixels = img.pixels.data();
    up.width = img.width;
    up.height = img.height;
    up.format = srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
    up.name = path.c_str();
    return scene.add_texture(rhi::upload_texture(scene.device(), up));
}

struct LoadedModel {
    u32 mesh[2] = {~0u, ~0u};
    u32 material[2] = {0, 0};
    f32 radius = 1.0f, center_y = 0.0f;
    std::vector<std::pair<u32, u32>> lod1; // (mesh, coarser LOD) when requested
};

LoadedModel load_model(render::Scene& scene, const std::string& path, u32 max_triangles = 0, u32 lod1_triangles = 0) {
    LoadedModel m;
    const usize first = scene.instances().size();
    render::GltfLoadOptions opt;
    opt.max_triangles = max_triangles;
    opt.lod1_triangles = lod1_triangles;
    const render::GltfLoadResult r = render::load_gltf(scene, path.c_str(), opt);
    if (!r.ok) return m;
    m.lod1 = r.lod1;
    f32 top = 0.0f;
    for (usize i = first; i < scene.instances().size() && i - first < 2; ++i) {
        m.mesh[i - first] = scene.instances()[i].mesh;
        m.material[i - first] = scene.instances()[i].material;
        const auto& b = scene.meshes()[scene.instances()[i].mesh].bounds;
        m.radius = max(m.radius, b.radius);
        top = max(top, b.center.y);
    }
    m.center_y = top;
    scene.instances().resize(first);
    return m;
}

u32 hash_u32(u32 x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

f32 smoothstep(f32 a, f32 b, f32 x) {
    const f32 t = saturate((x - a) / (b - a));
    return t * t * (3.0f - 2.0f * t);
}

f32 hash01(u32 a, u32 b, u32 c) {
    return static_cast<f32>(hash_u32(a * 73856093u ^ b * 19349663u ^ c * 83492791u) >> 8) / 16777216.0f;
}

// Undergrowth models (Poly Haven CC0): name, draw distance (m), scatter spacing (m).
struct UnderSpec {
    const char* name;
    f32 dist;
    f32 spacing;
};
constexpr UnderSpec kUnder[4] = {{"fern_02", 70.0f, 2.2f},
                                 {"grass_medium_02", 45.0f, 1.1f},
                                 {"rock_moss_set_01", 160.0f, 9.0f},
                                 {"tree_stump_01", 160.0f, 21.0f}};

// Shore rocks (Poly Haven CC0): name, draw distance (m). Reef rocks at the waterline, pebble clusters on the sand,
// big boulders at the foot of the headlands.
struct RockSpec {
    const char* name;
    f32 dist;
};
constexpr RockSpec kRocks[3] = {
    {"coast_rocks_05", 450.0f}, {"sand_rocks_small_01", 140.0f}, {"coast_land_rocks_02", 900.0f}};

} // namespace

void build_valley_assets(render::Scene& scene, LandscapeAssets& assets, const std::string& cache_dir) {
    if (assets.valley_built) return;
    assets.valley_built = true;
    // ---- Materials --------------------------------------------------------------------------------
    const std::string tex = cache_dir + "/textures/";
    render::MaterialGPU bark = plain(scene, {1, 1, 1}, 1.0f, 0.0f);
    bark.base_color = {0.85f, 0.80f, 0.75f, 1.0f};
    bark.base_color_tex =
        load_texture_file(scene, tex + "bark_brown_01/bark_brown_01_diff.jpg", true, scene.white_texture());
    bark.normal_tex =
        load_texture_file(scene, tex + "bark_brown_01/bark_brown_01_nor_gl.jpg", false, scene.flat_normal_texture());
    bark.orm_tex = load_texture_file(scene, tex + "bark_brown_01/bark_brown_01_arm.jpg", false, scene.white_texture());
    bark.occlusion_strength = 1.0f;
    bark.normal_scale = 1.8f;                // stronger relief: the plates read at 2-5 m
    bark.flags = 4u;                         // bark: per-instance tint and moss
    bark.pad[0] = std::bit_cast<u32>(0.85f); // moss coverage
    const u32 bark_mat = scene.add_material(bark);

    std::vector<u8> needle_albedo, needle_normal;
    render::make_needle_textures(7, 1024, 512, needle_albedo, needle_normal);
    rhi::TextureUpload up;
    up.pixels = needle_albedo.data();
    up.width = 1024;
    up.height = 512;
    up.format = VK_FORMAT_R8G8B8A8_SRGB;
    up.alpha_coverage_cutoff = 0.4f;
    up.name = "needles";
    rhi::TextureUpload nup;
    nup.pixels = needle_normal.data();
    nup.width = 1024;
    nup.height = 512;
    nup.format = VK_FORMAT_R8G8B8A8_UNORM;
    nup.name = "needles_normal";
    render::MaterialGPU needles = plain(scene, {1, 1, 1}, 0.62f, 0.0f);
    needles.base_color_tex = scene.add_texture(rhi::upload_texture(scene.device(), up));
    needles.normal_tex = scene.add_texture(rhi::upload_texture(scene.device(), nup));
    needles.emissive_cutoff.w = 0.4f;
    needles.flags = 1u | 2u | 8u; // foliage, two-sided, normal alpha = baked AO
    needles.translucency = 0.5f;
    const u32 needle_mat = scene.add_material(needles);

    // ---- Conifer prototypes (procedural) ---------------------------------------------------------------
    struct Variant {
        u32 seed;
        f32 h, r;
    };
    const Variant variants[3] = {{11, 30.0f, 0.55f}, {23, 24.0f, 0.42f}, {37, 36.0f, 0.68f}};
    for (int v = 0; v < 3; ++v) {
        const render::ConiferMeshes cm = render::make_conifer(variants[v].seed, variants[v].h, variants[v].r);
        TEFRA_LOG_INFO("Conifer %d: LOD0 %u triangles (trunk %u, crown %u)", v,
                       u32((cm.trunk_lod0.indices.size() + cm.crown_lod0.indices.size()) / 3),
                       u32(cm.trunk_lod0.indices.size() / 3), u32(cm.crown_lod0.indices.size() / 3));
        render::VegetationPrototype p;
        p.lods[0].mesh[0] = scene.add_mesh(cm.trunk_lod0);
        p.lods[0].material[0] = bark_mat;
        p.lods[0].mesh[1] = scene.add_mesh(cm.crown_lod0);
        p.lods[0].material[1] = needle_mat;
        p.lods[0].max_distance = 140.0f;
        p.lods[1].mesh[0] = scene.add_mesh(cm.trunk_lod1);
        p.lods[1].material[0] = bark_mat;
        p.lods[1].mesh[1] = scene.add_mesh(cm.crown_lod1);
        p.lods[1].material[1] = needle_mat;
        p.lods[1].max_distance = 2200.0f;
        p.lod_count = 2;
        p.radius = cm.height * 0.55f;
        p.center_y = cm.height * 0.5f;
        p.wind = 0.35f;
        p.shadow_distance = 160.0f;
        p.canopy_radius = cm.canopy_radius;
        p.canopy_top = cm.height * 0.98f;
        p.canopy_density = 1.4f;
        assets.valley[v] = {p, true};
    }

    // ---- Undergrowth prototypes (Poly Haven CC0) ------------------------------------------------------
    const std::string models = cache_dir + "/models/";
    for (u32 k = 0; k < 4; ++k) {
        const std::string path = models + kUnder[k].name + "/" + kUnder[k].name + ".gltf";
        if (!file_exists(path.c_str())) continue;
        const LoadedModel lm = load_model(scene, path, 20000); // scans: cap the triangle count
        if (lm.mesh[0] == ~0u) continue;
        render::VegetationPrototype p;
        p.lods[0].mesh[0] = lm.mesh[0];
        p.lods[0].material[0] = lm.material[0];
        p.lods[0].mesh[1] = lm.mesh[1];
        p.lods[0].material[1] = lm.material[1];
        p.lods[0].max_distance = kUnder[k].dist;
        p.radius = lm.radius;
        p.center_y = lm.center_y;
        const std::string name = kUnder[k].name;
        p.wind = (name.find("grass") != std::string::npos || name.find("fern") != std::string::npos) ? 1.0f : 0.0f;
        p.cast_shadows = name.find("grass") == std::string::npos;
        p.shadow_distance = 40.0f;
        assets.valley[3 + k] = {p, true};
    }
    scene.upload();
}

vec2 populate_valley(render::Scene& scene, const render::Terrain& terrain, render::Vegetation& veg,
                     LandscapeAssets& assets, const std::string& cache_dir) {
    build_valley_assets(scene, assets, cache_dir);
    u32 tree_protos[3];
    for (u32 v = 0; v < 3; ++v) tree_protos[v] = veg.add_prototype(assets.valley[v].proto);
    u32 under_protos[4];
    for (u32 k = 0; k < 4; ++k)
        under_protos[k] = assets.valley[3 + k].valid ? veg.add_prototype(assets.valley[3 + k].proto) : ~0u;

    // ---- Scatter ---------------------------------------------------------------------------------
    const f32 half = terrain.world_size() * 0.5f - 4.0f;
    u32 trees = 0;
    const f32 spacing = 6.0f;
    for (f32 z = -half; z < half; z += spacing)
        for (f32 x = -half; x < half; x += spacing) {
            const u32 ix = static_cast<u32>(i32((x + half) / spacing)),
                      iz = static_cast<u32>(i32((z + half) / spacing));
            const f32 px = x + (hash01(ix, iz, 1) - 0.5f) * spacing * 0.9f;
            const f32 pz = z + (hash01(ix, iz, 2) - 0.5f) * spacing * 0.9f;
            const vec3 n = terrain.normal_at(px, pz);
            const f32 h = terrain.height_at(px, pz);
            const f32 rd = std::fabs(terrain.river_distance(px, pz));
            if (n.y < 0.80f || rd < 11.0f || h < 16.5f || h > 400.0f) continue;
            // Clearings: low-frequency density field.
            const f32 density =
                0.5f + 0.5f * std::sin(px * 0.011f + std::sin(pz * 0.007f) * 2.0f) * std::cos(pz * 0.009f);
            if (hash01(ix, iz, 3) > 0.25f + 0.75f * density) continue;
            const u32 v = hash_u32(ix * 31u + iz * 17u) % 3u;
            const f32 scale = 0.75f + 0.5f * hash01(ix, iz, 4);
            veg.add_instance(tree_protos[v], {px, h - 0.3f, pz}, hash01(ix, iz, 5) * kTwoPi, scale, hash01(ix, iz, 6));
            ++trees;
        }
    // Undergrowth only around the start area for now (M3 streams it by cells).
    const f32 cz0 = -120.0f, cx0 = -terrain.river_distance(0.0f, cz0) + 18.0f;
    u32 small = 0;
    for (u32 k = 0; k < 4; ++k) {
        if (under_protos[k] == ~0u) continue;
        const f32 sp = kUnder[k].spacing, R = k < 2 ? 220.0f : 500.0f;
        for (f32 z = cz0 - R; z < cz0 + R; z += sp)
            for (f32 x = cx0 - R; x < cx0 + R; x += sp) {
                const u32 ix = static_cast<u32>(i32((x - cx0 + R) / sp)),
                          iz = static_cast<u32>(i32((z - cz0 + R) / sp));
                const f32 px = x + (hash01(ix, iz, 11 + k) - 0.5f) * sp, pz = z + (hash01(ix, iz, 21 + k) - 0.5f) * sp;
                const f32 rd = std::fabs(terrain.river_distance(px, pz));
                const vec3 n = terrain.normal_at(px, pz);
                if (rd < 5.0f || n.y < 0.7f) continue;
                if (hash01(ix, iz, 31 + k) > (k < 2 ? 0.55f : 0.5f)) continue;
                const f32 h = terrain.height_at(px, pz);
                const f32 scale = (k == 3 ? 0.8f : 0.7f) + 0.6f * hash01(ix, iz, 41 + k);
                veg.add_instance(under_protos[k], {px, h - 0.05f, pz}, hash01(ix, iz, 51 + k) * kTwoPi, scale,
                                 hash01(ix, iz, 61 + k));
                ++small;
            }
    }
    veg.upload();
    TEFRA_LOG_INFO("Valley: %u trees, %u undergrowth instances", trees, small);

    return vec2{cx0, cz0};
}

vec2 populate_plains(render::Scene& scene, const render::Terrain& terrain, render::Vegetation& veg,
                     LandscapeAssets& assets, const std::string& cache_dir) {
    build_valley_assets(scene, assets, cache_dir);
    u32 tree_protos[3];
    for (u32 v = 0; v < 3; ++v) tree_protos[v] = veg.add_prototype(assets.valley[v].proto);
    const u32 rock_proto = assets.valley[5].valid ? veg.add_prototype(assets.valley[5].proto) : ~0u;
    // Open grassland: a few copses and lone trees on the knolls, boulders here and there, nothing near the spawn.
    const f32 half = terrain.world_size() * 0.5f - 4.0f;
    u32 trees = 0, rocks = 0;
    const f32 spacing = 9.0f;
    for (f32 z = -half; z < half; z += spacing)
        for (f32 x = -half; x < half; x += spacing) {
            const u32 ix = static_cast<u32>(i32((x + half) / spacing)),
                      iz = static_cast<u32>(i32((z + half) / spacing));
            const f32 px = x + (hash01(ix, iz, 1) - 0.5f) * spacing * 0.9f;
            const f32 pz = z + (hash01(ix, iz, 2) - 0.5f) * spacing * 0.9f;
            const f32 r = std::sqrt(px * px + pz * pz);
            if (r < 45.0f) continue;
            const vec3 n = terrain.normal_at(px, pz);
            const f32 h = terrain.height_at(px, pz);
            // Copses: blobs of a slow noise; elsewhere very rare lone trees.
            const f32 copse = std::sin(px * 0.013f + std::sin(pz * 0.009f) * 2.3f) * std::cos(pz * 0.011f + 1.7f);
            const f32 p = copse > 0.93f ? 0.35f : 0.003f;
            if (n.y > 0.85f && hash01(ix, iz, 3) < p) {
                const u32 v = hash_u32(ix * 31u + iz * 17u) % 3u;
                const f32 scale = 0.8f + 0.6f * hash01(ix, iz, 4);
                veg.add_instance(tree_protos[v], {px, h - 0.3f, pz}, hash01(ix, iz, 5) * kTwoPi, scale,
                                 hash01(ix, iz, 6));
                ++trees;
            } else if (rock_proto != ~0u && hash01(ix, iz, 7) < 0.002f) {
                veg.add_instance(rock_proto, {px, h - 0.1f, pz}, hash01(ix, iz, 8) * kTwoPi,
                                 0.8f + 1.6f * hash01(ix, iz, 9), hash01(ix, iz, 10));
                ++rocks;
            }
        }
    veg.upload();
    TEFRA_LOG_INFO("Plains: %u trees, %u boulders", trees, rocks);
    return vec2{0.0f, 0.0f};
}

void build_palm_assets(render::Scene& scene, LandscapeAssets& assets, const std::string& cache_dir) {
    if (assets.palms_built) return;
    assets.palms_built = true;
    const std::string tex = cache_dir + "/textures/";
    render::MaterialGPU bark = plain(scene, {1, 1, 1}, 0.9f, 0.0f);
    bark.base_color = {0.62f, 0.58f, 0.54f, 1.0f};
    bark.base_color_tex =
        load_texture_file(scene, tex + "palm_tree_bark/palm_tree_bark_diff.jpg", true, scene.white_texture());
    bark.normal_tex =
        load_texture_file(scene, tex + "palm_tree_bark/palm_tree_bark_nor_gl.jpg", false, scene.flat_normal_texture());
    bark.orm_tex =
        load_texture_file(scene, tex + "palm_tree_bark/palm_tree_bark_arm.jpg", false, scene.white_texture());
    bark.occlusion_strength = 1.0f;
    bark.normal_scale = 1.2f;
    const u32 bark_mat = scene.add_material(bark);

    std::vector<u8> frond_albedo, frond_normal;
    render::make_frond_textures(5, 1024, 512, frond_albedo, frond_normal);
    rhi::TextureUpload up;
    up.pixels = frond_albedo.data();
    up.width = 1024;
    up.height = 512;
    up.format = VK_FORMAT_R8G8B8A8_SRGB;
    up.alpha_coverage_cutoff = 0.35f;
    up.name = "fronds";
    rhi::TextureUpload nup;
    nup.pixels = frond_normal.data();
    nup.width = 1024;
    nup.height = 512;
    nup.format = VK_FORMAT_R8G8B8A8_UNORM;
    nup.name = "fronds_normal";
    render::MaterialGPU fronds = plain(scene, {1, 1, 1}, 0.55f, 0.0f);
    fronds.base_color_tex = scene.add_texture(rhi::upload_texture(scene.device(), up));
    fronds.normal_tex = scene.add_texture(rhi::upload_texture(scene.device(), nup));
    fronds.emissive_cutoff.w = 0.35f;
    fronds.flags = 1u | 2u | 8u; // foliage, two-sided, normal alpha = baked AO
    fronds.translucency = 0.5f;
    const u32 frond_mat = scene.add_material(fronds);

    // Three palms with different heights and seeds.
    struct Variant {
        u32 seed;
        f32 h;
    };
    const Variant variants[3] = {{5, 10.0f}, {9, 12.5f}, {21, 14.5f}};
    for (int v = 0; v < 3; ++v) {
        const render::PalmMeshes pm = render::make_palm(variants[v].seed, variants[v].h);
        TEFRA_LOG_INFO("Palm %d: LOD0 %u triangles, LOD1 %u triangles", v,
                       u32((pm.trunk_lod0.indices.size() + pm.crown_lod0.indices.size()) / 3),
                       u32((pm.trunk_lod1.indices.size() + pm.crown_lod1.indices.size()) / 3));
        render::VegetationPrototype p;
        p.lods[0].mesh[0] = scene.add_mesh(pm.trunk_lod0);
        p.lods[0].material[0] = bark_mat;
        p.lods[0].mesh[1] = scene.add_mesh(pm.crown_lod0);
        p.lods[0].material[1] = frond_mat;
        p.lods[0].max_distance = 120.0f;
        p.lods[1].mesh[0] = scene.add_mesh(pm.trunk_lod1);
        p.lods[1].material[0] = bark_mat;
        p.lods[1].mesh[1] = scene.add_mesh(pm.crown_lod1);
        p.lods[1].material[1] = frond_mat;
        p.lods[1].max_distance = 2000.0f;
        p.lod_count = 2;
        p.center_y = pm.height * 0.5f;
        const f32 half_h = pm.height * 0.5f;
        p.radius = std::sqrt(half_h * half_h + pm.crown_radius * pm.crown_radius) + 1.0f;
        p.wind = 0.25f;
        p.shadow_distance = 120.0f;
        p.canopy_radius = pm.canopy_radius;
        p.canopy_top = pm.height;
        p.canopy_density = 0.9f;
        assets.palms[v] = {p, true};
    }
}

void build_rock_assets(render::Scene& scene, LandscapeAssets& assets, const std::string& cache_dir) {
    if (assets.rocks_built) return;
    assets.rocks_built = true;
    for (u32 k = 0; k < 3; ++k) {
        const std::string path = cache_dir + "/models/" + kRocks[k].name + "/" + kRocks[k].name + ".gltf";
        if (!file_exists(path.c_str())) continue;
        // The CC0 scans have 0.7-1.3 M triangles: far too many for hundreds of instances (and three shadow cascades)
        // on low-end GPUs. Near LOD 12k triangles, far LOD 1.5k.
        const LoadedModel lm = load_model(scene, path, 12000, 1500);
        if (lm.mesh[0] == ~0u) continue;
        auto coarse = [&](u32 mesh) {
            for (const auto& [full, low] : lm.lod1)
                if (full == mesh) return low;
            return mesh;
        };
        render::VegetationPrototype p;
        p.lods[0].mesh[0] = lm.mesh[0];
        p.lods[0].material[0] = lm.material[0];
        p.lods[0].mesh[1] = lm.mesh[1];
        p.lods[0].material[1] = lm.material[1];
        p.lods[0].max_distance = min(kRocks[k].dist, 60.0f);
        p.lods[1].mesh[0] = coarse(lm.mesh[0]);
        p.lods[1].material[0] = lm.material[0];
        p.lods[1].mesh[1] = lm.mesh[1] == ~0u ? ~0u : coarse(lm.mesh[1]);
        p.lods[1].material[1] = lm.material[1];
        p.lods[1].max_distance = kRocks[k].dist;
        p.radius = lm.radius;
        p.center_y = lm.center_y;
        p.wind = 0.0f;
        p.shadow_distance = 150.0f;
        assets.rocks[k] = {p, true};
    }
    scene.upload();
}

void add_palm_prototypes(render::Scene& scene, render::Vegetation& veg, u32 palm_protos[3],
                         const std::string& cache_dir) {
    LandscapeAssets assets;
    build_palm_assets(scene, assets, cache_dir);
    for (u32 v = 0; v < 3; ++v) palm_protos[v] = veg.add_prototype(assets.palms[v].proto);
}

vec2 populate_coast(render::Scene& scene, const render::Terrain& terrain, render::Vegetation& veg,
                    LandscapeAssets& assets, const std::string& cache_dir) {
    build_palm_assets(scene, assets, cache_dir);
    build_rock_assets(scene, assets, cache_dir);
    u32 palms[3];
    for (u32 v = 0; v < 3; ++v) palms[v] = veg.add_prototype(assets.palms[v].proto);
    u32 rock_protos[3];
    for (u32 k = 0; k < 3; ++k) rock_protos[k] = assets.rocks[k].valid ? veg.add_prototype(assets.rocks[k].proto) : ~0u;

    const f32 half = terrain.world_size() * 0.5f - 10.0f;
    auto seaward = [&](f32 x, f32 z) {
        const f32 gx = terrain.shore_distance(x + 2.0f, z) - terrain.shore_distance(x - 2.0f, z);
        const f32 gz = terrain.shore_distance(x, z + 2.0f) - terrain.shore_distance(x, z - 2.0f);
        return std::atan2(gz, gx); // yaw that points the palm's lean (+X) towards the sea
    };
    // Palms: groves along the back of the beach and the dunes, leaning out over the sand.
    u32 n_palms = 0, n_rocks = 0;
    const f32 sp = 6.0f;
    for (f32 z = -700.0f; z < 500.0f; z += sp)
        for (f32 x = -half; x < half; x += sp) {
            const u32 ix = static_cast<u32>(i32((x + half) / sp)), iz = static_cast<u32>(i32((z + 700.0f) / sp));
            const f32 px = x + (hash01(ix, iz, 1) - 0.5f) * sp, pz = z + (hash01(ix, iz, 2) - 0.5f) * sp;
            const f32 land = -terrain.shore_distance(px, pz);
            if (land < 36.0f || land > 160.0f || terrain.normal_at(px, pz).y < 0.93f ||
                terrain.height_at(px, pz) > terrain.sea_level() + 9.0f)
                continue;
            const f32 grove = 0.5f + 0.5f * std::sin(px * 0.021f + std::sin(pz * 0.017f) * 2.0f);
            const f32 p = 0.3f * (0.15f + 0.85f * grove * grove) * (1.0f - smoothstep(70.0f, 160.0f, land));
            if (hash01(ix, iz, 3) > p) continue;
            const f32 yaw = seaward(px, pz) + (hash01(ix, iz, 4) - 0.5f) * 1.2f;
            veg.add_instance(palms[hash_u32(ix * 7u + iz * 13u) % 3u], {px, terrain.height_at(px, pz) - 0.1f, pz}, yaw,
                             0.7f + 0.65f * hash01(ix, iz, 5), hash01(ix, iz, 6));
            ++n_palms;
        }
    // Rocks.
    const f32 rsp[3] = {7.0f, 9.0f, 22.0f};
    for (u32 k = 0; k < 3; ++k) {
        if (rock_protos[k] == ~0u) continue;
        for (f32 z = -800.0f; z < 300.0f; z += rsp[k])
            for (f32 x = -half; x < half; x += rsp[k]) {
                const u32 ix = static_cast<u32>(i32((x + half) / rsp[k])),
                          iz = static_cast<u32>(i32((z + 800.0f) / rsp[k]));
                const f32 px = x + (hash01(ix, iz, 11 + k) - 0.5f) * rsp[k];
                const f32 pz = z + (hash01(ix, iz, 21 + k) - 0.5f) * rsp[k];
                const f32 sd = terrain.shore_distance(px, pz);
                const f32 headland = smoothstep(240.0f, 380.0f, std::fabs(px));
                f32 prob = 0.0f;
                const bool low = terrain.height_at(px, pz) < terrain.sea_level() + 3.0f;
                if (k == 0 && low && sd > -6.0f && sd < 30.0f) prob = 0.04f + 0.5f * headland;
                if (k == 1 && low && sd > -45.0f && sd < -4.0f) prob = 0.05f + 0.3f * headland;
                if (k == 2 && sd > -14.0f && sd < 18.0f && terrain.height_at(px, pz) < terrain.sea_level() + 4.0f)
                    prob = 0.6f * headland;
                if (hash01(ix, iz, 31 + k) > prob) continue;
                const f32 scale = (k == 2 ? 0.8f : 0.6f) + (k == 2 ? 0.7f : 1.0f) * hash01(ix, iz, 41 + k);
                veg.add_instance(rock_protos[k], {px, terrain.height_at(px, pz) - 0.25f * scale, pz},
                                 hash01(ix, iz, 51 + k) * kTwoPi, scale, hash01(ix, iz, 61 + k));
                ++n_rocks;
            }
    }
    veg.upload();
    TEFRA_LOG_INFO("Coast: %u palms, %u rocks", n_palms, n_rocks);

    const f32 cx = 0.0f;
    f32 cz = 0.0f;
    for (f32 z = 400.0f; z > -400.0f; z -= 0.5f)
        if (terrain.shore_distance(cx, z) > -20.0f) {
            cz = z;
            break;
        }
    return vec2{cx, cz};
}

// ---- CPU side ----------------------------------------------------------------------------------------------------

namespace {

constexpr i64 kKindNone = 0, kKindCoast = 2, kKindPlains = 3; // Terrain.Kind enum: None, Valley, Coast, Plains

i64 int_prop(const Instance& inst, const char* name, i64 def) {
    const Value* v = inst.get(name);
    return v ? v->as_int() : def;
}

// Signature of the properties a build depends on: a change rebuilds the object.
struct TerrainSig {
    bool present = false;
    u64 id = 0;
    i64 kind = 0, seed = 0;
    f32 sea = 0.0f;
    bool vegetation = false;
    bool operator==(const TerrainSig& o) const {
        return present == o.present && id == o.id && kind == o.kind && seed == o.seed && sea == o.sea &&
               vegetation == o.vegetation;
    }
    bool operator!=(const TerrainSig& o) const { return !(*this == o); }
};

struct OceanSig {
    bool present = false;
    u64 id = 0;
    f32 sea = 0.0f, swell = 0.0f, surf = 0.0f;
    vec2 wind{};
    bool operator==(const OceanSig& o) const {
        return present == o.present && id == o.id && sea == o.sea && swell == o.swell && surf == o.surf &&
               wind.x == o.wind.x && wind.y == o.wind.y;
    }
    bool operator!=(const OceanSig& o) const { return !(*this == o); }
};

} // namespace

Instance* find_terrain(const DataModel& dm) {
    const Instance* ws = dm.workspace();
    if (!ws) return nullptr;
    for (Instance* c : ws->children())
        if (c->is_a("Terrain") && int_prop(*c, "Kind", kKindNone) != kKindNone) return c;
    return nullptr;
}

Instance* find_ocean(const DataModel& dm) {
    const Instance* ws = dm.workspace();
    if (!ws) return nullptr;
    for (Instance* c : ws->children())
        if (c->is_a("Ocean")) return c;
    return nullptr;
}

bool terrain_desc(const Instance& terrain, render::TerrainDesc& out) {
    const i64 kind = int_prop(terrain, "Kind", kKindNone);
    if (kind == kKindNone) return false;
    out = render::TerrainDesc{};
    out.kind = kind == kKindCoast    ? render::TerrainKind::Coast
               : kind == kKindPlains ? render::TerrainKind::Plains
                                     : render::TerrainKind::Valley;
    out.seed = static_cast<u32>(int_prop(terrain, "Seed", 1337));
    out.sea_level = terrain.get_float("SeaLevel", 0.0f);
    return true;
}

f32 terrain_height(const Instance& terrain, f32 x, f32 z) {
    render::TerrainDesc desc;
    if (!terrain_desc(terrain, desc)) return 0.0f;
    return render::TerrainField::acquire(desc)->height_at(x, z);
}

// ---- Live landscape ----------------------------------------------------------------------------------------------

struct EnvironmentSync::Impl {
    Impl(const rhi::Device& d, rhi::BindlessHeap& h, render::Scene& s, render::Renderer& r)
        : dev(d), heap(h), scene(s), renderer(r) {}
    const rhi::Device& dev;
    rhi::BindlessHeap& heap;
    render::Scene& scene;
    render::Renderer& renderer;
    LandscapeAssets assets; // kept across rebuilds: a rebuild adds nothing to the Scene
    std::unique_ptr<render::Terrain> terrain;
    std::unique_ptr<render::Vegetation> vegetation;
    std::unique_ptr<render::Water> water;
    std::unique_ptr<render::Ocean> ocean;
    TerrainSig terrain_sig;
    OceanSig ocean_sig;
    // Settling: a changed signature is applied only after it stayed the same for the requested number of frames.
    bool has_pending = false;
    TerrainSig pending_terrain;
    OceanSig pending_ocean;
    u32 pending_frames = 0;
    u64 revision = 0;

    void publish() {
        render::RenderWorld lw;
        lw.terrain = terrain.get();
        lw.vegetation = vegetation.get();
        lw.water = water.get();
        lw.ocean = ocean.get();
        renderer.set_landscape(lw);
    }
};

EnvironmentSync::EnvironmentSync(const rhi::Device& dev, rhi::BindlessHeap& heap, render::Scene& scene,
                                 render::Renderer& renderer)
    : impl_(std::make_unique<Impl>(dev, heap, scene, renderer)) {}

EnvironmentSync::~EnvironmentSync() { clear(); }

void EnvironmentSync::clear() {
    Impl& s = *impl_;
    if (!s.terrain && !s.ocean && !s.vegetation && !s.water) return;
    s.dev.wait_idle();
    s.renderer.set_landscape(render::RenderWorld{});
    s.ocean.reset();
    s.water.reset();
    s.vegetation.reset();
    s.terrain.reset();
    s.terrain_sig = TerrainSig{};
    s.ocean_sig = OceanSig{};
}

bool EnvironmentSync::update(const DataModel& model, u32 settle_frames) {
    Impl& s = *impl_;
    const Instance* t = find_terrain(model);
    TerrainSig ts;
    if (t) {
        ts.present = true;
        ts.id = t->id();
        ts.kind = int_prop(*t, "Kind", kKindNone);
        ts.seed = int_prop(*t, "Seed", 1337);
        ts.sea = t->get_float("SeaLevel", 0.0f);
        ts.vegetation = t->get_bool("Vegetation", true);
    }
    const Instance* o = find_ocean(model);
    OceanSig os;
    if (o) {
        os.present = true;
        os.id = o->id();
        os.sea = ts.present ? ts.sea : 0.0f;
        os.swell = o->get_float("SwellHeight", 0.9f);
        os.surf = o->get_float("SurfHeight", 0.7f);
        const vec3 w = o->get_vec3("WindDirection", vec3{0.18f, 0.0f, 1.0f});
        os.wind = vec2{w.x, w.z};
    }
    if (ts == s.terrain_sig && os == s.ocean_sig) {
        s.has_pending = false;
        return false;
    }
    if (settle_frames > 1 && s.terrain_sig.present && ts.present) {
        // A landscape property dragged every frame keeps changing: wait until it rests (one rebuild per gesture). The
        // first build and removals are applied at once.
        if (!s.has_pending || ts != s.pending_terrain || os != s.pending_ocean) {
            s.has_pending = true;
            s.pending_terrain = ts;
            s.pending_ocean = os;
            s.pending_frames = 1;
        } else {
            ++s.pending_frames;
        }
        if (s.pending_frames < settle_frames) return false;
    }
    s.has_pending = false;

    if (ts != s.terrain_sig) {
        // Terrain (and the water that depends on it): rebuild everything.
        s.dev.wait_idle();
        s.renderer.set_landscape(render::RenderWorld{});
        s.ocean.reset();
        s.water.reset();
        s.vegetation.reset();
        s.terrain.reset();
        s.terrain_sig = ts;
        if (ts.present) {
            render::TerrainDesc td;
            terrain_desc(*t, td);
            td.texture_dir = file_exists("assets/cache/textures") ? "assets/cache/textures" : "";
            s.terrain = std::make_unique<render::Terrain>(s.dev, s.heap, td);
            if (ts.vegetation) {
                s.vegetation = std::make_unique<render::Vegetation>(s.dev, s.scene);
                if (td.kind == render::TerrainKind::Coast)
                    populate_coast(s.scene, *s.terrain, *s.vegetation, s.assets);
                else if (td.kind == render::TerrainKind::Plains)
                    populate_plains(s.scene, *s.terrain, *s.vegetation, s.assets);
                else
                    populate_valley(s.scene, *s.terrain, *s.vegetation, s.assets);
            }
            if (td.kind == render::TerrainKind::Valley) s.water = std::make_unique<render::Water>(s.dev, *s.terrain);
            ++s.revision;
            TEFRA_LOG_INFO("Landscape: %s terrain built (seed %lld, sea %.1f m, %s vegetation)",
                           td.kind == render::TerrainKind::Coast    ? "coast"
                           : td.kind == render::TerrainKind::Plains ? "plains"
                                                                    : "valley",
                           static_cast<long long>(ts.seed), static_cast<f64>(ts.sea),
                           ts.vegetation ? "with" : "without");
        }
        // The ocean level follows the sea of the terrain: force the ocean to be rebuilt below.
        s.ocean_sig = OceanSig{};
    }
    if (os != s.ocean_sig) {
        s.ocean.reset();
        s.ocean_sig = os;
        if (os.present) {
            render::OceanDesc od;
            od.level = os.sea;
            od.wind_dir = os.wind;
            od.swell_height = os.swell;
            od.surf_height = os.surf;
            s.ocean = std::make_unique<render::Ocean>(s.dev, od);
        }
    }
    s.publish();
    return true;
}

bool EnvironmentSync::has_terrain() const { return impl_->terrain != nullptr; }
bool EnvironmentSync::has_ocean() const { return impl_->ocean != nullptr; }

render::TerrainKind EnvironmentSync::kind() const {
    return impl_->terrain ? impl_->terrain->kind() : render::TerrainKind::Valley;
}

// The sea of the Terrain property (the shared field of a valley keeps the level it was generated with).
f32 EnvironmentSync::sea_level() const { return impl_->terrain ? impl_->terrain_sig.sea : 0.0f; }

f32 EnvironmentSync::height_at(f32 x, f32 z) const { return impl_->terrain ? impl_->terrain->height_at(x, z) : 0.0f; }

const render::TerrainField* EnvironmentSync::field() const {
    return impl_->terrain ? &impl_->terrain->field() : nullptr;
}

u64 EnvironmentSync::terrain_revision() const { return impl_->revision; }

} // namespace tefra::world
