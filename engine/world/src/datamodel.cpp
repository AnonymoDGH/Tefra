#include "tefra/world/datamodel.h"

#include "tefra/core/fs.h"
#include "tefra/core/log.h"

#include <algorithm>
#include <cmath>

namespace tefra::world {

const char* prop_type_name(PropType t) {
    switch (t) {
    case PropType::Bool: return "bool";
    case PropType::Int: return "int";
    case PropType::Float: return "float";
    case PropType::String: return "string";
    case PropType::Vector3: return "Vector3";
    case PropType::Color3: return "Color3";
    case PropType::Enum: return "enum";
    case PropType::Ref: return "Instance";
    }
    return "?";
}

bool Value::operator==(const Value& o) const {
    if (type != o.type) return false;
    switch (type) {
    case PropType::String: return s == o.s;
    case PropType::Ref: return ref == o.ref;
    case PropType::Vector3:
    case PropType::Color3: return n[0] == o.n[0] && n[1] == o.n[1] && n[2] == o.n[2];
    default: return n[0] == o.n[0];
    }
}

i32 EnumInfo::index_of(std::string_view item) const {
    for (usize i = 0; i < items.size(); ++i)
        if (items[i] == item) return static_cast<i32>(i);
    return -1;
}

// ---- ClassInfo / registry -----------------------------------------------------------------------------------

const PropInfo* ClassInfo::find_prop(std::string_view n) const {
    for (const ClassInfo* c = this; c; c = c->base)
        for (const PropInfo& p : c->props)
            if (p.name == n) return &p;
    return nullptr;
}

const PropInfo* ClassInfo::prop(u32 index) const {
    for (const ClassInfo* c = this; c; c = c->base)
        if (index >= c->first_prop && index < c->prop_count()) return &c->props[index - c->first_prop];
    return nullptr;
}

bool ClassInfo::is_a(const ClassInfo* other) const {
    for (const ClassInfo* c = this; c; c = c->base)
        if (c == other) return true;
    return false;
}

void register_builtin_classes(ClassRegistry& r); // classes.cpp

ClassRegistry& ClassRegistry::get() {
    static ClassRegistry reg;
    return reg;
}

ClassRegistry::ClassRegistry() {
    register_builtin_classes(*this);
    finalize();
}

const ClassInfo* ClassRegistry::find(std::string_view name) const {
    for (const auto& c : classes_)
        if (c->name == name) return c.get();
    return nullptr;
}

const EnumInfo* ClassRegistry::find_enum(std::string_view name) const {
    for (const auto& e : enums_)
        if (e->name == name) return e.get();
    return nullptr;
}

ClassInfo& ClassRegistry::add(std::string name, const char* base) {
    auto c = std::make_unique<ClassInfo>();
    c->name = std::move(name);
    c->base = base ? find(base) : nullptr;
    classes_.push_back(std::move(c));
    order_.push_back(classes_.back().get());
    return *classes_.back();
}

const EnumInfo* ClassRegistry::add_enum(std::string name, std::vector<std::string> items) {
    auto e = std::make_unique<EnumInfo>();
    e->name = std::move(name);
    e->items = std::move(items);
    enums_.push_back(std::move(e));
    return enums_.back().get();
}

void ClassRegistry::finalize() {
    // Classes are registered after their bases, so one pass in order suffices.
    for (auto& c : classes_) {
        c->first_prop = c->base ? c->base->prop_count() : 0;
        for (usize i = 0; i < c->props.size(); ++i) c->props[i].index = c->first_prop + static_cast<u32>(i);
    }
}

// ---- Instance -----------------------------------------------------------------------------------------------

bool Instance::is_a(std::string_view class_name) const {
    for (const ClassInfo* c = cls_; c; c = c->base)
        if (c->name == class_name) return true;
    return false;
}

const Value* Instance::get(std::string_view prop) const {
    const PropInfo* p = cls_->find_prop(prop);
    return p ? &props_[p->index] : nullptr;
}

bool Instance::set(u32 index, Value v, bool force) {
    const PropInfo* p = cls_->prop(index);
    if (!p || v.type != p->type) return false;
    if ((p->flags & kPropReadOnly) && !force) return false;
    if (p->type == PropType::Enum && p->enum_info && v.as_enum() >= p->enum_info->items.size()) return false;
    if (index == 0 && v.s.empty()) return false; // names are never empty
    if (props_[index] == v) return true;
    props_[index] = std::move(v);
    model_->notify_changed(*this, index);
    return true;
}

bool Instance::set(std::string_view prop, Value v, bool force) {
    const PropInfo* p = cls_->find_prop(prop);
    return p && set(p->index, std::move(v), force);
}

Instance* Instance::find_first_child(std::string_view n) const {
    for (Instance* c : children_)
        if (c->name() == n) return c;
    return nullptr;
}

Instance* Instance::find_first_child_of_class(std::string_view class_name) const {
    for (Instance* c : children_)
        if (c->is_a(class_name)) return c;
    return nullptr;
}

std::vector<Instance*> Instance::descendants() const {
    std::vector<Instance*> out;
    std::vector<const Instance*> stack{this};
    while (!stack.empty()) {
        const Instance* i = stack.back();
        stack.pop_back();
        for (auto it = i->children_.rbegin(); it != i->children_.rend(); ++it) {
            out.push_back(*it);
            stack.push_back(*it);
        }
    }
    return out;
}

bool Instance::is_descendant_of(const Instance* ancestor) const {
    for (const Instance* p = parent_; p; p = p->parent_)
        if (p == ancestor) return true;
    return false;
}

std::string Instance::full_name() const {
    if (!parent_) return name();
    std::string out = name();
    for (const Instance* p = parent_; p && p->parent_; p = p->parent_) out = p->name() + "." + out;
    return out;
}

vec3 Instance::get_vec3(std::string_view prop, vec3 def) const {
    const Value* v = get(prop);
    return v && (v->type == PropType::Vector3 || v->type == PropType::Color3) ? v->as_vec3() : def;
}
f32 Instance::get_float(std::string_view prop, f32 def) const {
    const Value* v = get(prop);
    return v && (v->type == PropType::Float || v->type == PropType::Int) ? v->as_float() : def;
}
bool Instance::get_bool(std::string_view prop, bool def) const {
    const Value* v = get(prop);
    return v && v->type == PropType::Bool ? v->as_bool() : def;
}
const std::string& Instance::get_string(std::string_view prop) const {
    static const std::string empty;
    const Value* v = get(prop);
    return v && v->type == PropType::String ? v->s : empty;
}

bool attribute_type_supported(PropType t) {
    switch (t) {
    case PropType::Bool:
    case PropType::Int:
    case PropType::Float:
    case PropType::String:
    case PropType::Vector3:
    case PropType::Color3: return true;
    case PropType::Enum:
    case PropType::Ref: return false;
    }
    return false;
}

const Value* Instance::get_attribute(std::string_view name) const {
    auto it = attributes_.find(name);
    return it == attributes_.end() ? nullptr : &it->second;
}

bool Instance::set_attribute(std::string_view name, Value v) {
    if (name.empty() || !attribute_type_supported(v.type)) return false;
    // Keep only the fields of the value's type: the stored copy is small and equal values compare equal.
    Value clean;
    clean.type = v.type;
    for (int i = 0; i < 3; ++i) clean.n[i] = v.n[i];
    if (v.type == PropType::String) clean.s = std::move(v.s);
    auto it = attributes_.find(name);
    if (it != attributes_.end()) {
        if (it->second == clean) return true;
        it->second = std::move(clean);
    } else {
        attributes_.emplace(std::string(name), std::move(clean));
    }
    model_->notify_attribute(*this, name);
    return true;
}

bool Instance::remove_attribute(std::string_view name) {
    auto it = attributes_.find(name);
    if (it == attributes_.end()) return false;
    const std::string key = it->first; // name may point into the entry that is erased
    attributes_.erase(it);
    model_->notify_attribute(*this, key);
    return true;
}

bool Instance::add_tag(std::string_view tag) {
    if (tag.empty() || !tags_.emplace(std::string(tag)).second) return false;
    model_->index_tag(id_, tag, true);
    model_->notify_tag(*this, tag, true);
    return true;
}

bool Instance::remove_tag(std::string_view tag) {
    auto it = tags_.find(tag);
    if (it == tags_.end()) return false;
    const std::string key = *it;
    tags_.erase(it);
    model_->index_tag(id_, key, false);
    model_->notify_tag(*this, key, false);
    return true;
}

bool Instance::has_tag(std::string_view tag) const { return tags_.find(tag) != tags_.end(); }

// ---- DataModel ----------------------------------------------------------------------------------------------

DataModel::DataModel() {
    ClassRegistry& reg = ClassRegistry::get();
    root_ = make(reg.find("DataModel"), next_id_++);
    root_->props_[0].s = "Game";
    for (const ClassInfo* c : reg.all())
        if (c->service) {
            Instance* s = make(c, next_id_++);
            s->props_[0].s = c->name;
            attach(s, root_);
        }
}

DataModel::~DataModel() = default;

Instance* DataModel::make(const ClassInfo* cls, u64 id) {
    auto inst = std::unique_ptr<Instance>(new Instance());
    inst->id_ = id;
    inst->cls_ = cls;
    inst->model_ = this;
    inst->props_.resize(cls->prop_count());
    for (u32 i = 0; i < cls->prop_count(); ++i) inst->props_[i] = cls->prop(i)->def;
    inst->props_[0].s = cls->name;
    Instance* raw = inst.get();
    instances_[id] = std::move(inst);
    if (id >= next_id_) next_id_ = id + 1;
    return raw;
}

Instance* DataModel::service(std::string_view class_name) const {
    for (Instance* c : root_->children_)
        if (c->cls_->service && c->cls_->name == class_name) return c;
    return nullptr;
}

Instance* DataModel::create(std::string_view class_name, Instance* parent, std::string name) {
    const ClassInfo* cls = ClassRegistry::get().find(class_name);
    if (!cls || !cls->creatable) return nullptr;
    return create_internal(class_name, parent, std::move(name));
}

Instance* DataModel::create_internal(std::string_view class_name, Instance* parent, std::string name) {
    const ClassInfo* cls = ClassRegistry::get().find(class_name);
    if (!cls || cls->is_abstract || cls->service) return nullptr;
    Instance* inst = make(cls, next_id_++);
    if (!name.empty()) inst->props_[0].s = std::move(name);
    ++revision_;
    if (parent) attach(inst, parent);
    return inst;
}

void DataModel::attach(Instance* inst, Instance* parent) {
    inst->parent_ = parent;
    parent->children_.push_back(inst);
    ++revision_;
    notify_added(*inst);
}

void DataModel::detach(Instance* inst) {
    if (!inst->parent_) return;
    notify_removing(*inst);
    auto& v = inst->parent_->children_;
    v.erase(std::remove(v.begin(), v.end(), inst), v.end());
    inst->parent_ = nullptr;
    ++revision_;
}

bool DataModel::set_parent(Instance* inst, Instance* parent) {
    if (!inst || inst == root_ || inst->cls_->service) return false;
    if (parent && (parent == inst || parent->is_descendant_of(inst))) return false;
    if (parent && parent->model_ != this) return false;
    Instance* old = inst->parent_;
    if (old == parent) return true;
    if (old && parent) {
        // Reparent inside the tree: no removing/added cycle, a single event.
        auto& v = old->children_;
        v.erase(std::remove(v.begin(), v.end(), inst), v.end());
        inst->parent_ = parent;
        parent->children_.push_back(inst);
        ++revision_;
        for (auto& l : listeners_)
            if (l && l->reparented) l->reparented(*inst, old);
        return true;
    }
    if (old) detach(inst);
    if (parent) attach(inst, parent);
    return true;
}

bool DataModel::destroy(Instance* inst) {
    if (!inst || inst == root_ || inst->cls_->service || inst->model_ != this) return false;
    detach(inst);
    // Collect the subtree, then clear references to it from the rest of the model.
    std::vector<Instance*> dead = inst->descendants();
    dead.push_back(inst);
    for (Instance* d : dead) {
        for (auto& [id, other] : instances_) {
            for (u32 i = 0; i < other->cls_->prop_count(); ++i)
                if (other->props_[i].type == PropType::Ref && other->props_[i].ref == d->id_) other->props_[i].ref = 0;
        }
    }
    for (Instance* d : dead) {
        for (const std::string& t : d->tags_) index_tag(d->id_, t, false);
        instances_.erase(d->id_);
    }
    ++revision_;
    return true;
}

void DataModel::set_skin_pose(u64 part_id, std::vector<mat4> palette) {
    SkinPose& p = skin_poses_[part_id];
    p.palette = std::move(palette);
    ++p.version;
}

const DataModel::SkinPose* DataModel::skin_pose(u64 part_id) const {
    auto it = skin_poses_.find(part_id);
    return it == skin_poses_.end() ? nullptr : &it->second;
}

Instance* DataModel::find(u64 id) const {
    auto it = instances_.find(id);
    return it == instances_.end() ? nullptr : it->second.get();
}

Instance* DataModel::find_path(std::string_view path) const {
    Instance* cur = root_;
    usize start = 0;
    bool first = true;
    while (start <= path.size()) {
        usize dot = path.find('.', start);
        if (dot == std::string_view::npos) dot = path.size();
        const std::string_view part = path.substr(start, dot - start);
        if (!(first && (part == "game" || part == root_->name()))) {
            cur = cur->find_first_child(part);
            if (!cur) return nullptr;
        }
        first = false;
        start = dot + 1;
    }
    return cur;
}

usize DataModel::add_listener(WorldListener l) {
    listeners_.push_back(std::make_unique<WorldListener>(std::move(l)));
    return listeners_.size() - 1;
}

void DataModel::remove_listener(usize handle) {
    if (handle < listeners_.size()) listeners_[handle].reset();
}

void DataModel::fire_event(Instance& source, std::string_view name, Instance* other, f64 number,
                           std::string_view text) {
    GameEvent e;
    e.source = &source;
    e.name = name;
    e.other = other;
    e.number = number;
    e.text = text;
    // By index: a listener may add listeners while handling the event.
    for (usize i = 0; i < listeners_.size(); ++i)
        if (listeners_[i] && listeners_[i]->event) listeners_[i]->event(e);
}

void DataModel::notify_changed(Instance& inst, u32 prop) {
    ++revision_;
    if (!inst.parent_ && &inst != root_) return; // detached: nobody observes it
    for (auto& l : listeners_)
        if (l && l->changed) l->changed(inst, prop);
}

void DataModel::notify_attribute(Instance& inst, std::string_view name) {
    ++revision_;
    if (!inst.parent_ && &inst != root_) return; // detached: nobody observes it
    fire_event(inst, "AttributeChanged", nullptr, 0.0, name);
}

void DataModel::notify_tag(Instance& inst, std::string_view tag, bool added) {
    ++revision_;
    if (!inst.parent_ && &inst != root_) return;
    fire_event(inst, added ? "TagAdded" : "TagRemoved", nullptr, 0.0, tag);
}

void DataModel::index_tag(u64 id, std::string_view tag, bool add) {
    if (add) {
        tag_index_.try_emplace(std::string(tag)).first->second.insert(id);
        return;
    }
    auto it = tag_index_.find(tag);
    if (it == tag_index_.end()) return;
    it->second.erase(id);
    if (it->second.empty()) tag_index_.erase(it);
}

std::vector<Instance*> DataModel::tagged(std::string_view tag) const {
    std::vector<Instance*> out;
    auto it = tag_index_.find(tag);
    if (it == tag_index_.end()) return out;
    for (u64 id : it->second) {
        Instance* inst = find(id);
        if (!inst) continue;
        bool attached = inst == root_;
        for (const Instance* p = inst->parent_; !attached && p; p = p->parent_) attached = p == root_;
        if (attached) out.push_back(inst);
    }
    return out;
}

void DataModel::notify_added(Instance& inst) {
    for (auto& l : listeners_)
        if (l && l->added) l->added(inst);
    for (Instance* c : inst.children_) notify_added(*c);
}

void DataModel::notify_removing(Instance& inst) {
    for (Instance* c : inst.children_) notify_removing(*c);
    for (auto& l : listeners_)
        if (l && l->removing) l->removing(inst);
}

Instance* DataModel::clone(const Instance& src, Instance* parent) {
    std::unordered_map<u64, u64> remap;
    std::vector<Instance*> created;
    std::function<Instance*(const Instance&)> copy = [&](const Instance& s) -> Instance* {
        Instance* d = make(s.cls_, next_id_++);
        d->props_ = s.props_;
        d->attributes_ = s.attributes_;
        d->tags_ = s.tags_;
        for (const std::string& t : d->tags_) index_tag(d->id_, t, true);
        remap[s.id_] = d->id_;
        created.push_back(d);
        for (const Instance* c : s.children_) {
            Instance* cc = copy(*c);
            cc->parent_ = d;
            d->children_.push_back(cc);
        }
        return d;
    };
    Instance* top = copy(src);
    for (Instance* d : created)
        for (Value& v : d->props_)
            if (v.type == PropType::Ref) {
                auto it = remap.find(v.ref);
                if (it != remap.end()) v.ref = it->second;
            }
    ++revision_;
    if (parent) attach(top, parent);
    return top;
}

// ---- Serialization ------------------------------------------------------------------------------------------

JsonValue DataModel::value_to_json(const PropInfo& p, const Value& v) {
    switch (p.type) {
    case PropType::Bool: return JsonValue(v.as_bool());
    case PropType::Int: return JsonValue(static_cast<i64>(v.n[0]));
    case PropType::Float: return JsonValue(v.n[0]);
    case PropType::String: return JsonValue(v.s);
    case PropType::Vector3:
    case PropType::Color3: {
        JsonValue a = JsonValue::array();
        for (int i = 0; i < 3; ++i) a.push(JsonValue(v.n[i]));
        return a;
    }
    case PropType::Enum:
        if (p.enum_info && v.as_enum() < p.enum_info->items.size()) return JsonValue(p.enum_info->items[v.as_enum()]);
        return JsonValue(static_cast<i64>(v.n[0]));
    case PropType::Ref: return v.ref ? JsonValue(v.ref) : JsonValue();
    }
    return JsonValue();
}

bool DataModel::value_from_json(const PropInfo& p, const JsonValue& j, Value& out) {
    out = p.def;
    out.type = p.type;
    switch (p.type) {
    case PropType::Bool:
        if (!j.is_bool()) return false;
        out.n[0] = j.as_bool() ? 1 : 0;
        return true;
    case PropType::Int:
    case PropType::Float:
        if (!j.is_number()) return false;
        out.n[0] = p.type == PropType::Int ? f64(j.as_int()) : j.as_number();
        return true;
    case PropType::String:
        if (!j.is_string()) return false;
        out.s = j.as_string();
        return true;
    case PropType::Vector3:
    case PropType::Color3:
        if (!j.is_array() || j.size() != 3) return false;
        for (usize i = 0; i < 3; ++i) {
            if (!j.at(i).is_number()) return false;
            out.n[i] = j.at(i).as_number();
        }
        return true;
    case PropType::Enum:
        if (j.is_string() && p.enum_info) {
            const i32 idx = p.enum_info->index_of(j.as_string());
            if (idx < 0) return false;
            out.n[0] = idx;
            return true;
        }
        if (j.is_number()) {
            out.n[0] = f64(j.as_int());
            return !p.enum_info || u64(j.as_int()) < p.enum_info->items.size();
        }
        return false;
    case PropType::Ref:
        if (j.is_null()) {
            out.ref = 0;
            return true;
        }
        if (!j.is_number()) return false;
        out.ref = static_cast<u64>(j.as_int());
        return true;
    }
    return false;
}

JsonValue DataModel::attribute_to_json(const Value& v) {
    switch (v.type) {
    case PropType::Bool: return JsonValue(v.as_bool());
    case PropType::Int: return JsonValue(static_cast<i64>(v.n[0]));
    case PropType::Float: return JsonValue(v.n[0]);
    case PropType::String: return JsonValue(v.s);
    case PropType::Vector3:
    case PropType::Color3: {
        JsonValue a = JsonValue::array();
        for (int i = 0; i < 3; ++i) a.push(JsonValue(v.n[i]));
        if (v.type == PropType::Vector3) return a;
        JsonValue o = JsonValue::object();
        o.set("Color3", std::move(a));
        return o;
    }
    case PropType::Enum:
    case PropType::Ref: break;
    }
    return JsonValue();
}

bool DataModel::attribute_from_json(const JsonValue& j, Value& out) {
    // Three numbers as a Vector3 or Color3 payload.
    auto triple = [](const JsonValue& a, Value& v) {
        if (!a.is_array() || a.size() != 3) return false;
        for (usize i = 0; i < 3; ++i) {
            if (!a.at(i).is_number()) return false;
            v.n[i] = a.at(i).as_number();
        }
        return true;
    };
    if (j.is_bool()) {
        out = Value::boolean(j.as_bool());
        return true;
    }
    if (j.is_number()) {
        const f64 n = j.as_number();
        if (!std::isfinite(n)) return false;
        // JSON has one number type: whole numbers come back as Int, others as Float.
        if (n == std::floor(n) && std::fabs(n) < 9.0e15)
            out = Value::integer(static_cast<i64>(n));
        else
            out = Value::number(n);
        return true;
    }
    if (j.is_string()) {
        out = Value::string(j.as_string());
        return true;
    }
    if (j.is_array()) {
        Value v;
        v.type = PropType::Vector3;
        if (!triple(j, v)) return false;
        out = v;
        return true;
    }
    if (j.is_object()) {
        Value v;
        v.type = PropType::Color3;
        if (!triple(j["Color3"], v)) return false;
        out = v;
        return true;
    }
    return false;
}

JsonValue DataModel::instance_to_json(const Instance& inst, bool with_children) const {
    JsonValue j = JsonValue::object();
    j.set("class", inst.cls_->name);
    j.set("id", inst.id_);
    JsonValue props = JsonValue::object();
    for (u32 i = 0; i < inst.cls_->prop_count(); ++i) {
        const PropInfo* p = inst.cls_->prop(i);
        if (p->flags & kPropNotSerialized) continue;
        if (i != 0 && inst.props_[i] == p->def) continue; // defaults are implicit (Name always written)
        props.set(p->name, value_to_json(*p, inst.props_[i]));
    }
    j.set("props", std::move(props));
    // Optional keys: files written before attributes and tags existed simply lack them.
    if (!inst.attributes_.empty()) {
        JsonValue attrs = JsonValue::object();
        for (const auto& [name, v] : inst.attributes_) attrs.set(name, attribute_to_json(v));
        j.set("attributes", std::move(attrs));
    }
    if (!inst.tags_.empty()) {
        JsonValue tags = JsonValue::array();
        for (const std::string& t : inst.tags_) tags.push(JsonValue(t));
        j.set("tags", std::move(tags));
    }
    if (with_children && !inst.children_.empty()) {
        JsonValue ch = JsonValue::array();
        for (const Instance* c : inst.children_) ch.push(instance_to_json(*c, true));
        j.set("children", std::move(ch));
    }
    return j;
}

Instance* DataModel::instance_from_json(const JsonValue& j, Instance* parent, std::string* error) {
    const ClassInfo* cls = ClassRegistry::get().find(j["class"].as_string());
    if (!cls || cls->service || cls == root_->cls_) {
        if (error) *error = "unknown or non-creatable class '" + j["class"].as_string() + "'";
        return nullptr;
    }
    u64 id = static_cast<u64>(j["id"].as_int(0));
    if (id == 0 || instances_.count(id)) id = next_id_++;
    Instance* inst = make(cls, id);
    for (const auto& [key, val] : j["props"].members()) {
        const PropInfo* p = cls->find_prop(key);
        if (!p) {
            TEFRA_LOG_WARN("%s: unknown property '%s' ignored", cls->name.c_str(), key.c_str());
            continue;
        }
        Value v;
        if (!value_from_json(*p, val, v)) {
            TEFRA_LOG_WARN("%s.%s: bad value ignored", cls->name.c_str(), key.c_str());
            continue;
        }
        inst->props_[p->index] = std::move(v);
    }
    if (inst->props_[0].s.empty()) inst->props_[0].s = cls->name;
    if (const JsonValue* attrs = j.find("attributes"); attrs && attrs->is_object()) {
        for (const auto& [key, val] : attrs->members()) {
            Value v;
            if (key.empty() || !attribute_from_json(val, v)) {
                TEFRA_LOG_WARN("%s: attribute '%s' ignored (unsupported value)", cls->name.c_str(), key.c_str());
                continue;
            }
            inst->attributes_[key] = std::move(v);
        }
    }
    if (const JsonValue* tags = j.find("tags"); tags && tags->is_array()) {
        for (const JsonValue& t : tags->items()) {
            if (!t.is_string() || t.as_string().empty()) continue;
            if (inst->tags_.emplace(t.as_string()).second) index_tag(inst->id_, t.as_string(), true);
        }
    }
    for (const JsonValue& c : j["children"].items()) {
        Instance* child = instance_from_json(c, nullptr, error);
        if (!child) continue;
        child->parent_ = inst;
        inst->children_.push_back(child);
    }
    ++revision_;
    if (parent) attach(inst, parent);
    return inst;
}

void DataModel::clear_services() {
    for (Instance* s : root_->children_)
        while (!s->children_.empty()) destroy(s->children_.back());
}

JsonValue DataModel::to_json() const {
    JsonValue doc = JsonValue::object();
    doc.set("format", "tefra-place");
    doc.set("version", 1);
    JsonValue services = JsonValue::array();
    for (const Instance* s : root_->children_) {
        JsonValue sj = instance_to_json(*s, true);
        services.push(std::move(sj));
    }
    doc.set("services", std::move(services));
    return doc;
}

bool DataModel::from_json(const JsonValue& doc, std::string* error) {
    if (doc["format"].as_string() != "tefra-place") {
        if (error) *error = "not a tefra-place document";
        return false;
    }
    clear_services();
    for (const JsonValue& sj : doc["services"].items()) {
        Instance* s = service(sj["class"].as_string());
        if (!s) continue;
        for (const auto& [key, val] : sj["props"].members()) {
            const PropInfo* p = s->cls_->find_prop(key);
            Value v;
            if (p && p->index != 0 && value_from_json(*p, val, v)) s->set(p->index, std::move(v), true);
        }
        for (const JsonValue& c : sj["children"].items()) instance_from_json(c, s, error);
    }
    return true;
}

bool DataModel::save(const char* path) const {
    const std::string text = to_json().dump(true);
    const std::string dir = path_parent(path);
    if (!dir.empty()) make_dirs(dir.c_str()); // the project folder may have been removed or never created
    return file_write_all(path, text.data(), text.size());
}

bool DataModel::load(const char* path, std::string* error) {
    std::vector<u8> bytes;
    if (!file_read_all(path, bytes)) {
        if (error) *error = std::string("cannot read ") + path;
        return false;
    }
    const std::string text(bytes.begin(), bytes.end());
    JsonValue doc;
    if (!JsonValue::parse(text, doc, error)) return false;
    return from_json(doc, error);
}

// ---- Orientation helpers ------------------------------------------------------------------------------------

quat orientation_to_quat(vec3 d) {
    const quat qy = quat_axis_angle({0, 1, 0}, radians(d.y));
    const quat qx = quat_axis_angle({1, 0, 0}, radians(d.x));
    const quat qz = quat_axis_angle({0, 0, 1}, radians(d.z));
    return qy * qx * qz;
}

vec3 quat_to_orientation(quat q) {
    // Inverse of R = Ry * Rx * Rz. Matrix elements from the quaternion.
    const f32 r12 = 2.0f * (q.y * q.z - q.w * q.x); // m[1][2] row 1, col 2
    const f32 sx = clamp(-r12, -1.0f, 1.0f);
    const f32 x = std::asin(sx);
    const f32 r02 = 2.0f * (q.x * q.z + q.w * q.y);
    const f32 r22 = 1.0f - 2.0f * (q.x * q.x + q.y * q.y);
    const f32 r10 = 2.0f * (q.x * q.y + q.w * q.z);
    const f32 r11 = 1.0f - 2.0f * (q.x * q.x + q.z * q.z);
    const f32 y = std::atan2(r02, r22);
    const f32 z = std::atan2(r10, r11);
    return {degrees(x), degrees(y), degrees(z)};
}

} // namespace tefra::world
