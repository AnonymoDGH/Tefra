#pragma once

// DataModel (spec M3, ADR-012): a Roblox-style tree of instances with reflected, typed properties. One description
// of every class and property serves the editor (Properties panel), serialization (.tefra), C# bindings and the MCP
// tools. No RTTI: a ClassInfo pointer is the type; is_a() walks the base chain.

#include "tefra/core/json_value.h"
#include "tefra/core/math.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace tefra::world {

enum class PropType : u8 { Bool, Int, Float, String, Vector3, Color3, Enum, Ref };
const char* prop_type_name(PropType t);

// Property value. Bool/Int/Float/Enum use n[0]; Vector3/Color3 use n[0..2]; String uses s; Ref uses ref (0 = nil).
struct Value {
    PropType type = PropType::Bool;
    f64 n[3] = {0, 0, 0};
    std::string s;
    u64 ref = 0;

    static Value boolean(bool b) {
        Value v;
        v.type = PropType::Bool;
        v.n[0] = b ? 1 : 0;
        return v;
    }
    static Value integer(i64 i) {
        Value v;
        v.type = PropType::Int;
        v.n[0] = f64(i);
        return v;
    }
    static Value number(f64 f) {
        Value v;
        v.type = PropType::Float;
        v.n[0] = f;
        return v;
    }
    static Value string(std::string str) {
        Value v;
        v.type = PropType::String;
        v.s = std::move(str);
        return v;
    }
    static Value vector3(vec3 p) {
        Value v;
        v.type = PropType::Vector3;
        v.n[0] = p.x;
        v.n[1] = p.y;
        v.n[2] = p.z;
        return v;
    }
    static Value color3(vec3 c) {
        Value v;
        v.type = PropType::Color3;
        v.n[0] = c.x;
        v.n[1] = c.y;
        v.n[2] = c.z;
        return v;
    }
    static Value enumeration(u32 e) {
        Value v;
        v.type = PropType::Enum;
        v.n[0] = f64(e);
        return v;
    }
    static Value reference(u64 id) {
        Value v;
        v.type = PropType::Ref;
        v.ref = id;
        return v;
    }

    bool as_bool() const { return n[0] != 0.0; }
    i64 as_int() const { return static_cast<i64>(n[0]); }
    f32 as_float() const { return static_cast<f32>(n[0]); }
    u32 as_enum() const { return static_cast<u32>(n[0]); }
    vec3 as_vec3() const { return {f32(n[0]), f32(n[1]), f32(n[2])}; }
    bool operator==(const Value& o) const;
};

struct EnumInfo {
    std::string name;
    std::vector<std::string> items;
    i32 index_of(std::string_view item) const;
};

enum PropFlags : u32 {
    kPropReadOnly = 1u << 0,      // not editable in the Properties panel / by scripts
    kPropHidden = 1u << 1,        // not shown in the Properties panel (e.g. Script.Source has its own editor)
    kPropNotSerialized = 1u << 2, // runtime state (e.g. Humanoid state)
};

struct PropInfo {
    std::string name;
    PropType type = PropType::Bool;
    Value def;
    const EnumInfo* enum_info = nullptr;
    u32 flags = 0;
    std::string category; // Properties panel group ("Data", "Appearance", "Behavior", ...)
    std::string doc;
    u32 index = 0; // global index in the instance's property array (base properties first)
};

struct ClassInfo {
    std::string name;
    const ClassInfo* base = nullptr;
    std::vector<PropInfo> props; // own properties only
    u32 first_prop = 0;          // = base->prop_count()
    bool creatable = true;       // Instance.new / Insert menu
    bool service = false;        // singleton child of the DataModel
    std::string category;        // Insert menu group
    std::string doc;

    u32 prop_count() const { return first_prop + static_cast<u32>(props.size()); }
    const PropInfo* find_prop(std::string_view name) const; // searches the base chain
    const PropInfo* prop(u32 index) const;                  // global index
    bool is_a(const ClassInfo* other) const;
};

// Class registry (built-in classes are registered on first use).
class ClassRegistry {
  public:
    static ClassRegistry& get();
    const ClassInfo* find(std::string_view name) const;
    const std::vector<const ClassInfo*>& all() const { return order_; }
    const EnumInfo* find_enum(std::string_view name) const;
    // Registration API (used by classes.cpp; engine modules may add their own classes).
    ClassInfo& add(std::string name, const char* base);
    const EnumInfo* add_enum(std::string name, std::vector<std::string> items);
    void finalize(); // assigns property indices

  private:
    ClassRegistry();
    std::vector<std::unique_ptr<ClassInfo>> classes_;
    std::vector<const ClassInfo*> order_;
    std::vector<std::unique_ptr<EnumInfo>> enums_;
};

class DataModel;

class Instance {
  public:
    u64 id() const { return id_; }
    const ClassInfo& cls() const { return *cls_; }
    const std::string& class_name() const { return cls_->name; }
    bool is_a(std::string_view class_name) const;
    bool is_a(const ClassInfo* c) const { return cls_->is_a(c); }

    const std::string& name() const { return props_[0].s; }
    void set_name(std::string n) { set(0, Value::string(std::move(n))); }
    Instance* parent() const { return parent_; }
    const std::vector<Instance*>& children() const { return children_; }
    DataModel& model() const { return *model_; }

    const Value& get(u32 index) const { return props_[index]; }
    const Value* get(std::string_view prop) const;
    // Type-checked: returns false if the property does not exist, is read-only (unless force) or the type differs.
    bool set(u32 index, Value v, bool force = false);
    bool set(std::string_view prop, Value v, bool force = false);

    Instance* find_first_child(std::string_view name) const;
    Instance* find_first_child_of_class(std::string_view class_name) const;
    std::vector<Instance*> descendants() const;
    bool is_descendant_of(const Instance* ancestor) const;
    std::string full_name() const; // "Workspace.Model.Part"

    // Convenience accessors by property name for common engine fields.
    vec3 get_vec3(std::string_view prop, vec3 def = {}) const;
    f32 get_float(std::string_view prop, f32 def = 0.0f) const;
    bool get_bool(std::string_view prop, bool def = false) const;
    const std::string& get_string(std::string_view prop) const;

  private:
    friend class DataModel;
    Instance() = default;
    u64 id_ = 0;
    const ClassInfo* cls_ = nullptr;
    DataModel* model_ = nullptr;
    Instance* parent_ = nullptr;
    std::vector<Instance*> children_;
    std::vector<Value> props_;
};

// Change events for engine systems (renderer bridge, physics, scripting, editor).
struct WorldListener {
    std::function<void(Instance&)> added;             // after the instance is parented into the tree
    std::function<void(Instance&)> removing;          // before it leaves the tree (descendants first)
    std::function<void(Instance&, u32 prop)> changed; // property changed (index); Name = 0
    std::function<void(Instance&, Instance* old_parent)> reparented;
};

class DataModel {
  public:
    DataModel();
    ~DataModel();
    DataModel(const DataModel&) = delete;
    DataModel& operator=(const DataModel&) = delete;

    Instance& root() { return *root_; }
    const Instance& root() const { return *root_; }
    Instance* service(std::string_view class_name) const; // "Workspace", "Lighting", ...
    Instance* workspace() const { return service("Workspace"); }

    // Creates an instance of a creatable class (nullptr for unknown / non-creatable / services). Parent may be null
    // (detached; it is destroyed with the model unless parented).
    Instance* create(std::string_view class_name, Instance* parent = nullptr, std::string name = {});
    // Destroys the instance and its descendants. Services and the root cannot be destroyed.
    bool destroy(Instance* inst);
    bool set_parent(Instance* inst, Instance* parent);
    Instance* clone(const Instance& src, Instance* parent); // deep copy with fresh ids; internal refs remapped

    Instance* find(u64 id) const;
    Instance* find_path(std::string_view path) const; // "Workspace.Model.Part" (root name optional)

    // Serialization (.tefra JSON). load() replaces everything under the services; returns false with an error.
    JsonValue to_json() const;
    bool from_json(const JsonValue& doc, std::string* error = nullptr);
    bool save(const char* path) const;
    bool load(const char* path, std::string* error = nullptr);
    // Single instance subtree (for clipboard / MCP).
    JsonValue instance_to_json(const Instance& inst, bool with_children = true) const;
    Instance* instance_from_json(const JsonValue& j, Instance* parent, std::string* error = nullptr);

    u64 revision() const { return revision_; } // bumped by every structural or property change
    usize add_listener(WorldListener l);
    void remove_listener(usize handle);

    // Value <-> JSON (by property type).
    static JsonValue value_to_json(const PropInfo& p, const Value& v);
    static bool value_from_json(const PropInfo& p, const JsonValue& j, Value& out);

  private:
    friend class Instance;
    Instance* make(const ClassInfo* cls, u64 id);
    void attach(Instance* inst, Instance* parent);
    void detach(Instance* inst);
    void notify_changed(Instance& inst, u32 prop);
    void notify_added(Instance& inst);
    void notify_removing(Instance& inst);
    void clear_services();

    std::unordered_map<u64, std::unique_ptr<Instance>> instances_;
    Instance* root_ = nullptr;
    u64 next_id_ = 1;
    u64 revision_ = 0;
    std::vector<std::unique_ptr<WorldListener>> listeners_;
};

// Rotation of a part from its Orientation property (degrees, Roblox order: Y, then X, then Z).
quat orientation_to_quat(vec3 degrees_yxz);
vec3 quat_to_orientation(quat q);

} // namespace tefra::world
