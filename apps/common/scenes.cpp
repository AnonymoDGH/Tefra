#include "scenes.h"

#include "tefra/renderer/gltf.h"
#include "tefra/rhi/upload.h"

#include "tefra/core/log.h"

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
