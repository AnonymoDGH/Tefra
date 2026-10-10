#include "scenes.h"

#include "tefra/renderer/gltf.h"
#include "tefra/rhi/upload.h"

#include "tefra/core/fs.h"
#include "tefra/core/image.h"
#include "tefra/core/log.h"

#include <bit>
#include <cmath>

namespace tefra::app {
namespace {

u32 checker_texture(render::Scene& scene) {
    constexpr u32 kSize = 256;
    std::vector<u8> px(kSize * kSize * 4);
    for (u32 y = 0; y < kSize; ++y)
        for (u32 x = 0; x < kSize; ++x) {
            const bool on = ((x / (kSize / 2)) + (y / (kSize / 2))) % 2 == 0;
            const u8 v = on ? 120 : 70; // sRGB ~0.19 / 0.06 linear: dark realistic floor
            u8* p = &px[(y * kSize + x) * 4];
            p[0] = v;
            p[1] = static_cast<u8>(v * 0.96f);
            p[2] = static_cast<u8>(v * 0.9f);
            p[3] = 255;
        }
    rhi::TextureUpload up;
    up.pixels = px.data();
    up.width = kSize;
    up.height = kSize;
    up.format = VK_FORMAT_R8G8B8A8_SRGB;
    up.name = "checker";
    return scene.add_texture(rhi::upload_texture(scene.device(), up));
}

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

} // namespace

void build_materials_scene(render::Scene& scene, render::Environment& env, SceneSetup& setup) {
    render::ProceduralSky sky;
    sky.sun_direction = setup.settings.sun_direction;
    sky.sun_illuminance = setup.settings.sun_illuminance;
    env.build_procedural(sky);

    const u32 sphere = scene.add_mesh(render::make_sphere(0.5f, 64, 32));
    const u32 plane = scene.add_mesh(render::make_plane(400.0f, 400.0f, 1.0f));
    render::MaterialGPU floor = plain(scene, {1, 1, 1}, 0.8f, 0.0f);
    floor.base_color_tex = checker_texture(scene);
    const u32 floor_mat = scene.add_material(floor);
    render::Instance fi;
    fi.mesh = plane;
    fi.material = floor_mat;
    scene.add_instance(fi);

    const vec3 rows[5] = {{0.80f, 0.80f, 0.80f},
                          {0.95f, 0.64f, 0.54f},
                          {0.56f, 0.57f, 0.58f},
                          {0.60f, 0.08f, 0.05f},
                          {0.10f, 0.30f, 0.12f}};
    const f32 metal[5] = {0.0f, 1.0f, 1.0f, 0.0f, 0.0f};
    for (int r = 0; r < 5; ++r)
        for (int c = 0; c < 7; ++c) {
            const f32 rough = 0.05f + 0.95f * f32(c) / 6.0f;
            const u32 m = scene.add_material(plain(scene, rows[r], rough, metal[r]));
            render::Instance in;
            in.mesh = sphere;
            in.material = m;
            in.position = {(c - 3) * 1.2, 0.5 + r * 1.15, -6.0};
            scene.add_instance(in);
        }
    scene.upload();
    setup.camera.position = {0.0, 2.6, 2.5};
    setup.camera.pitch = radians(-6.0f);
    setup.camera.fovy = radians(55.0f);
}

bool build_gltf_scene(render::Scene& scene, render::Environment& env, SceneSetup& setup, const std::string& gltf,
                      const std::string& hdr) {
    if (!hdr.empty() && env.build_from_hdr(hdr.c_str(), 4500.0f)) {
        if (env.dominant_illuminance() > 1000.0f) {
            setup.settings.sun_direction = env.dominant_direction();
            setup.settings.sun_illuminance = env.dominant_illuminance();
        }
    } else {
        render::ProceduralSky sky;
        sky.sun_direction = setup.settings.sun_direction;
        env.build_procedural(sky);
    }
    render::GltfLoadOptions opt;
    const usize first = scene.instances().size();
    const auto res = render::load_gltf(scene, gltf.c_str(), opt);
    if (!res.ok) return false;
    // Rest the model on the ground (y = 0) and frame it.
    f32 lowest = 1e30f, radius = 0.0f;
    for (usize i = first; i < scene.instances().size(); ++i) {
        const auto& in = scene.instances()[i];
        const auto& b = scene.meshes()[in.mesh].bounds;
        const f32 s = max(max(in.scale.x, in.scale.y), in.scale.z);
        lowest = min(lowest, static_cast<f32>(in.position.y) + (b.center.y - b.radius) * s);
        radius = max(radius, b.radius * s +
                                 length(vec3{static_cast<f32>(in.position.x), 0.0f, static_cast<f32>(in.position.z)}));
    }
    for (usize i = first; i < scene.instances().size(); ++i)
        scene.instances()[i].position.y -= static_cast<f64>(lowest);
    if (!hdr.empty()) { // look-dev: the HDRI provides the surroundings
        scene.upload();
        setup.camera.position = {0.0, static_cast<f64>(radius), static_cast<f64>(max(radius, 0.5f)) * 2.4};
        return true;
    }
    const u32 plane = scene.add_mesh(render::make_plane(400.0f, 400.0f, 1.0f));
    const u32 ground = scene.add_material(plain(scene, {0.09f, 0.08f, 0.06f}, 0.95f, 0.0f));
    render::Instance gi;
    gi.mesh = plane;
    gi.material = ground;
    scene.add_instance(gi);
    scene.upload();
    const f64 dist = static_cast<f64>(max(radius, 0.5f)) * 2.6;
    setup.camera.position = {0.0, 1.0 + static_cast<f64>(radius) * 0.4, dist};
    setup.camera.pitch = -std::atan2(static_cast<f32>(setup.camera.position.y) - radius * 0.6f, static_cast<f32>(dist));
    return true;
}

} // namespace tefra::app

namespace tefra::app {

void build_atmosphere_environment(const render::Atmosphere& atmo, render::Environment& env, vec3 sun_dir) {
    env.build_from_equirect(atmo.render_equirect(sun_dir, 256));
}

void build_materials_sky_scene(render::Scene& scene, render::Environment& env, const render::Atmosphere& atmo,
                               SceneSetup& setup) {
    build_materials_scene(scene, env, setup);
    setup.use_atmosphere = true;
    setup.settings.sun_direction = normalize(vec3{0.45f, 0.16f, -0.88f}); // ~9 deg: early morning
    setup.settings.fog_density = 0.012f;
    setup.settings.fog_falloff = 0.08f;
    setup.settings.fog_height = 0.0f;
    setup.settings.ev100 = 13.0f;
    build_atmosphere_environment(atmo, env, setup.settings.sun_direction);
}

} // namespace tefra::app

namespace tefra::app {

namespace {

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
};

// Loads a glTF and keeps its first two primitives as a vegetation prototype (instances are removed).
LoadedModel load_model(render::Scene& scene, const std::string& path) {
    LoadedModel m;
    const usize first = scene.instances().size();
    if (!render::load_gltf(scene, path.c_str(), {}).ok) return m;
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
f32 hash01(u32 a, u32 b, u32 c) {
    return static_cast<f32>(hash_u32(a * 73856093u ^ b * 19349663u ^ c * 83492791u) >> 8) / 16777216.0f;
}

} // namespace

void build_valley_scene(render::Scene& scene, render::Environment& env, const render::Atmosphere& atmo,
                        const render::Terrain& terrain, render::Vegetation& veg, SceneSetup& setup) {
    setup.use_atmosphere = true;
    setup.use_terrain = true;
    setup.settings.sun_direction = normalize(vec3{0.62f, 0.20f, -0.76f}); // ~11.5 deg morning sun
    setup.settings.fog_density = 0.008f;
    setup.settings.fog_falloff = 0.035f;
    setup.settings.fog_height = terrain.height_at(0.0f, 0.0f);
    setup.settings.cascades = 3;
    setup.settings.shadow_distance = 200.0f;
    setup.settings.ev100 = 12.5f;
    build_atmosphere_environment(atmo, env, setup.settings.sun_direction);

    // ---- Materials --------------------------------------------------------------------------------
    const std::string tex = "assets/cache/textures/";
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
    render::make_needle_textures(7, 512, 256, needle_albedo, needle_normal);
    rhi::TextureUpload up;
    up.pixels = needle_albedo.data();
    up.width = 512;
    up.height = 256;
    up.format = VK_FORMAT_R8G8B8A8_SRGB;
    up.alpha_coverage_cutoff = 0.4f;
    up.name = "needles";
    render::MaterialGPU needles = plain(scene, {1, 1, 1}, 0.62f, 0.0f);
    needles.base_color_tex = scene.add_texture(rhi::upload_texture(scene.device(), up));
    needles.emissive_cutoff.w = 0.4f;
    needles.flags = 1u | 2u; // foliage, two-sided
    needles.translucency = 0.5f;
    const u32 needle_mat = scene.add_material(needles);

    // ---- Conifer prototypes (procedural) ---------------------------------------------------------------
    struct Variant {
        u32 seed;
        f32 h, r;
    };
    const Variant variants[3] = {{11, 30.0f, 0.55f}, {23, 24.0f, 0.42f}, {37, 36.0f, 0.68f}};
    u32 tree_protos[3];
    for (int v = 0; v < 3; ++v) {
        const render::ConiferMeshes cm = render::make_conifer(variants[v].seed, variants[v].h, variants[v].r);
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
        tree_protos[v] = veg.add_prototype(p);
    }

    // ---- Undergrowth prototypes (Poly Haven CC0) ------------------------------------------------------
    const std::string models = "assets/cache/models/";
    struct Under {
        const char* name;
        f32 dist;
        f32 spacing;
        u32 proto = ~0u;
    };
    Under under[4] = {{"fern_02", 70.0f, 2.2f},
                      {"grass_medium_02", 45.0f, 1.1f},
                      {"rock_moss_set_01", 160.0f, 9.0f},
                      {"tree_stump_01", 160.0f, 21.0f}};
    for (Under& u : under) {
        const std::string path = models + u.name + "/" + u.name + ".gltf";
        if (!file_exists(path.c_str())) continue;
        const LoadedModel lm = load_model(scene, path);
        if (lm.mesh[0] == ~0u) continue;
        render::VegetationPrototype p;
        p.lods[0].mesh[0] = lm.mesh[0];
        p.lods[0].material[0] = lm.material[0];
        p.lods[0].mesh[1] = lm.mesh[1];
        p.lods[0].material[1] = lm.material[1];
        p.lods[0].max_distance = u.dist;
        p.radius = lm.radius;
        p.center_y = lm.center_y;
        p.wind = (std::string(u.name).find("grass") != std::string::npos ||
                  std::string(u.name).find("fern") != std::string::npos)
                     ? 1.0f
                     : 0.0f;
        p.cast_shadows = std::string(u.name).find("grass") == std::string::npos;
        p.shadow_distance = 40.0f;
        u.proto = veg.add_prototype(p);
    }
    scene.upload();

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
        if (under[k].proto == ~0u) continue;
        const f32 sp = under[k].spacing, R = k < 2 ? 220.0f : 500.0f;
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
                veg.add_instance(under[k].proto, {px, h - 0.05f, pz}, hash01(ix, iz, 51 + k) * kTwoPi, scale,
                                 hash01(ix, iz, 61 + k));
                ++small;
            }
    }
    veg.upload();
    TEFRA_LOG_INFO("Valley: %u trees, %u undergrowth instances", trees, small);

    // Stand on the valley floor beside the river, looking downstream.
    const f32 z = cz0, x = cx0;
    setup.camera.position = {static_cast<f64>(x), static_cast<f64>(terrain.height_at(x, z) + 1.7f),
                             static_cast<f64>(z)};
    setup.camera.yaw = radians(185.0f);
    setup.camera.pitch = radians(3.0f);
    setup.camera.fovy = radians(60.0f);
}

} // namespace tefra::app
