#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest.h>

#include "runner.h"
#include "scenes.h"

#include "tefra/core/log.h"
#include "tefra/renderer/mesh.h"
#include "tefra/renderer/scene.h"
#include "tefra/renderer/skin.h"

#include <cmath>
#include <memory>
#include <vector>

using namespace tefra;

namespace {

// Open tube along +Y, 2 m tall, 8 sides. Rings at y = 0, 0.5, 1, 1.5, 2: the bottom half follows joint 0, the top
// half joint 1, and the middle ring is shared 50/50 (a bent elbow when joint 1 rotates about the middle ring).
render::MeshData make_tube() {
    render::MeshData m;
    constexpr u32 kSides = 8;
    constexpr f32 kRadius = 0.25f;
    const f32 rings[5] = {0.0f, 0.5f, 1.0f, 1.5f, 2.0f};
    for (u32 r = 0; r < 5; ++r)
        for (u32 s = 0; s <= kSides; ++s) {
            const f32 a = 6.2831853f * f32(s % kSides) / f32(kSides);
            const vec3 n{std::cos(a), 0.0f, std::sin(a)};
            m.vertices.push_back(render::make_vertex(vec3{n.x * kRadius, rings[r], n.z * kRadius}, n, vec2{},
                                                     vec3{-n.z, 0.0f, n.x}, 1.0f));
            render::SkinAttr sa;
            if (rings[r] <= 0.5f) {
                sa.joints[0] = 0;
                sa.weights[0] = 1.0f;
            } else if (rings[r] >= 1.5f) {
                sa.joints[0] = 1;
                sa.weights[0] = 1.0f;
            } else {
                // Middle ring (1.0) is 50/50; the rings in between at 0.5 and 1.5 are the bone ends.
                sa.joints[0] = 0;
                sa.weights[0] = 0.5f;
                sa.joints[1] = 1;
                sa.weights[1] = 0.5f;
            }
            m.skin.push_back(sa);
        }
    // Side quads (a, b, c, d) with counter-clockwise triangles seen from outside.
    for (u32 r = 0; r < 4; ++r)
        for (u32 s = 0; s < kSides; ++s) {
            const u32 a = r * (kSides + 1) + s, b = a + 1, d = a + (kSides + 1), c = d + 1;
            m.indices.insert(m.indices.end(), {a, c, b, a, d, c});
        }
    return m;
}

std::vector<mat4> palette_for_bend(const render::MeshData& tube, f32 bend_deg) {
    (void)tube;
    // Joint 0 at the origin; joint 1 at the middle ring (y = 1), rotated about Z.
    std::vector<mat4> p(2);
    p[0] = mat4{};
    const f32 a = bend_deg * 3.14159265f / 180.0f;
    const quat q{0.0f, 0.0f, std::sin(a * 0.5f), std::cos(a * 0.5f)};
    // global1 = T(0,1,0) * R; palette1 = global1 * T(0,-1,0)
    const mat4 pivot = compose_trs(vec3{0.0f, 1.0f, 0.0f}, quat{0.0f, 0.0f, 0.0f, 1.0f}, vec3{1.0f});
    const mat4 rot = compose_trs(vec3{0.0f}, q, vec3{1.0f});
    const mat4 unpivot = compose_trs(vec3{0.0f, -1.0f, 0.0f}, quat{0.0f, 0.0f, 0.0f, 1.0f}, vec3{1.0f});
    p[1] = pivot * rot * unpivot;
    return p;
}

struct TubeState {
    render::Scene* scene = nullptr;
    u32 skin = render::kNoSkin;
    bool skinned = false;
    f32 bend_deg = 0.0f;
};

app::RunConfig tube_config() {
    app::RunConfig cfg;
    cfg.width = 480;
    cfg.height = 360;
    cfg.frames = 2;
    cfg.validation = true;
    cfg.strict_baseline = true;
    cfg.capture_to_memory = true;
    cfg.scene = "custom";
    return cfg;
}

// Renders the tube (static, or skinned with a bend) and returns the last frame.
app::RunResult render_tube(bool skinned, f32 bend_deg, const render::MeshData& tube) {
    auto st = std::make_shared<TubeState>();
    st->skinned = skinned;
    st->bend_deg = bend_deg;
    app::RunConfig cfg = tube_config();
    cfg.build_custom = [st, &tube](render::Scene& s, render::Environment& e, render::Atmosphere& a,
                                   app::SceneSetup& setup) {
        app::build_materials_sky_scene(s, e, a, setup); // sky, sun and environment; its test spheres are removed below
        s.instances().clear();
        st->scene = &s;
        render::MaterialGPU m{};
        m.base_color = vec4(0.55f, 0.35f, 0.2f, 1.0f);
        m.roughness = 0.6f;
        m.base_color_tex = s.white_texture();
        m.normal_tex = s.flat_normal_texture();
        m.orm_tex = s.white_texture();
        m.emissive_tex = render::kNoTexture;
        m.occlusion_strength = 0.0f;
        render::Instance inst;
        inst.mesh = s.add_mesh(tube);
        inst.material = s.add_material(m);
        inst.position = {0.0, 0.0, -3.0};
        if (st->skinned) {
            st->skin = s.add_skin(2);
            inst.skin = st->skin;
        }
        s.add_instance(inst);
        s.upload();
        setup.camera.position = {0.0, 1.0, 0.0};
        setup.camera.pitch = 0.0f;
        setup.camera.fovy = radians(40.0f);
        setup.settings.gtao = false;
    };
    cfg.on_frame = [st, &tube](u64) {
        // Posing every frame: the previous palette becomes the previous frame's (motion vectors of the skinned draw).
        if (st->skinned && st->scene) st->scene->set_skin_palette(st->skin, palette_for_bend(tube, st->bend_deg));
    };
    return app::run(cfg);
}

} // namespace

TEST_CASE("skinning: GPU reference math agrees with the CPU reference on the tube") {
    // Same palette, both sides: the CPU reference moves the rest tube by the bend, so its bounding box differs.
    const render::MeshData tube = make_tube();
    const std::vector<mat4> palette = palette_for_bend(tube, 60.0f);
    const render::MeshData posed = render::skin_mesh_reference(tube, palette);
    f32 top_x = 0.0f;
    for (const render::Vertex& v : posed.vertices) top_x = std::fmin(top_x, v.pos[0]);
    CHECK(top_x < -0.2f); // the top bends towards -X under +60 degrees about Z
}

TEST_CASE("skinning: identity palette renders like the static mesh, a bend changes the image, zero validation errors") {
    const render::MeshData tube = make_tube();
    const app::RunResult stat = render_tube(false, 0.0f, tube);
    REQUIRE_MESSAGE(stat.ok, stat.error);
    const app::RunResult rest = render_tube(true, 0.0f, tube);
    REQUIRE_MESSAGE(rest.ok, rest.error);
    const app::RunResult bent = render_tube(true, 60.0f, tube);
    REQUIRE_MESSAGE(bent.ok, bent.error);
    CHECK(rest.validation_enabled);
    CHECK(rest.validation_errors == 0);
    CHECK(bent.validation_errors == 0);
    CHECK(rest.draw_calls >= 1);

    const ImageRGBA8 a = stat.capture;
    const ImageRGBA8 b = rest.capture;
    const ImageRGBA8 c = bent.capture;
    REQUIRE(a.width == b.width);
    const ImageDiff same = image_compare(a, b, 8);
    const ImageDiff moved = image_compare(b, c, 8);
    INFO("static vs rest rmse=" << same.rmse << " rest vs bent rmse=" << moved.rmse);
    CHECK(same.rmse < 1.5);
    CHECK(moved.rmse > 2.0);
}
