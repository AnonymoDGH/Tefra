#pragma once

// Bridge DataModel -> renderer: every visible BasePart in the Workspace becomes a render::Instance (unit primitive
// mesh scaled by Size, or a glTF mesh for MeshPart) with a material derived from Color / Material / Reflectance.
// Changes are collected through a WorldListener and applied in update().

#include "tefra/world/datamodel.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace tefra::render {
class Scene;
} // namespace tefra::render

namespace tefra::world {

struct RayHit {
    Instance* instance = nullptr;
    f32 distance = 0.0f;
    vec3 position{};
    vec3 normal{};
};

class SceneSync {
  public:
    SceneSync(render::Scene& scene, DataModel& model);
    ~SceneSync();
    SceneSync(const SceneSync&) = delete;
    SceneSync& operator=(const SceneSync&) = delete;

    // Switches to another DataModel (e.g. the editor restored the place after play mode). Meshes and materials
    // already in the render scene are reused.
    void set_model(DataModel& model);
    DataModel& model() const { return *model_; }

    // Applies pending changes (call once per frame before rendering). Returns true if the render scene changed.
    bool update();
    void mark_all_dirty() { structure_dirty_ = true; }

    // Closest BasePart hit by the ray (world space). ignore: skipped (e.g. the dragged part).
    RayHit raycast(vec3 origin, vec3 dir, f32 max_distance = 1e4f, const Instance* ignore = nullptr) const;
    // World-space bounding box of a part or model (false if it has no parts).
    bool bounds(const Instance& inst, vec3& lo, vec3& hi) const;

  private:
    struct PartEntry {
        Instance* inst;
        u32 render_index;
    };
    void attach_listener();
    u32 material_for(const Instance& part);
    u32 mesh_for(const Instance& part);
    void rebuild();
    void write_transform(const Instance& part, u32 render_index);

    render::Scene& scene_;
    DataModel* model_;
    usize listener_ = 0;
    bool structure_dirty_ = true;
    bool materials_dirty_ = false;
    std::vector<u64> dirty_parts_;
    std::vector<PartEntry> parts_;
    std::unordered_map<u64, u32> part_index_; // instance id -> parts_ index
    u32 primitive_mesh_[4] = {~0u, ~0u, ~0u, ~0u};
    std::unordered_map<std::string, u32> material_cache_;
    std::unordered_map<std::string, std::vector<std::pair<u32, u32>>> gltf_cache_; // path -> (mesh, material)
    bool uploaded_once_ = false;
};

} // namespace tefra::world
