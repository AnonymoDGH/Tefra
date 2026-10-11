#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest.h>

#include "tefra/anim/animator.h"
#include "tefra/anim/avatar.h"
#include "tefra/core/fs.h"
#include "tefra/core/input.h"
#include "tefra/runtime/game_runtime.h"
#include "tefra/world/datamodel.h"

#include <cmath>
#include <string>

using namespace tefra;
using namespace tefra::anim;
using namespace tefra::world;

// Imported characters (spec M3b): the DataModel side of CharacterMesh. The character is the Khronos Fox from
// assets/cache (scripts/fetch_assets.py); the test is skipped when the file is missing.

TEST_CASE("character: a skinned avatar from the Fox drives its palette through the Animator") {
    const std::string path = std::string(TEFRA_ASSET_ROOT) + "/models/characters/fox.glb";
    if (!file_exists(path.c_str())) {
        MESSAGE("assets/cache/models/characters/fox.glb is missing (run scripts/fetch_assets.py): skipped");
        return;
    }
    DataModel dm;
    SkinnedAvatarDesc d;
    d.name = "Fox";
    d.mesh_path = path;
    d.scale = 0.01f; // the Fox is authored in centimetres
    Instance* model = build_skinned_avatar(dm, dm.workspace(), d);
    REQUIRE(model != nullptr);
    Instance* root = model->find_first_child("HumanoidRootPart");
    Instance* body = model->find_first_child("Body");
    Instance* hum = model->find_first_child("Humanoid");
    REQUIRE(root != nullptr);
    REQUIRE(body != nullptr);
    REQUIRE(hum != nullptr);
    REQUIRE(hum->find_first_child("Animator") != nullptr);
    CHECK(hum->get("RigType")->as_enum() == 1); // Custom
    CHECK(body->get_string("MeshId") == path);
    const f32 hip = hum->get_float("HipHeight", 0.0f);
    CHECK(hip > 0.3f);
    CHECK(hip < 0.6f); // about half of the 0.8 m Fox

    AnimatorSystem animator(dm);
    const f32 dt = 1.0f / 60.0f;
    animator.update(dt);
    // Copies: skin_pose() points into the DataModel, which the next updates overwrite.
    const DataModel::SkinPose* first = dm.skin_pose(body->id());
    REQUIRE(first != nullptr);
    CHECK(first->version == 1);
    CHECK(first->palette.size() >= 20); // one matrix per joint of the Fox skin (24)
    const std::vector<mat4> rest = first->palette;

    // The feet sit on the ground: the MeshPart is placed HipHeight + Size.y / 2 below the root centre.
    CHECK(std::fabs(body->get_vec3("Position").y) < 1e-3f);

    // Walking at 1.4 m/s moves the joints: the palette changes after a second of simulation.
    root->set("AssemblyLinearVelocity", Value::vector3({0.0f, 0.0f, -1.4f}));
    for (int i = 0; i < 60; ++i) animator.update(dt);
    const DataModel::SkinPose* walk = dm.skin_pose(body->id());
    REQUIRE(walk != nullptr);
    CHECK(walk->version > 1);
    const u64 walk_version = walk->version;
    f32 moved = 0.0f;
    for (usize j = 0; j < rest.size(); ++j)
        moved = std::fmax(moved, std::fabs(walk->palette[j].c[3].x - rest[j].c[3].x) +
                                     std::fabs(walk->palette[j].c[3].z - rest[j].c[3].z));
    CHECK(moved > 0.005f);

    // Clips by name, and a missing name is refused.
    CHECK(animator.play_clip(*hum, "run", 0.2f));
    CHECK_FALSE(animator.play_clip(*hum, "does_not_exist"));
    CHECK(animator.is_playing(*hum, "Run"));
    for (int i = 0; i < 30; ++i) animator.update(dt);
    CHECK(dm.skin_pose(body->id())->version > walk_version);
}

TEST_CASE("character: StarterPlayer.CharacterMesh spawns the imported character in the runtime and it stands") {
    const std::string path = std::string(TEFRA_ASSET_ROOT) + "/models/characters/fox.glb";
    if (!file_exists(path.c_str())) {
        MESSAGE("assets/cache/models/characters/fox.glb is missing (run scripts/fetch_assets.py): skipped");
        return;
    }
    DataModel dm;
    Instance* ws = dm.workspace();
    Instance* floor = dm.create("Part", ws, "Floor");
    REQUIRE(floor != nullptr);
    floor->set("Size", Value::vector3({200.0f, 1.0f, 200.0f}));
    floor->set("Position", Value::vector3({0.0f, -0.5f, 0.0f}));
    floor->set("Anchored", Value::boolean(true));
    Instance* spawn = dm.create("SpawnLocation", ws, "Spawn");
    REQUIRE(spawn != nullptr);
    spawn->set("Size", Value::vector3({4.0f, 1.0f, 4.0f}));
    spawn->set("Position", Value::vector3({0.0f, -0.5f, 0.0f})); // its top is at the floor level (y = 0)
    spawn->set("Anchored", Value::boolean(true));
    Instance* starter = dm.service("StarterPlayer");
    REQUIRE(starter != nullptr);
    starter->set("CharacterMesh", Value::string(path));
    starter->set("CharacterMeshScale", Value::number(0.01));

    runtime::RuntimeConfig cfg;
    cfg.player_name = "Tester";
    runtime::GameRuntime rt(dm, cfg);
    REQUIRE(rt.start());
    Instance* model = rt.character();
    REQUIRE(model != nullptr);
    Instance* body = model->find_first_child("Body");
    REQUIRE(body != nullptr);
    CHECK(body->get_string("MeshId") == path);
    CHECK(model->find_first_child("Humanoid")->get("RigType")->as_enum() == 1);
    REQUIRE(rt.controller() != nullptr);

    InputState input;
    for (int i = 0; i < 180; ++i) rt.step(1.0f / 60.0f, input);
    CHECK(dm.skin_pose(body->id()) != nullptr); // the Animator drove the palette
    // The character settled on the floor: the mesh feet are at the floor's top (y = 0).
    CHECK(std::fabs(body->get_vec3("Position").y) < 0.05f);
    rt.stop();
}
