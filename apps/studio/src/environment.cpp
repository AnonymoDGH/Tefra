// Landscape in the editor (M3b): keeps the Workspace Terrain and Ocean rendered (world::EnvironmentSync) and gives
// the render settings the look of the bench scenes "valley" and "coast". It runs after update_lighting(), which
// owns the sun, exposure, clouds and the grade from the Lighting service; the landscape only sets what depends on its
// geometry (fog height, shadows, canopy occlusion, grass), and fog density only when Lighting.FogDensity is zero.

#include "tefra/studio/app.h"

#include "tefra/renderer/renderer.h"
#include "tefra/renderer/scene.h"
#include "tefra/rhi/bindless.h"
#include "tefra/rhi/device.h"
#include "tefra/world/datamodel.h"
#include "tefra/world/environment_sync.h"

namespace tefra::studio {

namespace {

// The editor's own values for the fields a landscape changes (App::init), used again when the place has no Terrain.
void restore_editor_settings(render::RenderSettings& s) {
    const render::RenderSettings def{};
    s.cascades = 3;
    s.shadow_distance = 160.0f;
    s.grass = false;
    s.canopy_occlusion = def.canopy_occlusion;
    s.volumetric_fog = def.volumetric_fog;
    s.fog_height = def.fog_height;
    s.fog_anisotropy = def.fog_anisotropy;
    s.fog_falloff = def.fog_falloff;
}

} // namespace

void App::sync_environment() {
    if (!environment_) environment_ = std::make_shared<world::EnvironmentSync>(*device_, *heap_, *scene_, *renderer_);
    world::EnvironmentSync& env = *std::static_pointer_cast<world::EnvironmentSync>(environment_);
    const bool rebuilt = env.update(session_.model()); // rebuilds only when a Terrain or Ocean property changed
    if (!env.has_terrain()) {
        restore_editor_settings(settings_);
        return;
    }
    // A landscape that rose over the editor camera (a new place, a raised Terrain): look at it from above.
    const f32 cx = static_cast<f32>(camera_.position.x), cz = static_cast<f32>(camera_.position.z);
    if (rebuilt && static_cast<f32>(camera_.position.y) < env.height_at(cx, cz) + 2.0f) {
        const f32 ground = env.height_at(0.0f, 80.0f);
        camera_.position = {0.0, static_cast<f64>(ground) + 40.0, 80.0};
        camera_.yaw = 0.0f;
        camera_.pitch = radians(-20.0f);
    }
    const world::Instance* lighting = session_.model().service("Lighting");
    const bool lighting_fog = lighting && lighting->get_float("FogDensity", 0.0f) > 0.0f;
    if (env.kind() == render::TerrainKind::Coast) {
        // bench "coast": sea haze over the water, no grass or canopy occlusion on the beach.
        settings_.cascades = 3;
        settings_.shadow_distance = 150.0f;
        settings_.grass = false;
        settings_.canopy_occlusion = false;
        settings_.volumetric_fog = false;
        settings_.fog_height = env.sea_level();
        settings_.fog_anisotropy = 0.7f;
        if (!lighting_fog) {
            settings_.fog_density = 0.000012f;
            settings_.fog_falloff = 0.03f;
        }
    } else {
        // bench "valley": forest haze in a layer at the valley floor, grass and canopy occlusion, volumetric light.
        settings_.cascades = 3;
        settings_.shadow_distance = 200.0f;
        settings_.grass = true;
        settings_.canopy_occlusion = true;
        settings_.volumetric_fog = true;
        settings_.fog_height = env.height_at(0.0f, 0.0f);
        settings_.fog_anisotropy = 0.6f;
        if (!lighting_fog) {
            settings_.fog_density = 0.008f;
            settings_.fog_falloff = 0.035f;
        }
    }
}

} // namespace tefra::studio
