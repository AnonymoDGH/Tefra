// Native functions handed to managed code (TefraNativeApi). Each one resolves ids against the DataModel of the
// running session and returns a plain C value. Functions return 0 / nullptr when there is no session or the id is
// unknown; they never throw and never let a C++ exception escape.

#include "script_session.h"

#include <cstdint>
#include <iterator>
#include <utility>
#include <vector>

namespace tefra::scripting::detail {
namespace {

world::DataModel* g_model = nullptr;
const LogCallback* g_log = nullptr;
const InputState* g_input = nullptr;
const PhysicsHooks* g_hooks = nullptr;

world::Instance* find(std::uint64_t id) { return g_model && id != 0 ? g_model->find(id) : nullptr; }

// Builds the query filter of a call from its C form. Ids that no longer exist are dropped from the list.
void build_filter(const TefraFilter* in, world::QueryFilter& out) {
    out = world::QueryFilter{};
    if (!in) return;
    out.include = in->include != 0;
    for (std::uint32_t i = 0; in->ids && i < in->count; ++i)
        if (world::Instance* p = find(in->ids[i])) out.instances.push_back(p);
}

// Copies a hit into out[7] = position[3], normal[3], distance and returns the instance id.
std::uint64_t write_hit(const RayHit& hit, double* out) {
    out[0] = hit.position.x;
    out[1] = hit.position.y;
    out[2] = hit.position.z;
    out[3] = hit.normal.x;
    out[4] = hit.normal.y;
    out[5] = hit.normal.z;
    out[6] = hit.distance;
    return hit.instance->id();
}

vec3 vec_at(const double* p) { return {static_cast<f32>(p[0]), static_cast<f32>(p[1]), static_cast<f32>(p[2])}; }

bool valid_type(std::int32_t t) { return t >= 0 && t <= static_cast<std::int32_t>(world::PropType::Ref); }

void to_flat(const world::Value& v, TefraValue& out) {
    out.type = static_cast<std::int32_t>(v.type);
    out.n[0] = v.n[0];
    out.n[1] = v.n[1];
    out.n[2] = v.n[2];
    out.str = v.s.data();
    out.str_len = static_cast<std::uint32_t>(v.s.size());
    out.ref = v.ref;
}

bool from_flat(const TefraValue& in, world::Value& out) {
    if (!valid_type(in.type)) return false;
    out = world::Value{};
    out.type = static_cast<world::PropType>(in.type);
    out.n[0] = in.n[0];
    out.n[1] = in.n[1];
    out.n[2] = in.n[2];
    if (in.str && in.str_len > 0) out.s.assign(in.str, in.str_len);
    out.ref = in.ref;
    return true;
}

} // namespace

world::DataModel* current_model() { return g_model; }

void set_session(world::DataModel* model, const LogCallback* log) {
    g_model = model;
    g_log = log;
}

void set_input(const InputState* input) { g_input = input; }

void set_physics_hooks(const PhysicsHooks* hooks) { g_hooks = hooks; }

void session_log(LogLevel level, std::string_view message) {
    if (g_log && *g_log) {
        (*g_log)(level, message);
        return;
    }
    log_write(level, "%.*s", static_cast<int>(message.size()), message.data());
}

// C linkage: these are the entries of the TefraNativeApi table in abi.h.
extern "C" {

std::int32_t tefra_script_get_prop(std::uint64_t id, std::uint32_t index, TefraValue* out) {
    tefra::world::Instance* inst = find(id);
    if (!inst || !out || index >= inst->cls().prop_count()) return 0;
    to_flat(inst->get(index), *out);
    return 1;
}

std::int32_t tefra_script_set_prop(std::uint64_t id, std::uint32_t index, const TefraValue* value) {
    tefra::world::Instance* inst = find(id);
    tefra::world::Value v;
    if (!inst || !value || !from_flat(*value, v)) return 0;
    return inst->set(index, std::move(v)) ? 1 : 0;
}

std::int32_t tefra_script_prop_index(const char* class_name, const char* prop_name) {
    if (!class_name || !prop_name) return -1;
    const tefra::world::ClassInfo* cls = tefra::world::ClassRegistry::get().find(class_name);
    const tefra::world::PropInfo* p = cls ? cls->find_prop(prop_name) : nullptr;
    return p ? static_cast<std::int32_t>(p->index) : -1;
}

std::int32_t tefra_script_prop_type(const char* class_name, const char* prop_name) {
    if (!class_name || !prop_name) return -1;
    const tefra::world::ClassInfo* cls = tefra::world::ClassRegistry::get().find(class_name);
    const tefra::world::PropInfo* p = cls ? cls->find_prop(prop_name) : nullptr;
    return p ? static_cast<std::int32_t>(p->type) : -1;
}

const char* tefra_script_prop_name(std::uint64_t id, std::uint32_t index) {
    tefra::world::Instance* inst = find(id);
    const tefra::world::PropInfo* p = inst ? inst->cls().prop(index) : nullptr;
    return p ? p->name.c_str() : nullptr;
}

const char* tefra_script_class_name(std::uint64_t id) {
    tefra::world::Instance* inst = find(id);
    return inst ? inst->class_name().c_str() : nullptr;
}

std::int32_t tefra_script_is_a(std::uint64_t id, const char* class_name) {
    tefra::world::Instance* inst = find(id);
    return inst && class_name && inst->is_a(std::string_view(class_name)) ? 1 : 0;
}

std::uint64_t tefra_script_create(const char* class_name, std::uint64_t parent, const char* name) {
    if (!current_model() || !class_name) return 0;
    tefra::world::Instance* p = nullptr;
    if (parent != 0) {
        p = find(parent);
        if (!p) return 0;
    }
    tefra::world::Instance* inst = current_model()->create(class_name, p, name ? name : "");
    return inst ? inst->id() : 0;
}

std::int32_t tefra_script_destroy(std::uint64_t id) {
    tefra::world::Instance* inst = find(id);
    return inst && current_model()->destroy(inst) ? 1 : 0;
}

std::int32_t tefra_script_set_parent(std::uint64_t id, std::uint64_t parent) {
    tefra::world::Instance* inst = find(id);
    if (!inst) return 0;
    tefra::world::Instance* p = nullptr;
    if (parent != 0) {
        p = find(parent);
        if (!p) return 0;
    }
    return current_model()->set_parent(inst, p) ? 1 : 0;
}

std::uint64_t tefra_script_parent(std::uint64_t id) {
    tefra::world::Instance* inst = find(id);
    return inst && inst->parent() ? inst->parent()->id() : 0;
}

std::int32_t tefra_script_child_count(std::uint64_t id) {
    tefra::world::Instance* inst = find(id);
    return inst ? static_cast<std::int32_t>(inst->children().size()) : 0;
}

std::uint64_t tefra_script_child_at(std::uint64_t id, std::int32_t index) {
    tefra::world::Instance* inst = find(id);
    if (!inst || index < 0 || static_cast<std::size_t>(index) >= inst->children().size()) return 0;
    return inst->children()[static_cast<std::size_t>(index)]->id();
}

std::uint64_t tefra_script_find_child(std::uint64_t id, const char* name) {
    tefra::world::Instance* inst = find(id);
    if (!inst || !name) return 0;
    tefra::world::Instance* child = inst->find_first_child(std::string_view(name));
    return child ? child->id() : 0;
}

std::uint64_t tefra_script_service(const char* name) {
    if (!current_model() || !name) return 0;
    tefra::world::Instance* s = current_model()->service(name);
    return s ? s->id() : 0;
}

std::uint64_t tefra_script_clone(std::uint64_t id, std::uint64_t parent) {
    tefra::world::Instance* src = find(id);
    if (!src) return 0;
    tefra::world::Instance* p = nullptr;
    if (parent != 0) {
        p = find(parent);
        if (!p) return 0;
    }
    tefra::world::Instance* copy = current_model()->clone(*src, p);
    return copy ? copy->id() : 0;
}

std::uint64_t tefra_script_find_path(const char* path) {
    if (!current_model() || !path) return 0;
    tefra::world::Instance* inst = current_model()->find_path(path);
    return inst ? inst->id() : 0;
}

void tefra_script_log(std::int32_t level, const char* message) {
    const std::int32_t clamped = level < 0 ? 0 : (level > 4 ? 4 : level);
    session_log(static_cast<tefra::LogLevel>(clamped), message ? message : "");
}

std::uint64_t tefra_script_raycast(const double* origin, const double* direction, double max_distance,
                                   const TefraFilter* filter, double* out) {
    if (!current_model() || !g_hooks || !g_hooks->raycast || !origin || !direction || !out) return 0;
    world::QueryFilter qf;
    build_filter(filter, qf);
    RayHit hit;
    if (!g_hooks->raycast(vec_at(origin), vec_at(direction), static_cast<f32>(max_distance), qf, hit) || !hit.instance)
        return 0;
    return write_hit(hit, out);
}

std::uint64_t tefra_script_spherecast(const double* origin, double radius, const double* direction,
                                      const TefraFilter* filter, double* out) {
    if (!current_model() || !g_hooks || !g_hooks->spherecast || !origin || !direction || !out) return 0;
    world::QueryFilter qf;
    build_filter(filter, qf);
    RayHit hit;
    if (!g_hooks->spherecast(vec_at(origin), static_cast<f32>(radius), vec_at(direction), qf, hit) || !hit.instance)
        return 0;
    return write_hit(hit, out);
}

std::uint64_t tefra_script_blockcast(const double* center, const double* orientation, const double* size,
                                     const double* direction, const TefraFilter* filter, double* out) {
    if (!current_model() || !g_hooks || !g_hooks->blockcast || !center || !orientation || !size || !direction || !out)
        return 0;
    world::QueryFilter qf;
    build_filter(filter, qf);
    RayHit hit;
    if (!g_hooks->blockcast(vec_at(center), vec_at(orientation), vec_at(size), vec_at(direction), qf, hit) ||
        !hit.instance)
        return 0;
    return write_hit(hit, out);
}

std::uint32_t tefra_script_overlap(std::int32_t kind, const double* geom, std::uint64_t part_id,
                                   const TefraFilter* filter, std::uint32_t max_parts, std::uint64_t* out_ids,
                                   std::uint32_t capacity) {
    if (!current_model() || !g_hooks || !g_hooks->overlap || !geom || kind < 0 || kind > 2) return 0;
    world::OverlapRequest req;
    req.kind = static_cast<world::OverlapKind>(kind);
    req.center = vec_at(geom);
    req.orientation = vec_at(geom + 3);
    req.size = vec_at(geom + 6);
    req.radius = static_cast<f32>(geom[9]);
    req.part = req.kind == world::OverlapKind::Part ? find(part_id) : nullptr;
    if (req.kind == world::OverlapKind::Part && !req.part) return 0;
    world::QueryFilter qf;
    build_filter(filter, qf);
    req.filter = &qf;
    req.max_parts = max_parts;
    std::vector<world::Instance*> found;
    g_hooks->overlap(req, found);
    if (out_ids)
        for (std::size_t i = 0; i < found.size() && i < capacity; ++i) out_ids[i] = found[i]->id();
    return static_cast<std::uint32_t>(found.size());
}

std::uint32_t tefra_script_touching_parts(std::uint64_t part_id, std::uint64_t* out_ids, std::uint32_t capacity) {
    world::Instance* part = find(part_id);
    if (!current_model() || !g_hooks || !g_hooks->touching || !part) return 0;
    std::vector<world::Instance*> found;
    g_hooks->touching(*part, found);
    if (out_ids)
        for (std::size_t i = 0; i < found.size() && i < capacity; ++i) out_ids[i] = found[i]->id();
    return static_cast<std::uint32_t>(found.size());
}

std::int32_t tefra_script_apply_impulse(std::uint64_t part_id, const double* value, std::int32_t angular) {
    world::Instance* part = find(part_id);
    if (!current_model() || !g_hooks || !g_hooks->impulse || !part || !value) return 0;
    return g_hooks->impulse(*part, vec_at(value), angular != 0) ? 1 : 0;
}

double tefra_script_mass(std::uint64_t part_id) {
    world::Instance* part = find(part_id);
    if (!current_model() || !g_hooks || !g_hooks->mass || !part) return 0.0;
    return static_cast<double>(g_hooks->mass(*part));
}

std::int32_t tefra_script_input_key_down(std::int32_t key_code) {
    if (!g_input || key_code <= 0 || key_code >= static_cast<std::int32_t>(Key::Count)) return 0;
    return g_input->key(static_cast<Key>(key_code)) ? 1 : 0;
}

std::int32_t tefra_script_input_mouse_down(std::int32_t button) {
    if (!g_input || button < 0 || button > 2) return 0;
    return g_input->mouse_down[button] ? 1 : 0;
}

std::int32_t tefra_script_input_mouse_location(double* xy_out) {
    if (!xy_out) return 0;
    xy_out[0] = g_input ? static_cast<double>(g_input->mouse_pos.x) : 0.0;
    xy_out[1] = g_input ? static_cast<double>(g_input->mouse_pos.y) : 0.0;
    return 1;
}

std::uint64_t tefra_script_root() { return current_model() ? current_model()->root().id() : 0; }

std::int32_t tefra_script_attr_count(std::uint64_t id) {
    tefra::world::Instance* inst = find(id);
    return inst ? static_cast<std::int32_t>(inst->attributes().size()) : -1;
}

const char* tefra_script_attr_name_at(std::uint64_t id, std::int32_t index) {
    tefra::world::Instance* inst = find(id);
    if (!inst || index < 0 || static_cast<std::size_t>(index) >= inst->attributes().size()) return nullptr;
    auto it = inst->attributes().begin();
    std::advance(it, index);
    return it->first.c_str();
}

std::int32_t tefra_script_attr_get(std::uint64_t id, const char* name, TefraValue* out) {
    tefra::world::Instance* inst = find(id);
    const tefra::world::Value* v = inst && name ? inst->get_attribute(name) : nullptr;
    if (!v || !out) return 0;
    to_flat(*v, *out);
    return 1;
}

std::int32_t tefra_script_attr_set(std::uint64_t id, const char* name, const TefraValue* value) {
    tefra::world::Instance* inst = find(id);
    tefra::world::Value v;
    if (!inst || !name || !value || !from_flat(*value, v)) return 0;
    return inst->set_attribute(name, std::move(v)) ? 1 : 0;
}

std::int32_t tefra_script_attr_remove(std::uint64_t id, const char* name) {
    tefra::world::Instance* inst = find(id);
    return inst && name && inst->remove_attribute(name) ? 1 : 0;
}

std::int32_t tefra_script_tag_add(std::uint64_t id, const char* tag) {
    tefra::world::Instance* inst = find(id);
    return inst && tag && inst->add_tag(tag) ? 1 : 0;
}

std::int32_t tefra_script_tag_remove(std::uint64_t id, const char* tag) {
    tefra::world::Instance* inst = find(id);
    return inst && tag && inst->remove_tag(tag) ? 1 : 0;
}

std::int32_t tefra_script_tag_has(std::uint64_t id, const char* tag) {
    tefra::world::Instance* inst = find(id);
    return inst && tag && inst->has_tag(tag) ? 1 : 0;
}

std::int32_t tefra_script_tag_count(std::uint64_t id) {
    tefra::world::Instance* inst = find(id);
    return inst ? static_cast<std::int32_t>(inst->tags().size()) : -1;
}

const char* tefra_script_tag_at(std::uint64_t id, std::int32_t index) {
    tefra::world::Instance* inst = find(id);
    if (!inst || index < 0 || static_cast<std::size_t>(index) >= inst->tags().size()) return nullptr;
    auto it = inst->tags().begin();
    std::advance(it, index);
    return it->c_str();
}

std::int32_t tefra_script_tagged_count(const char* tag) {
    if (!current_model() || !tag) return 0;
    return static_cast<std::int32_t>(current_model()->tagged(tag).size());
}

std::uint64_t tefra_script_tagged_at(const char* tag, std::int32_t index) {
    if (!current_model() || !tag || index < 0) return 0;
    const std::vector<tefra::world::Instance*> list = current_model()->tagged(tag);
    return static_cast<std::size_t>(index) < list.size() ? list[static_cast<std::size_t>(index)]->id() : 0;
}

} // extern "C"

const TefraNativeApi* native_table() {
    static const TefraNativeApi table = {
        TEFRA_SCRIPT_ABI_VERSION,
        tefra_script_get_prop,
        tefra_script_set_prop,
        tefra_script_prop_index,
        tefra_script_prop_type,
        tefra_script_prop_name,
        tefra_script_class_name,
        tefra_script_is_a,
        tefra_script_create,
        tefra_script_destroy,
        tefra_script_set_parent,
        tefra_script_parent,
        tefra_script_child_count,
        tefra_script_child_at,
        tefra_script_find_child,
        tefra_script_service,
        tefra_script_clone,
        tefra_script_find_path,
        tefra_script_log,
        tefra_script_raycast,
        tefra_script_input_key_down,
        tefra_script_input_mouse_down,
        tefra_script_input_mouse_location,
        tefra_script_root,
        tefra_script_attr_count,
        tefra_script_attr_name_at,
        tefra_script_attr_get,
        tefra_script_attr_set,
        tefra_script_attr_remove,
        tefra_script_tag_add,
        tefra_script_tag_remove,
        tefra_script_tag_has,
        tefra_script_tag_count,
        tefra_script_tag_at,
        tefra_script_tagged_count,
        tefra_script_tagged_at,
        tefra_script_spherecast,
        tefra_script_blockcast,
        tefra_script_overlap,
        tefra_script_touching_parts,
        tefra_script_apply_impulse,
        tefra_script_mass,
    };
    return &table;
}

} // namespace tefra::scripting::detail
