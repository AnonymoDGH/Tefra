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
    // Base folder of relative MeshId paths (the project folder; empty = working directory).
    void set_asset_root(const std::string& dir) {
        if (dir != asset_root_) structure_dirty_ = true;
        asset_root_ = dir;
    }

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
    void sync_lights();
    void write_transform(const Instance& part, u32 render_index);

    render::Scene& scene_;
    DataModel* model_;
    usize listener_ = 0;
    bool structure_dirty_ = true;
    std::string asset_root_;
    std::vector<u64> light_ids_;
    u64 lights_revision_ = ~0ull;
    bool materials_dirty_ = false;
    std::vector<u64> dirty_parts_;
    std::vector<PartEntry> parts_;
    std::unordered_map<u64, u32> part_index_; // instance id -> parts_ index
    u32 primitive_mesh_[4] = {~0u, ~0u, ~0u, ~0u};
    std::unordered_map<std::string, u32> material_cache_;
    // A glTF primitive as render instances: mesh, material and (for skinned primitives) scene skin.
    struct GltfPrim {
        u32 mesh = 0;
        u32 material = 0;
        u32 skin = 0xFFFFFFFFu;
    };
    // A skinned MeshPart owns its own skins (its palette comes from DataModel::skin_pose).
    struct SkinnedPart {
        std::string path;
        std::vector<GltfPrim> prims;
        u64 seen_version = 0;
    };
    std::unordered_map<std::string, std::vector<GltfPrim>> gltf_cache_; // static files: path -> primitives
    std::unordered_map<u64, SkinnedPart> skinned_parts_;                // MeshPart id -> skinned primitives
    bool uploaded_once_ = false;
};

} // namespace tefra::world
