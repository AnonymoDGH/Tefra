// tefra_avatar_demo: three procedural R15 avatars (idle, walk, run) on a ground under the physical sky (spec M3,
// ADR-016). The avatars live in a DataModel; SceneSync draws them and AnimatorSystem drives their rigs.
//
//   tefra_avatar_demo --time 0.45 --capture out.png   render the frame at simulation time 0.45 s
//   tefra_avatar_demo --emote wave                    also plays a built-in clip on the walking avatar
//   tefra_avatar_demo --export-clips DIR              write the built-in clips as .tefraanim files
//   tefra_avatar_demo --character FILE.glb           the middle avatar is an imported skinned character (walking)
//   tefra_avatar_demo --character FILE.glb --character-scale S
#include "options.h"
#include "runner.h"
#include "scenes.h"

#include "tefra/anim/animator.h"
#include "tefra/anim/avatar.h"
#include "tefra/anim/clip.h"
#include "tefra/core/cli.h"
#include "tefra/core/fs.h"
#include "tefra/core/log.h"
#include "tefra/renderer/scene.h"
#include "tefra/world/datamodel.h"
#include "tefra/world/scene_sync.h"

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

using namespace tefra;

namespace {

constexpr f32 kSimStep = 1.0f / 60.0f;

struct DemoState {
    world::DataModel dm;
    std::unique_ptr<world::SceneSync> sync;
    std::unique_ptr<anim::AnimatorSystem> animator;
    std::string emote;     // built-in clip played on top of the walking avatar (optional)
    std::string character; // rigged .glb that replaces the middle avatar (--character)
    f32 character_scale = 1.0f;
};

// Sets up a root velocity and a Humanoid state for a character that is "walking" in place.
void set_motion(world::Instance* model, f32 speed_mps, u32 humanoid_state) {
    world::Instance* root = model->find_first_child("HumanoidRootPart");
    world::Instance* hum = model->find_first_child("Humanoid");
    if (root) root->set("AssemblyLinearVelocity", world::Value::vector3({0.0f, 0.0f, -speed_mps}));
    if (hum) hum->set("State", world::Value::enumeration(humanoid_state), true);
}

void build_demo_scene(DemoState& s, render::Scene& scene, render::Environment& env, const render::Atmosphere& atmo,
                      app::SceneSetup& setup) {
    setup.use_atmosphere = true;
    setup.use_terrain = false;
    setup.settings.sun_direction = normalize(vec3{0.42f, 0.64f, -0.64f}); // ~40 deg elevation, in front of the cast
    setup.settings.ev100 = 14.0f;
    setup.settings.fog_density = 0.0015f;
    setup.settings.fog_falloff = 0.05f;
    setup.settings.cascades = 3;
    setup.settings.shadow_distance = 40.0f;
    setup.settings.grade_saturation = 1.1f;
    app::build_atmosphere_environment(atmo, env, setup.settings.sun_direction);
    setup.camera.position = {0.0, 1.35, -7.0}; // in front of the avatars, which face -Z
    setup.camera.yaw = kPi;                    // look along +Z, towards them
    setup.camera.pitch = 0.0f;
    if (!s.character.empty()) {
        // Close view of the imported character (about 0.8 m tall at the default scale 0.01).
        setup.camera.position = {0.0, 0.55, -2.3};
        setup.camera.pitch = -0.1f;
    }

    // Ground: a plain anchored slab under the characters.
    world::Instance* ground = s.dm.create("Part", s.dm.workspace(), "Ground");
    ground->set("Size", world::Value::vector3({80.0f, 0.2f, 80.0f}));
    ground->set("Position", world::Value::vector3({0.0f, -0.1f, 0.0f}));
    ground->set("Color", world::Value::color3({0.30f, 0.36f, 0.22f}));
    ground->set("Material", world::Value::enumeration(13)); // Grass
    ground->set("Anchored", world::Value::boolean(true));

    // Three presets, three speeds: idle, walk (1.4 m/s) and run (5 m/s), side by side.
    struct Cast {
        anim::AvatarPreset preset;
        f32 x;
        f32 speed;
        u32 state;
    };
    const Cast cast[] = {{anim::AvatarPreset::Explorer, -1.6f, 0.0f, 0},
                         {anim::AvatarPreset::Stocky, 0.0f, 1.4f, 1},
                         {anim::AvatarPreset::Lanky, 1.6f, 5.0f, 2}};
    for (const Cast& c : cast) {
        if (!s.character.empty() && c.x != 0.0f) continue; // with --character the view shows only that character
        if (c.x == 0.0f && !s.character.empty()) {
            // The imported character walks in the middle, under the same root and Humanoid as the R15 avatars.
            anim::SkinnedAvatarDesc sd;
            sd.name = "Imported";
            sd.mesh_path = s.character;
            sd.scale = s.character_scale;
            sd.origin = {c.x, 0.0f, 0.0f};
            if (world::Instance* model = anim::build_skinned_avatar(s.dm, s.dm.workspace(), sd))
                set_motion(model, c.speed, c.state);
            else
                TEFRA_LOG_WARN("cannot load a skinned character from %s", s.character.c_str());
            continue;
        }
        anim::AvatarDesc d = anim::AvatarDesc::preset(c.preset);
        d.origin = {c.x, 0.0f, 0.0f};
        world::Instance* model = anim::build_r15_avatar(s.dm, s.dm.workspace(), d);
        if (model) set_motion(model, c.speed, c.state);
    }

    s.sync = std::make_unique<world::SceneSync>(scene, s.dm);
    s.sync->update();
    s.animator = std::make_unique<anim::AnimatorSystem>(s.dm);
    if (!s.emote.empty()) {
        // The walking avatar is the second one built (see the cast above).
        int index = 0;
        for (world::Instance* c : s.dm.workspace()->children()) {
            if (c->name() != "Character" || index++ != 1) continue;
            if (world::Instance* hum = c->find_first_child("Humanoid"))
                if (!s.animator->play_clip(*hum, s.emote, 0.2f)) s.animator->play(*hum, s.emote, 0.2f);
        }
    }
}

int export_clips(const char* dir) {
    make_dirs(dir);
    for (const std::string& name : anim::builtin_clip_names()) {
        const anim::ClipPtr clip = anim::builtin_clip(name);
        const std::string path = path_join(dir, name + ".tefraanim");
        std::string error;
        if (!clip->save(path.c_str(), &error)) {
            TEFRA_LOG_ERROR("cannot write %s: %s", path.c_str(), error.c_str());
            return 1;
        }
        std::printf("%s\n", path.c_str());
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    CommandLine cl(argc, argv);
    if (cl.has("help")) {
        std::printf("tefra_avatar_demo [options]\n"
                    "  --time SECONDS          simulation time of the captured frame (default 0.45)\n"
                    "  --export-clips DIR      write the built-in clips as .tefraanim and exit\n");
        app::print_common_usage();
        return 0;
    }
    if (const char* dir = cl.get("export-clips")) return export_clips(dir);

    const f64 seconds = cl.get_float("time", 0.45);
    const char* emote = cl.get("emote");
    app::RunConfig cfg;
    cfg.scene = "avatars";
    cfg.no_taa = true; // no temporal blending: the limbs stay sharp while they move
    cfg.capture_path = "avatar_demo.png";
    if (!app::parse_common_options(cl, cfg)) return 2;
    cfg.frames = static_cast<u32>(std::lround(seconds / kSimStep)) + 1; // the last frame is at `seconds`

    auto state = std::make_shared<DemoState>();
    if (emote) state->emote = emote;
    if (const char* character = cl.get("character")) state->character = character;
    state->character_scale = static_cast<f32>(cl.get_float("character-scale", 1.0));
    auto custom = std::make_shared<app::CustomScene>();
    custom->build = [state](render::Scene& scene, render::Environment& env, const render::Atmosphere& atmo,
                            app::SceneSetup& setup) { build_demo_scene(*state, scene, env, atmo, setup); };
    custom->frame = [state](u64 frame_index) {
        if (frame_index > 0) state->animator->update(kSimStep);
        state->sync->update();
    };
    cfg.custom_scene = custom;

    const app::RunResult r = app::run(cfg);
    if (!r.ok) {
        TEFRA_LOG_ERROR("avatar demo failed: %s", r.error.c_str());
        return 1;
    }
    if (r.validation_errors > 0) {
        TEFRA_LOG_ERROR("%u Vulkan validation errors", r.validation_errors);
        return 3;
    }
    return 0;
}
