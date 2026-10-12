#include "tefra/mcp/world_tools.h"

#include "tefra/core/fs.h"
#include "tefra/mcp/server.h"
#include "tefra/world/datamodel.h"

#include <cctype>
#include <cmath>
#include <initializer_list>
#include <set>
#include <string>
#include <vector>

namespace tefra::mcp {

namespace {

using world::ClassInfo;
using world::ClassRegistry;
using world::DataModel;
using world::Instance;
using world::PropInfo;
using world::PropType;
using world::Value;

constexpr usize kNpos = std::string::npos;
constexpr usize kMaxScriptBytes = 1024 * 1024;
constexpr usize kMaxResults = 1000;
constexpr int kMaxTreeNodes = 2000;

// ---- Small helpers ------------------------------------------------------------------------------------------

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool has_arg(const JsonValue& args, const char* key) {
    const JsonValue* v = args.find(key);
    return v && !v->is_null();
}

std::string str_arg(const JsonValue& args, const char* key) {
    const JsonValue* v = args.find(key);
    return v && v->is_string() ? v->as_string() : std::string();
}

bool bool_arg(const JsonValue& args, const char* key, bool def) {
    const JsonValue* v = args.find(key);
    return v && v->is_bool() ? v->as_bool() : def;
}

int int_arg(const JsonValue& args, const char* key, int def, int lo, int hi) {
    const JsonValue* v = args.find(key);
    if (!v || !v->is_number()) return def;
    const i64 n = v->as_int();
    if (n < lo) return lo;
    if (n > hi) return hi;
    return static_cast<int>(n);
}

std::string path_of(const Instance& inst) { return inst.parent() ? inst.full_name() : std::string("game"); }

// "game" and "" are the DataModel itself; everything else goes through DataModel::find_path.
Instance* resolve_path(DataModel& dm, std::string_view path) {
    if (path.empty() || path == "game") return &dm.root();
    return dm.find_path(path);
}

// Instance from a path argument or an id argument (key names are parameters: parent_path/parent_id, ...).
Instance* find_arg(DataModel& dm, const JsonValue& args, const char* path_key, const char* id_key, std::string& error) {
    const bool has_path = has_arg(args, path_key);
    const bool has_id = has_arg(args, id_key);
    if (has_path && has_id) {
        error = std::string("give '") + path_key + "' or '" + id_key + "', not both";
        return nullptr;
    }
    if (has_id) {
        const JsonValue* id = args.find(id_key);
        if (!id->is_number()) {
            error = std::string("'") + id_key + "' must be an integer id";
            return nullptr;
        }
        Instance* inst = dm.find(static_cast<u64>(id->as_int()));
        if (!inst)
            error = "no instance with id " + std::to_string(id->as_int()) +
                    " (it was deleted, or the id is wrong). Use find or get_tree to get current ids";
        return inst;
    }
    if (has_path) {
        const JsonValue* path = args.find(path_key);
        if (!path->is_string()) {
            error = std::string("'") + path_key + "' must be a string";
            return nullptr;
        }
        Instance* inst = resolve_path(dm, path->as_string());
        if (!inst)
            error = "no instance at path '" + path->as_string() +
                    "'. Paths look like Workspace.Model.Part; use get_tree or find to list them";
        return inst;
    }
    error = std::string("missing '") + path_key + "' or '" + id_key + "'";
    return nullptr;
}

std::string name_problem(const std::string& name) {
    if (name.empty()) return "a name cannot be empty";
    if (name.find('.') != kNpos) return "names cannot contain '.' (paths use it as the separator)";
    return {};
}

std::string describe_json(const JsonValue& v) {
    switch (v.type()) {
    case JsonValue::Type::Null: return "null";
    case JsonValue::Type::Bool: return v.as_bool() ? "true" : "false";
    case JsonValue::Type::Number: return v.dump();
    case JsonValue::Type::String: return "\"" + v.as_string() + "\"";
    case JsonValue::Type::Array: return "an array";
    case JsonValue::Type::Object: return "an object";
    }
    return "?";
}

std::string expected_for(const PropInfo& p) {
    switch (p.type) {
    case PropType::Bool: return "true or false";
    case PropType::Int: return "an integer";
    case PropType::Float: return "a number";
    case PropType::String: return "a string";
    case PropType::Vector3: return "an array [x, y, z] of 3 numbers";
    case PropType::Color3: return "an array [r, g, b] of 3 numbers between 0 and 1";
    case PropType::Enum: {
        std::string out = "one of";
        if (p.enum_info)
            for (usize i = 0; i < p.enum_info->items.size(); ++i)
                out += (i ? ", \"" : " \"") + p.enum_info->items[i] + "\"";
        return out;
    }
    case PropType::Ref: return "an instance path (string), an instance id (integer) or null";
    }
    return "a valid value";
}

// Error result that lists every problem; nothing was changed when it is returned.
ToolResult problems_error(const std::string& headline, const std::vector<std::string>& problems) {
    std::string text = headline;
    for (const std::string& p : problems) text += "\n- " + p;
    return ToolResult::error(text);
}

// ---- Property assignments -----------------------------------------------------------------------------------

struct Assignment {
    const PropInfo* prop = nullptr;
    Value value;
};

// Checks property assignments for cls without touching the model. Every problem is collected, so the caller can
// report them together. Returns true only when all of them are valid.
bool convert_properties(DataModel& dm, const ClassInfo& cls, const JsonValue& props, std::vector<Assignment>& out,
                        std::vector<std::string>& problems) {
    if (!props.is_object()) {
        problems.push_back("'properties' must be an object such as {\"Anchored\": true}");
        return false;
    }
    const usize before = problems.size();
    for (const auto& [key, json] : props.members()) {
        const std::string where = cls.name + "." + key;
        const PropInfo* p = cls.find_prop(key);
        if (!p) {
            problems.push_back("unknown property '" + where + "'. Use list_classes to see the properties of " +
                               cls.name);
            continue;
        }
        if (p->flags & world::kPropReadOnly) {
            problems.push_back("'" + where + "' is read-only");
            continue;
        }
        Value v;
        if (p->type == PropType::Ref && json.is_string()) {
            if (json.as_string().empty()) {
                v = Value::reference(0);
            } else {
                Instance* target = resolve_path(dm, json.as_string());
                if (!target) {
                    problems.push_back("'" + where + "': no instance at path '" + json.as_string() + "'");
                    continue;
                }
                v = Value::reference(target->id());
            }
        } else if (!DataModel::value_from_json(*p, json, v)) {
            problems.push_back("'" + where + "' expects " + expected_for(*p) + ", got " + describe_json(json));
            continue;
        } else if (p->type == PropType::Int && std::floor(json.as_number()) != json.as_number()) {
            problems.push_back("'" + where + "' expects an integer, got " + json.dump());
            continue;
        }
        if (p->type == PropType::Ref && v.ref != 0 && !dm.find(v.ref)) {
            problems.push_back("'" + where + "': no instance with id " + std::to_string(v.ref));
            continue;
        }
        if (p->index == 0) {
            const std::string problem = name_problem(v.s);
            if (!problem.empty()) {
                problems.push_back("'" + where + "': " + problem);
                continue;
            }
        }
        out.push_back(Assignment{p, std::move(v)});
    }
    return problems.size() == before;
}

// Applies validated assignments and returns the resulting values as a JSON object.
JsonValue apply_assignments(Instance& inst, const std::vector<Assignment>& list) {
    JsonValue updated = JsonValue::object();
    for (const Assignment& a : list) {
        inst.set(a.prop->index, a.value);
        updated.set(a.prop->name, DataModel::value_to_json(*a.prop, inst.get(a.prop->index)));
    }
    return updated;
}

// ---- Descriptions -------------------------------------------------------------------------------------------

// Compact view of an instance: id, class, name, path, parent id. With full set: properties and children too.
JsonValue describe(DataModel& dm, const Instance& inst, bool full) {
    JsonValue j = JsonValue::object();
    j.set("id", inst.id());
    j.set("class", inst.class_name());
    j.set("name", inst.name());
    j.set("path", path_of(inst));
    j.set("parent_id", inst.parent() ? JsonValue(inst.parent()->id()) : JsonValue());
    if (!full) return j;

    JsonValue props = JsonValue::object();
    for (u32 i = 0; i < inst.cls().prop_count(); ++i) {
        const PropInfo* p = inst.cls().prop(i);
        JsonValue pj = JsonValue::object();
        pj.set("type", world::prop_type_name(p->type));
        pj.set("value", DataModel::value_to_json(*p, inst.get(i)));
        if (p->type == PropType::Ref && inst.get(i).ref != 0) {
            const Instance* target = dm.find(inst.get(i).ref);
            if (target) pj.set("target_path", path_of(*target));
        }
        if (p->flags & world::kPropReadOnly) pj.set("read_only", true);
        props.set(p->name, std::move(pj));
    }
    j.set("properties", std::move(props));

    JsonValue attrs = JsonValue::object();
    for (const auto& [name, v] : inst.attributes()) attrs.set(name, DataModel::attribute_to_json(v));
    j.set("attributes", std::move(attrs));
    JsonValue tags = JsonValue::array();
    for (const std::string& t : inst.tags()) tags.push(JsonValue(t));
    j.set("tags", std::move(tags));

    JsonValue kids = JsonValue::array();
    for (const Instance* c : inst.children()) {
        JsonValue cj = JsonValue::object();
        cj.set("id", c->id());
        cj.set("name", c->name());
        cj.set("class", c->class_name());
        kids.push(std::move(cj));
    }
    j.set("children", std::move(kids));
    return j;
}

JsonValue describe_class(const ClassInfo& c) {
    JsonValue j = JsonValue::object();
    j.set("name", c.name);
    if (c.base) j.set("base", c.base->name);
    j.set("doc", c.doc);
    j.set("category", c.category);
    j.set("creatable", c.creatable);
    if (c.service) j.set("service", true);
    JsonValue props = JsonValue::array();
    for (u32 i = 0; i < c.prop_count(); ++i) {
        const PropInfo* p = c.prop(i);
        JsonValue pj = JsonValue::object();
        pj.set("name", p->name);
        pj.set("type", world::prop_type_name(p->type));
        pj.set("default", DataModel::value_to_json(*p, p->def));
        if (p->enum_info) {
            JsonValue options = JsonValue::array();
            for (const std::string& item : p->enum_info->items) options.push(item);
            pj.set("options", std::move(options));
        }
        if (p->flags & world::kPropReadOnly) pj.set("read_only", true);
        if (p->flags & world::kPropHidden) pj.set("hidden", true);
        if (p->flags & world::kPropNotSerialized) pj.set("not_serialized", true);
        if (!p->category.empty()) pj.set("category", p->category);
        pj.set("doc", p->doc);
        props.push(std::move(pj));
    }
    j.set("properties", std::move(props));
    return j;
}

JsonValue tree_node(const Instance& inst, int depth, int& budget) {
    JsonValue n = JsonValue::object();
    n.set("id", inst.id());
    n.set("class", inst.class_name());
    n.set("name", inst.name());
    --budget;
    const std::vector<Instance*>& kids = inst.children();
    if (kids.empty()) return n;
    if (depth <= 0 || budget <= 0) {
        n.set("child_count", u64(kids.size()));
        return n;
    }
    JsonValue arr = JsonValue::array();
    for (const Instance* c : kids) {
        if (budget <= 0) break;
        arr.push(tree_node(*c, depth - 1, budget));
    }
    if (arr.size() < kids.size()) n.set("child_count", u64(kids.size())); // cut short by the node budget
    n.set("children", std::move(arr));
    return n;
}

// ---- Tool handlers ------------------------------------------------------------------------------------------

ToolResult tool_get_tree(DataModel& dm, const JsonValue& args) {
    Instance* start = &dm.root();
    if (has_arg(args, "path") || has_arg(args, "id")) {
        std::string err;
        start = find_arg(dm, args, "path", "id", err);
        if (!start) return ToolResult::error(err);
    }
    const int depth = int_arg(args, "max_depth", 2, 0, 16);
    int budget = kMaxTreeNodes;
    JsonValue tree = tree_node(*start, depth, budget);
    tree.set("path", path_of(*start));
    if (budget <= 0) tree.set("truncated", true);
    return ToolResult::json(tree);
}

ToolResult tool_get_instance(DataModel& dm, const JsonValue& args) {
    std::string err;
    Instance* inst = find_arg(dm, args, "path", "id", err);
    if (!inst) return ToolResult::error(err);
    return ToolResult::json(describe(dm, *inst, true));
}

ToolResult tool_list_classes(DataModel&, const JsonValue& args) {
    const std::string only = str_arg(args, "class");
    const std::string filter = lower(str_arg(args, "filter"));
    const bool non_creatable = bool_arg(args, "include_non_creatable", false);
    JsonValue list = JsonValue::array();
    for (const ClassInfo* c : ClassRegistry::get().all()) {
        if (!only.empty()) {
            if (c->name != only) continue;
        } else if (!non_creatable && !c->creatable) {
            continue;
        }
        if (!filter.empty() && lower(c->name).find(filter) == kNpos) continue;
        list.push(describe_class(*c));
    }
    if (!only.empty() && list.size() == 0)
        return ToolResult::error("unknown class '" + only + "'. Call list_classes without arguments to see them all");
    JsonValue out = JsonValue::object();
    out.set("count", u64(list.size()));
    out.set("classes", std::move(list));
    return ToolResult::json(out);
}

ToolResult tool_create_instance(DataModel& dm, const JsonValue& args) {
    const std::string class_name = str_arg(args, "class");
    const ClassInfo* cls = ClassRegistry::get().find(class_name);
    if (!cls) return ToolResult::error("unknown class '" + class_name + "'. Use list_classes to see the classes");
    if (cls->service)
        return ToolResult::error("'" + class_name +
                                 "' is a service that already exists in the DataModel; find it "
                                 "with get_tree and create children under it");
    if (!cls->creatable) return ToolResult::error("'" + class_name + "' cannot be created directly (it is abstract)");

    std::string err;
    Instance* parent = find_arg(dm, args, "parent_path", "parent_id", err);
    if (!parent) return ToolResult::error("parent: " + err);
    if (parent == &dm.root()) return ToolResult::error("parent must be a service such as Workspace, not the DataModel");

    std::vector<Assignment> list;
    std::vector<std::string> problems;
    if (has_arg(args, "name")) {
        const std::string name = str_arg(args, "name");
        const std::string problem = name_problem(name);
        if (!problem.empty())
            problems.push_back("'name': " + problem);
        else
            list.push_back(Assignment{cls->find_prop("Name"), Value::string(name)});
    }
    if (has_arg(args, "properties")) convert_properties(dm, *cls, args["properties"], list, problems);
    if (!problems.empty()) return problems_error("Instance not created; nothing was changed:", problems);

    // Built detached, configured, then parented: listeners see the finished instance once.
    Instance* inst = dm.create(class_name, nullptr);
    if (!inst) return ToolResult::error("could not create '" + class_name + "'");
    apply_assignments(*inst, list);
    if (!dm.set_parent(inst, parent)) {
        dm.destroy(inst);
        return ToolResult::error("could not parent the new " + class_name + " under '" + path_of(*parent) + "'");
    }
    return ToolResult::json(describe(dm, *inst, false));
}

// Many instances in one call (the same rules as create_instance, applied to each item; items may also omit the
// parent and inherit the call's parent_path).
ToolResult tool_create_instances(DataModel& dm, const JsonValue& args) {
    const JsonValue& items = args["instances"];
    if (!items.is_array() || items.items().empty()) return ToolResult::error("'instances' must be a non-empty array");
    if (items.items().size() > 5000) return ToolResult::error("at most 5000 instances per call");
    u32 created = 0;
    JsonValue failures = JsonValue::array();
    JsonValue ids = JsonValue::array();
    for (usize i = 0; i < items.items().size(); ++i) {
        JsonValue item = items.items()[i];
        if (!item.is_object()) {
            JsonValue f = JsonValue::object();
            f.set("index", JsonValue(static_cast<f64>(i)));
            f.set("error", JsonValue(std::string("not an object")));
            failures.push(std::move(f));
            continue;
        }
        if (!has_arg(item, "parent_path") && !has_arg(item, "parent_id"))
            item.set("parent_path",
                     JsonValue(has_arg(args, "parent_path") ? str_arg(args, "parent_path") : std::string("Workspace")));
        const ToolResult r = tool_create_instance(dm, item);
        if (r.is_error) {
            JsonValue f = JsonValue::object();
            f.set("index", JsonValue(static_cast<f64>(i)));
            f.set("error", JsonValue(r.content.empty() ? std::string("?") : r.content[0].text));
            failures.push(std::move(f));
            if (failures.items().size() >= 20) break;
            continue;
        }
        ++created;
        JsonValue d;
        if (!r.content.empty() && JsonValue::parse(r.content[0].text, d)) ids.push(d["id"]);
    }
    JsonValue out = JsonValue::object();
    out.set("created", JsonValue(static_cast<f64>(created)));
    out.set("ids", std::move(ids));
    out.set("failures", std::move(failures));
    return ToolResult::json(out);
}

// What the engine can do, for an AI that has never seen Tefra.
ToolResult tool_engine_guide(DataModel&, const JsonValue&) {
    return ToolResult::text(R"(Tefra engine guide (units: metres, +Y up; Orientation in degrees Y-X-Z; Color3 0..1)

LANDSCAPES - do not build terrain or grass from parts. Create ONE Terrain under Workspace and set Kind:
  Kind = "Plains": rolling meadow covered in tall, wind-animated grass and wildflowers, lone trees.
  Kind = "Valley": misty forest valley (conifers, river, lake, undergrowth, grass).
  Kind = "Coast":  tropical beach with palms and rocks; add an Ocean instance under Workspace for animated sea.
  Seed changes the shape; Vegetation=false removes trees. Use terrain_height(x, z) / raycast to place things on it.
  The templates tool (new_place_from_template) builds these with a spawn: meadow, beach, valley, obby, baseplate...

PARTS: Part (Shape Block/Ball/Cylinder/Wedge), MeshPart (MeshId = path of a .glb/.gltf; see import_model),
  SpawnLocation (where players appear), Model / Folder to group. Material: Plastic, SmoothPlastic, Wood,
  WoodPlanks, Metal, Concrete, Brick, Cobblestone, Granite, Marble, Slate, Sand, Grass, Ground, Rock, Ice, Glass,
  Neon (emissive), Fabric, Foil. Anchored=true for static scenery. Lights: PointLight / SpotLight under a part.

MANY OBJECTS: use create_instances with an array (up to 5000 per call) instead of create_instance in a loop.

SCRIPTS: Script / LocalScript / ModuleScript hold C# (.NET 8) compiled when Play starts. A script is a class
  deriving from TefraScript with Start() / Update(float dt). API: Instance.New, FindFirstChild, Game.Workspace,
  Players.LocalPlayer, BasePart.Touched, Humanoid.Health/Died, UserInputService, Sound.Play, RunService.Heartbeat,
  Task.Wait, TweenService (Create, Play, Completed), TextLabel.Text... Scripts do not run in edit mode: generate
  edit-time content with create_instances.
  Messaging: BindableEvent/BindableFunction (run at once), RemoteEvent/RemoteFunction (loopback, delivered at the next step), Debris.AddItem(item, s), DataStoreService.GetDataStore(name).SetAsync/GetAsync, HttpService.JSONEncode/JSONDecode.
  Spatial queries: Workspace.Raycast/Spherecast/Blockcast (RaycastParams), GetPartBoundsInRadius/Box and GetPartsInPart (OverlapParams), BasePart.ApplyImpulse/GetMass/GetTouchingParts.
  Also: CFrame, UDim2, NumberSequence/ColorSequence, SetAttribute/GetAttribute, CollectionService tags.

EFFECTS: ParticleEmitter under a part (real GPU particles, visible in edit mode too). Texture = builtin:fire /
  smoke / spark / star / snow / ring / circle; Rate, Lifetime, Speed, SpreadAngle, Size->SizeEnd, Color->ColorEnd,
  Transparency->TransparencyEnd, LightEmission (1 = additive glow), Acceleration (e.g. [0,-20,0] for sparks),
  Drag, RotSpeed, Stretch, Shape (Box/Sphere/Disc/Point), EmissionDirection. Bursts from C#: emitter.Emit(30).
  Do not fake particles with small parts.
CHARACTERS: Humanoid events per character: Jumping, FreeFalling, Landed, StateChanged(old, new), Died,
  HealthChanged. Hook every player's Humanoid (player.CharacterAdded for each of Players.GetPlayers()), not only player 0.
  Checkpoints: SpawnLocation pads whose Touched sets player.RespawnLocation (see the obby template).

UI: ScreenGui under StarterGui with Frame / TextLabel / TextButton / ImageLabel (Position/Size = scale + offset).
AUDIO: Sound (SoundId = file path or builtin:click/jump/checkpoint/win/coin/wind_loop...).
PLAY: play / stop / get_output (script errors) / capture_viewport (see the result) / set_camera.)");
}

// One attribute to set (remove = true for a null in the request) or a validated attribute value.
struct AttributeChange {
    std::string name;
    bool remove = false;
    Value value;
};

ToolResult tool_set_properties(DataModel& dm, const JsonValue& args) {
    std::string err;
    Instance* inst = find_arg(dm, args, "path", "id", err);
    if (!inst) return ToolResult::error(err);
    const bool has_props = has_arg(args, "properties");
    const bool has_attrs = has_arg(args, "attributes");
    const bool has_tags = has_arg(args, "tags");
    if (!has_props && !has_attrs && !has_tags)
        return ToolResult::error("Give 'properties' (an object such as {\"Anchored\": true}), 'attributes' (such as "
                                 "{\"Health\": 100}) or 'tags' (such as [\"Enemy\"]).");

    // Everything is checked before anything changes.
    std::vector<std::string> problems;
    std::vector<Assignment> list;
    const JsonValue none = JsonValue::object();
    convert_properties(dm, inst->cls(), has_props ? args["properties"] : none, list, problems);

    std::vector<AttributeChange> changes;
    if (has_attrs) {
        const JsonValue& attrs = args["attributes"];
        if (!attrs.is_object()) {
            problems.push_back("'attributes' must be an object such as {\"Health\": 100}");
        } else {
            for (const auto& [name, json] : attrs.members()) {
                if (name.empty()) {
                    problems.push_back("attribute names must not be empty");
                    continue;
                }
                AttributeChange c;
                c.name = name;
                if (json.is_null()) {
                    c.remove = true;
                } else if (!DataModel::attribute_from_json(json, c.value)) {
                    problems.push_back("'attributes." + name +
                                       "' expects a bool, number, string, [x, y, z] or "
                                       "{\"Color3\": [r, g, b]}, got " +
                                       describe_json(json));
                    continue;
                }
                changes.push_back(std::move(c));
            }
        }
    }

    std::set<std::string> tags;
    if (has_tags) {
        const JsonValue& list_json = args["tags"];
        if (!list_json.is_array()) {
            problems.push_back("'tags' must be an array of names such as [\"Enemy\"]");
        } else {
            for (const JsonValue& t : list_json.items()) {
                if (!t.is_string() || t.as_string().empty()) {
                    problems.push_back("tags must be non-empty strings, got " + describe_json(t));
                    continue;
                }
                tags.insert(t.as_string());
            }
        }
    }
    if (!problems.empty()) return problems_error("Properties not set; nothing was changed:", problems);

    JsonValue out = describe(dm, *inst, false);
    out.set("updated", apply_assignments(*inst, list));
    if (has_attrs) {
        for (const AttributeChange& c : changes) {
            if (c.remove)
                inst->remove_attribute(c.name);
            else
                inst->set_attribute(c.name, c.value);
        }
    }
    if (has_tags) {
        // The tag list is replaced: tags not listed are removed, listed ones added.
        const std::set<std::string> current(inst->tags().begin(), inst->tags().end());
        for (const std::string& t : current)
            if (!tags.count(t)) inst->remove_tag(t);
        for (const std::string& t : tags) inst->add_tag(t);
    }
    JsonValue attrs_now = JsonValue::object();
    for (const auto& [name, v] : inst->attributes()) attrs_now.set(name, DataModel::attribute_to_json(v));
    out.set("attributes", std::move(attrs_now));
    JsonValue tags_now = JsonValue::array();
    for (const std::string& t : inst->tags()) tags_now.push(JsonValue(t));
    out.set("tags", std::move(tags_now));
    return ToolResult::json(out);
}

ToolResult tool_delete_instance(DataModel& dm, const JsonValue& args) {
    std::string err;
    Instance* inst = find_arg(dm, args, "path", "id", err);
    if (!inst) return ToolResult::error(err);
    if (inst == &dm.root() || inst->cls().service)
        return ToolResult::error("services and the DataModel cannot be deleted; delete their children instead");
    const u64 id = inst->id();
    const std::string path = path_of(*inst);
    const std::string cls = inst->class_name();
    const u64 count = u64(inst->descendants().size()) + 1;
    if (!dm.destroy(inst)) return ToolResult::error("could not delete '" + path + "'");
    JsonValue out = JsonValue::object();
    out.set("deleted_id", id);
    out.set("path", path);
    out.set("class", cls);
    out.set("instances_removed", count);
    return ToolResult::json(out);
}

ToolResult tool_reparent(DataModel& dm, const JsonValue& args) {
    std::string err;
    Instance* inst = find_arg(dm, args, "path", "id", err);
    if (!inst) return ToolResult::error(err);
    Instance* target = find_arg(dm, args, "new_parent_path", "new_parent_id", err);
    if (!target) return ToolResult::error("new parent: " + err);
    if (inst == &dm.root() || inst->cls().service) return ToolResult::error("services cannot be moved");
    if (target == &dm.root()) return ToolResult::error("the new parent must be a service such as Workspace");
    if (target == inst || target->is_descendant_of(inst))
        return ToolResult::error("an instance cannot be moved into itself or one of its descendants");
    if (!dm.set_parent(inst, target)) return ToolResult::error("could not move the instance");
    return ToolResult::json(describe(dm, *inst, false));
}

ToolResult tool_clone(DataModel& dm, const JsonValue& args) {
    std::string err;
    Instance* src = find_arg(dm, args, "path", "id", err);
    if (!src) return ToolResult::error(err);
    if (src == &dm.root() || src->cls().service) return ToolResult::error("services cannot be cloned");
    Instance* parent = src->parent();
    if (has_arg(args, "parent_path") || has_arg(args, "parent_id")) {
        parent = find_arg(dm, args, "parent_path", "parent_id", err);
        if (!parent) return ToolResult::error("parent: " + err);
    }
    if (!parent || parent == &dm.root()) return ToolResult::error("the copy needs a parent such as Workspace");
    if (has_arg(args, "name")) {
        const std::string problem = name_problem(str_arg(args, "name"));
        if (!problem.empty()) return ToolResult::error("'name': " + problem);
    }
    Instance* copy = dm.clone(*src, parent);
    if (!copy) return ToolResult::error("could not clone the instance");
    if (has_arg(args, "name")) copy->set_name(str_arg(args, "name"));
    JsonValue out = describe(dm, *copy, false);
    out.set("cloned_instances", u64(copy->descendants().size()) + 1);
    return ToolResult::json(out);
}

ToolResult tool_find(DataModel& dm, const JsonValue& args) {
    const std::string name = str_arg(args, "name");
    const std::string class_name = str_arg(args, "class");
    if (name.empty() && class_name.empty()) return ToolResult::error("give 'name', 'class' or both");
    const ClassInfo* cls = nullptr;
    if (!class_name.empty()) {
        cls = ClassRegistry::get().find(class_name);
        if (!cls) return ToolResult::error("unknown class '" + class_name + "'. Use list_classes to see the classes");
    }
    Instance* scope = &dm.root();
    if (has_arg(args, "under_path") || has_arg(args, "under_id")) {
        std::string err;
        scope = find_arg(dm, args, "under_path", "under_id", err);
        if (!scope) return ToolResult::error("under: " + err);
    }
    const bool exact = bool_arg(args, "exact", false);
    const usize limit = static_cast<usize>(int_arg(args, "limit", 100, 1, static_cast<int>(kMaxResults)));
    const std::string wanted = lower(name);

    JsonValue results = JsonValue::array();
    u64 total = 0;
    for (Instance* inst : scope->descendants()) {
        if (cls && !inst->is_a(cls)) continue;
        if (!name.empty()) {
            const bool hit = exact ? inst->name() == name : lower(inst->name()).find(wanted) != kNpos;
            if (!hit) continue;
        }
        ++total;
        if (results.size() >= limit) continue;
        JsonValue r = JsonValue::object();
        r.set("id", inst->id());
        r.set("name", inst->name());
        r.set("class", inst->class_name());
        r.set("path", path_of(*inst));
        results.push(std::move(r));
    }
    JsonValue out = JsonValue::object();
    out.set("count", total);
    out.set("truncated", total > results.size());
    out.set("results", std::move(results));
    return ToolResult::json(out);
}

bool is_script(const Instance& inst) { return inst.is_a("BaseScript") || inst.is_a("ModuleScript"); }

usize line_count(const std::string& text) {
    if (text.empty()) return 0;
    usize lines = 1;
    for (char c : text)
        if (c == '\n') ++lines;
    return lines;
}

ToolResult tool_get_script(DataModel& dm, const JsonValue& args) {
    std::string err;
    Instance* inst = find_arg(dm, args, "path", "id", err);
    if (!inst) return ToolResult::error(err);
    if (!is_script(*inst))
        return ToolResult::error("'" + path_of(*inst) + "' is a " + inst->class_name() +
                                 ", not a script (Script, LocalScript or ModuleScript)");
    const std::string& source = inst->get_string("Source");
    JsonValue out = describe(dm, *inst, false);
    out.set("source", source);
    out.set("lines", u64(line_count(source)));
    if (inst->is_a("BaseScript")) out.set("enabled", inst->get_bool("Enabled", true));
    return ToolResult::json(out);
}

ToolResult tool_set_script(DataModel& dm, const JsonValue& args) {
    std::string err;
    Instance* inst = find_arg(dm, args, "path", "id", err);
    if (!inst) return ToolResult::error(err);
    if (!is_script(*inst))
        return ToolResult::error("'" + path_of(*inst) + "' is a " + inst->class_name() +
                                 ", not a script (Script, LocalScript or ModuleScript)");
    if (!has_arg(args, "source")) return ToolResult::error("'source' is required: the C# code as a string");
    const std::string source = str_arg(args, "source");
    if (source.size() > kMaxScriptBytes) return ToolResult::error("the source is larger than 1 MiB");
    if (!inst->set("Source", Value::string(source))) return ToolResult::error("could not set the source");
    JsonValue out = describe(dm, *inst, false);
    out.set("lines", u64(line_count(source)));
    out.set("bytes", u64(source.size()));
    return ToolResult::json(out);
}

bool ends_with_ci(const std::string& text, const std::string& suffix) {
    if (text.size() < suffix.size()) return false;
    return lower(text.substr(text.size() - suffix.size())) == lower(suffix);
}

ToolResult tool_save_place(DataModel& dm, const JsonValue& args) {
    const std::string path = str_arg(args, "path");
    if (path.empty()) return ToolResult::error("'path' is required, for example C:/Games/MyGame.tefra");
    if (!ends_with_ci(path, ".tefra")) return ToolResult::error("the file name must end in .tefra");
    if (!dm.save(path.c_str())) return ToolResult::error("could not write '" + path + "'");
    JsonValue out = JsonValue::object();
    out.set("path", path);
    out.set("instances", u64(dm.root().descendants().size()));
    return ToolResult::json(out);
}

ToolResult tool_load_place(DataModel& dm, const JsonValue& args) {
    const std::string path = str_arg(args, "path");
    if (path.empty()) return ToolResult::error("'path' is required");
    if (!file_exists(path.c_str())) return ToolResult::error("no file at '" + path + "'");
    std::string err;
    if (!dm.load(path.c_str(), &err)) return ToolResult::error("could not load '" + path + "': " + err);
    JsonValue out = JsonValue::object();
    out.set("path", path);
    out.set("instances", u64(dm.root().descendants().size()));
    out.set("note", "The place was replaced. Instances from earlier calls are gone; ids stored in the file are kept.");
    return ToolResult::json(out);
}

// ---- Schemas ------------------------------------------------------------------------------------------------

// One property of a schema "properties" object. The description is escaped as a JSON string.
std::string prop(const char* name, const char* type, const char* description) {
    return std::string("\"") + name + "\":{\"type\":\"" + type +
           "\",\"description\":" + JsonValue(std::string(description)).dump() + "}";
}

std::string schema(std::initializer_list<std::string> props, std::initializer_list<const char*> required = {}) {
    std::string out = "{\"type\":\"object\",\"properties\":{";
    bool first = true;
    for (const std::string& p : props) {
        if (!first) out += ",";
        out += p;
        first = false;
    }
    out += "}";
    if (required.size() > 0) {
        out += ",\"required\":[";
        first = true;
        for (const char* r : required) {
            if (!first) out += ",";
            out += std::string("\"") + r + "\"";
            first = false;
        }
        out += "]";
    }
    out += "}";
    return out;
}

const char* const kPathDoc =
    "Instance path such as Workspace.Baseplate (the leading game. is optional). Give path or id.";
const char* const kIdDoc = "Instance id as returned by get_tree, find or create_instance. Give path or id.";

using Handler = ToolResult (*)(DataModel&, const JsonValue&);

} // namespace

int register_world_tools(Server& server, DataModel& dm) {
    return register_world_tools(server, [&dm]() -> DataModel& { return dm; });
}

int register_world_tools(Server& server, std::function<DataModel&()> model) {
    int added = 0;
    auto add = [&](const char* name, const char* description, const std::string& input_schema, Handler fn) {
        if (server.add_tool(name, description, input_schema,
                            [model, fn](const JsonValue& args) { return fn(model(), args); }))
            ++added;
    };

    add("get_tree",
        "Returns the instance tree below an instance (default: the whole DataModel) as nested objects with id, class "
        "and name. Nodes deeper than max_depth show child_count instead of children.",
        schema({prop("path", "string", kPathDoc), prop("id", "integer", kIdDoc),
                prop("max_depth", "integer", "How many levels to expand (0-16, default 2).")}),
        tool_get_tree);

    add("get_instance",
        "Returns every property of an instance with its type and current value, its path, parent id and children, "
        "plus its attributes (name -> value) and tags.",
        schema({prop("path", "string", kPathDoc), prop("id", "integer", kIdDoc)}), tool_get_instance);

    add("list_classes",
        "Lists the creatable classes with their properties (inherited ones included): type, default, enum options, "
        "read-only flag and documentation. Pass class to get a single class; filter matches part of the class name.",
        schema({prop("class", "string", "Exact class name, for example Part."),
                prop("filter", "string", "Part of a class name, case-insensitive."),
                prop("include_non_creatable", "boolean", "Also list abstract classes and services.")}),
        tool_list_classes);

    add("create_instance",
        "Creates an instance of a class under a parent and sets its initial properties. Every property is checked "
        "first: if one is wrong nothing is created and all the problems are reported. Vectors are [x, y, z]; Color3 is "
        "[r, g, b] from 0 to 1; enums take the item name; instance references take a path or an id.",
        schema({prop("class", "string", "Class to create, for example Part, Model or Script."),
                prop("parent_path", "string", "Parent path, for example Workspace."),
                prop("parent_id", "integer", "Parent id (alternative to parent_path)."),
                prop("name", "string", "Name of the new instance."),
                prop("properties", "object", "Property values by name, for example {\"Size\": [4, 1, 4]}.")},
               {"class"}),
        tool_create_instance);

    add("create_instances",
        "Creates many instances in one call (up to 5000): an array of {class, name, properties, parent_path} with the "
        "same rules as create_instance. Items without a parent go under the call's parent_path (default Workspace). "
        "Returns the ids created and the first failures. Use it for scattering scenery instead of one call per part.",
        schema({prop("instances", "array", "Array of {class, name, properties, parent_path | parent_id}."),
                prop("parent_path", "string", "Default parent for items that do not give one (default Workspace).")},
               {"instances"}),
        tool_create_instances);

    add("engine_guide",
        "Read this first: what Tefra can do (landscapes with real animated grass via Terrain.Kind, parts, materials, "
        "batch creation, C# scripts, UI, audio) and which tools to use for common requests.",
        schema({}), tool_engine_guide);

    add("set_properties",
        "Sets several properties of one instance at once, and optionally its attributes and tags. Attributes are "
        "merged "
        "into the existing ones (null removes one); tags replace the whole tag list. Everything is checked first; if "
        "any "
        "value is invalid nothing changes and every problem is listed.",
        schema({prop("path", "string", kPathDoc), prop("id", "integer", kIdDoc),
                prop("properties", "object", "Property values by name, for example {\"Position\": [0, 5, 0]}."),
                prop("attributes", "object",
                     "Attributes to set, for example {\"Health\": 100, \"Tint\": [1, 0, 0]}; null removes one."),
                prop("tags", "array", "The complete tag list, for example [\"Enemy\", \"Breakable\"].")},
               {}),
        tool_set_properties);

    add("delete_instance", "Deletes an instance and all its descendants. Services and the DataModel cannot be deleted.",
        schema({prop("path", "string", kPathDoc), prop("id", "integer", kIdDoc)}), tool_delete_instance);

    add("reparent", "Moves an instance under another parent. Cycles are refused.",
        schema({prop("path", "string", kPathDoc), prop("id", "integer", kIdDoc),
                prop("new_parent_path", "string", "Path of the new parent."),
                prop("new_parent_id", "integer", "Id of the new parent (alternative to new_parent_path).")}),
        tool_reparent);

    add("clone",
        "Deep-copies an instance and its descendants. The copy goes under the same parent unless a new parent is "
        "given; references inside the copied subtree point to the copies.",
        schema({prop("path", "string", kPathDoc), prop("id", "integer", kIdDoc),
                prop("parent_path", "string", "Parent for the copy (default: the original's parent)."),
                prop("parent_id", "integer", "Parent id for the copy (alternative to parent_path)."),
                prop("name", "string", "Name for the copy.")}),
        tool_clone);

    add("find",
        "Finds instances by name (case-insensitive part of the name, or exact with exact=true) and/or class "
        "(subclasses "
        "included, so BasePart finds Parts and MeshParts). Searches under under_path/under_id or the whole model.",
        schema({prop("name", "string", "Name to look for."), prop("class", "string", "Class to look for."),
                prop("exact", "boolean", "Exact name match instead of a substring match."),
                prop("under_path", "string", "Only search below this path."),
                prop("under_id", "integer", "Only search below this id."),
                prop("limit", "integer", "Maximum results (1-1000, default 100).")}),
        tool_find);

    add("get_script", "Returns the C# source of a Script, LocalScript or ModuleScript, with its line count.",
        schema({prop("path", "string", kPathDoc), prop("id", "integer", kIdDoc)}), tool_get_script);

    add("set_script",
        "Replaces the C# source of a Script, LocalScript or ModuleScript. The code is compiled the next time Play "
        "starts.",
        schema({prop("path", "string", kPathDoc), prop("id", "integer", kIdDoc),
                prop("source", "string", "The complete C# source code.")},
               {"source"}),
        tool_set_script);

    add("save_place", "Saves the whole place to a .tefra file on this computer (readable JSON).",
        schema({prop("path", "string", "Destination file, ending in .tefra.")}, {"path"}), tool_save_place);

    add("load_place", "Replaces the whole place with a .tefra file. Everything in the current place is removed first.",
        schema({prop("path", "string", "File to open, ending in .tefra.")}, {"path"}), tool_load_place);

    return added;
}

} // namespace tefra::mcp
