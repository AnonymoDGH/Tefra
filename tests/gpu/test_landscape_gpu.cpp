#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest.h>

#include "tefra/renderer/renderer.h"
#include "tefra/renderer/scene.h"
#include "tefra/rhi/bindless.h"
#include "tefra/rhi/device.h"
#include "tefra/world/datamodel.h"
#include "tefra/world/environment_sync.h"

using namespace tefra;

// Rebuilds of the landscape: a first build applies at once, an edit applies once it has settled, rebuilds reuse the
// Scene's resources, and clear() detaches the landscape from the renderer.
TEST_CASE("environment sync: the landscape builds once, settles before it rebuilds, and clears cleanly") {
    rhi::DeviceDesc dd;
    auto dev = rhi::Device::create(dd);
    REQUIRE(dev != nullptr);
    rhi::BindlessHeap heap(*dev, 2);
    render::Scene scene(*dev, heap);
    render::Renderer renderer(*dev, heap, 2);
    world::DataModel dm;
    world::Instance* t = dm.create("Terrain", dm.workspace(), "Terrain");
    t->set("Kind", world::Value::enumeration(1)); // Valley
    t->set("Seed", world::Value::integer(1337));

    world::EnvironmentSync env(*dev, heap, scene, renderer);
    CHECK_FALSE(env.has_terrain());
    CHECK(env.update(dm, 8)); // the first build applies at once
    CHECK(env.has_terrain());
    CHECK(env.kind() == render::TerrainKind::Valley);
    CHECK(renderer.landscape().terrain != nullptr);
    const u64 revision = env.terrain_revision();
    const usize meshes = scene.meshes().size();
    CHECK_FALSE(env.update(dm, 8)); // nothing changed

    // A value dragged every frame does not rebuild while it moves ...
    for (int i = 0; i < 10; ++i) {
        t->set("Seed", world::Value::integer(100 + i));
        CHECK_FALSE(env.update(dm, 8));
    }
    CHECK(env.terrain_revision() == revision);
    // ... and rebuilds once it has rested for 8 consecutive frames (the change itself counts as the first).
    for (int i = 1; i < 7; ++i) CHECK_FALSE(env.update(dm, 8));
    CHECK(env.update(dm, 8));
    CHECK(env.terrain_revision() == revision + 1);
    // The rebuilt landscape reuses the cached assets: the Scene gains no meshes.
    CHECK(scene.meshes().size() == meshes);

    // An Ocean is a part of the landscape too; removing the Terrain removes the landscape at once.
    world::Instance* ocean = dm.create("Ocean", dm.workspace(), "Ocean");
    CHECK(env.update(dm, 1));
    CHECK(env.has_ocean());
    CHECK(dm.destroy(ocean));
    CHECK(env.update(dm, 1));
    CHECK_FALSE(env.has_ocean());
    t->set("Kind", world::Value::enumeration(0)); // None: no landscape
    CHECK(env.update(dm, 8));
    CHECK_FALSE(env.has_terrain());
    CHECK(renderer.landscape().terrain == nullptr);

    // Back to a Coast, then clear(): the renderer holds nothing before the objects go away.
    t->set("Kind", world::Value::enumeration(2));
    CHECK(env.update(dm, 1));
    CHECK(env.kind() == render::TerrainKind::Coast);
    env.clear();
    CHECK_FALSE(env.has_terrain());
    CHECK(renderer.landscape().terrain == nullptr);
    CHECK(renderer.landscape().vegetation == nullptr);
}
