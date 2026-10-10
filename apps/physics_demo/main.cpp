// tefra_physics_demo: a block tower hit by a wrecking ball. The DataModel is simulated by engine/physics (ADR-014) and
// drawn headless through the scene bridge (SceneSync), like tefra_bench. Writes two captures: the impact and the
// settled tower. Run from the repository root (captures go to docs/research/m3-physics by default).
#include "options.h"
#include "scenes.h"

#include "tefra/core/cli.h"
#include "tefra/core/log.h"
#include "tefra/physics/physics_world.h"
#include "tefra/renderer/scene.h"
#include "tefra/world/datamodel.h"
#include "tefra/world/scene_sync.h"

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace tefra;

namespace {

constexpr u32 kLayers = 10;
constexpr u32 kImpactFrame = 125;  // the ball hits the tower at about 1.3 s; the stack starts falling near 2 s (60 Hz)
constexpr u32 kSettledFrame = 360; // 6 s: the tower has come to rest

struct DemoState {
    std::unique_ptr<world::DataModel> model;
    std::unique_ptr<physics::PhysicsWorld> physics;
    std::unique_ptr<world::SceneSync> sync;
    std::vector<std::pair<world::Instance*, vec3>> blocks; // initial positions
    world::Instance* ball = nullptr;
};

world::Instance* make_part(world::DataModel& dm, const char* name, vec3 pos, vec3 size, bool anchored, u32 shape,
                           f32 density, vec3 color) {
    world::Instance* p = dm.create("Part", dm.workspace(), name);
    p->set("Position", world::Value::vector3(pos));
    p->set("Size", world::Value::vector3(size));
    p->set("Anchored", world::Value::boolean(anchored));
    p->set("Shape", world::Value::enumeration(shape));
    p->set("Density", world::Value::number(density));
    p->set("Color", world::Value::color3(color));
    return p;
}

// Ground, then kLayers layers of three 1 x 1 x 3 bricks, alternating direction, then the ball from the left. The
// bricks are light (density 100, about 300 kg) with friction 0.3, and the ball is about 8.5 t at 10 m/s: with
// plastic-like bricks (density 700) the bottom friction holds the stack and the ball just bounces back.
void build_tower(DemoState& st) {
    constexpr f32 kBrickDensity = 100.0f;
    constexpr f32 kBrickFriction = 0.3f;
    world::DataModel& dm = *st.model;
    make_part(dm, "Ground", {0, -0.5f, 0}, {80, 1, 80}, true, 0, 700.0f, {0.42f, 0.45f, 0.40f});
    for (u32 layer = 0; layer < kLayers; ++layer) {
        const f32 y = 0.5f + static_cast<f32>(layer);
        const vec3 tint = layer % 2 ? vec3{0.78f, 0.62f, 0.42f} : vec3{0.70f, 0.52f, 0.34f};
        for (int k = -1; k <= 1; ++k) {
            const f32 o = static_cast<f32>(k);
            world::Instance* b = layer % 2 == 0
                                     ? make_part(dm, "Block", {o, y, 0}, {1, 1, 3}, false, 0, kBrickDensity, tint)
                                     : make_part(dm, "Block", {0, y, o}, {3, 1, 1}, false, 0, kBrickDensity, tint);
            b->set("Friction", world::Value::number(kBrickFriction));
            st.blocks.emplace_back(b, b->get_vec3("Position"));
        }
    }
    st.ball = make_part(dm, "WreckingBall", {-16, 1.5f, 0}, {3, 3, 3}, false, 1, 600.0f, {0.75f, 0.14f, 0.10f});
    st.ball->set("AssemblyLinearVelocity", world::Value::vector3({10, 0, 0}));
}

void build_scene(DemoState& st, render::Scene& scene, render::Environment& env, render::Atmosphere& atmo,
                 app::SceneSetup& setup) {
    // Morning sky, sun and fog from the materials_sky setup; its test spheres are removed (the materials stay).
    app::build_materials_sky_scene(scene, env, atmo, setup);
    setup.settings.fog_density = 0.004f; // the scene's morning haze is too milky for a close view
    scene.instances().clear();

    st.model = std::make_unique<world::DataModel>();
    build_tower(st);
    st.physics = std::make_unique<physics::PhysicsWorld>(*st.model);
    st.sync = std::make_unique<world::SceneSync>(scene, *st.model);
    st.sync->update();
    scene.upload(); // new meshes and materials; later changes are transforms, which the renderer reads per frame

    const vec3 eye{-7.0f, 6.5f, 28.0f}, target{0.0f, 4.0f, 0.0f};
    const vec3 d = target - eye;
    setup.camera.position = {static_cast<f64>(eye.x), static_cast<f64>(eye.y), static_cast<f64>(eye.z)};
    setup.camera.yaw = std::atan2(-d.x, -d.z); // forward is (-sin yaw, 0, -cos yaw)
    setup.camera.pitch = std::atan2(d.y, std::sqrt(d.x * d.x + d.z * d.z));
    setup.camera.fovy = radians(42.0f);
}

void on_frame(DemoState& st) {
    st.physics->step(physics::kPhysicsStep);
    st.sync->update();
}

int run_capture(const app::RunConfig& base, const std::string& path, u32 frames, std::string& summary) {
    auto st = std::make_shared<DemoState>();
    app::RunConfig cfg = base;
    cfg.scene = "custom";
    cfg.frames = frames;
    cfg.capture_path = path;
    cfg.build_custom = [st](render::Scene& s, render::Environment& e, render::Atmosphere& a, app::SceneSetup& setup) {
        build_scene(*st, s, e, a, setup);
    };
    cfg.on_frame = [st](u64) { on_frame(*st); };
    const app::RunResult r = app::run(cfg);
    if (!r.ok) {
        TEFRA_LOG_ERROR("physics demo failed: %s", r.error.c_str());
        return 1;
    }
    u32 displaced = 0;
    for (const auto& [block, start] : st->blocks) {
        const vec3 p = block->get_vec3("Position");
        if (length(p - start) > 0.5f) ++displaced;
    }
    const vec3 ball = st->ball->get_vec3("Position");
    char line[256];
    std::snprintf(line, sizeof(line),
                  "%s: frame %u, %u bodies, %u of %u blocks moved > 0.5 m, ball at (%.1f, %.1f, %.1f)", path.c_str(),
                  frames, st->physics->body_count(), displaced, static_cast<u32>(st->blocks.size()), ball.x, ball.y,
                  ball.z);
    summary += line;
    summary += "\n";
    return r.validation_errors > 0 ? 3 : 0;
}

} // namespace

int main(int argc, char** argv) {
    CommandLine cl(argc, argv);
    if (cl.has("help")) {
        std::printf("tefra_physics_demo [options]\n"
                    "  --dir DIR               output directory (default: docs/research/m3-physics)\n");
        app::print_common_usage();
        return 0;
    }
    app::RunConfig base;
    if (!app::parse_common_options(cl, base)) return 2;
    const std::string dir = cl.get("dir", "docs/research/m3-physics");

    std::string summary;
    int status = run_capture(base, dir + "/tower_impact.png", kImpactFrame, summary);
    const int status2 = run_capture(base, dir + "/tower_settled.png", kSettledFrame, summary);
    if (status == 0) status = status2;
    std::printf("%s", summary.c_str());
    return status;
}
