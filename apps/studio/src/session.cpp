#include "tefra/studio/session.h"

#include "tefra/core/log.h"

#include <algorithm>

namespace tefra::studio {

using world::Instance;
using world::Value;

void build_baseplate(world::DataModel& m) {
    Instance* ws = m.workspace();
    Instance* base = m.create("Part", ws, "Baseplate");
    base->set("Size", Value::vector3({256, 1, 256}));
    base->set("Position", Value::vector3({0, -0.5f, 0}));
    base->set("Anchored", Value::boolean(true));
    base->set("Color", Value::color3({0.39f, 0.40f, 0.42f}));
    base->set("Material", Value::enumeration(6)); // Concrete
    Instance* spawn = m.create("SpawnLocation", ws, "SpawnLocation");
    spawn->set("Size", Value::vector3({6, 0.4f, 6}));
    spawn->set("Position", Value::vector3({0, 0.2f, 0}));
    spawn->set("Anchored", Value::boolean(true));
    spawn->set("Color", Value::color3({0.15f, 0.45f, 0.85f}));
    spawn->set("Material", Value::enumeration(1));
    m.service("Lighting")->set("ClockTime", Value::number(14.0));
}

Session::Session() { new_place(true); }

void Session::new_place(bool baseplate) {
    if (state_ != PlayState::Edit) stop();
    model_ = std::make_unique<world::DataModel>();
    ++generation_;
    if (baseplate) build_baseplate(*model_);
    path_.clear();
    selection_.clear();
    history_.clear();
    saved_revision_ = model_->revision();
}

bool Session::open(const std::string& path, std::string* error) {
    if (state_ != PlayState::Edit) stop();
    auto m = std::make_unique<world::DataModel>();
    if (!m->load(path.c_str(), error)) return false;
    model_ = std::move(m);
    ++generation_;
    path_ = path;
    selection_.clear();
    history_.clear();
    saved_revision_ = model_->revision();
    TEFRA_LOG_INFO("Opened %s", path.c_str());
    return true;
}

bool Session::save(const std::string& path) {
    const std::string p = path.empty() ? path_ : path;
    if (p.empty() || state_ != PlayState::Edit) return false;
    if (!model_->save(p.c_str())) {
        TEFRA_LOG_ERROR("Cannot save %s", p.c_str());
        return false;
    }
    path_ = p;
    saved_revision_ = model_->revision();
    TEFRA_LOG_INFO("Saved %s", p.c_str());
    return true;
}

// ---- Selection ----------------------------------------------------------------------------------------------

Instance* Session::primary() const { return selection_.empty() ? nullptr : model_->find(selection_.back()); }

bool Session::is_selected(u64 id) const {
    return std::find(selection_.begin(), selection_.end(), id) != selection_.end();
}

void Session::select(u64 id, bool additive) {
    if (!additive) {
        selection_.assign(1, id);
        return;
    }
    auto it = std::find(selection_.begin(), selection_.end(), id);
    if (it != selection_.end())
        selection_.erase(it);
    else
        selection_.push_back(id);
}

void Session::select_only(const std::vector<u64>& ids) { selection_ = ids; }

void Session::prune_selection() {
    selection_.erase(std::remove_if(selection_.begin(), selection_.end(), [&](u64 id) { return !model_->find(id); }),
                     selection_.end());
}

// ---- Editing --------------------------------------------------------------------------------------------------

void Session::record(Action a) {
    if (state_ != PlayState::Edit) return; // play-mode edits are discarded on Stop
    history_.push(std::move(a));
}

Instance* Session::create(const std::string& class_name, Instance* parent, const std::string& name) {
    Instance* inst = model_->create(class_name, parent ? parent : model_->workspace(), name);
    if (!inst) return nullptr;
    // Sensible defaults for inserted parts: in front of the origin, anchored, a pleasant colour.
    if (inst->is_a("BasePart")) {
        inst->set("Anchored", Value::boolean(true));
        inst->set("Position", Value::vector3({0, 2.5f, 0}));
        if (inst->is_a("MeshPart")) inst->set("Size", Value::vector3({1, 1, 1}));
    }
    if (inst->is_a("BaseScript") || inst->is_a("ModuleScript")) {
        const std::string cls = inst->name();
        inst->set("Source",
                  Value::string("using Tefra;\n\npublic class " + cls +
                                " : TefraScript\n{\n"
                                "    public override void Start()\n    {\n        Debug.Log(\"Hello from \" + "
                                "Script.Name);\n    }\n\n    public override void Update(float dt)\n    {\n"
                                "    }\n}\n"));
    }
    Action a;
    a.label = "Insert " + class_name;
    a.ops.push_back(capture_create(*model_, *inst));
    record(std::move(a));
    select(inst->id());
    return inst;
}

bool Session::destroy_selection() {
    Action a;
    a.label = "Delete";
    // Skip instances whose ancestor is also selected (destroyed with it).
    std::vector<Instance*> victims;
    for (u64 id : selection_) {
        Instance* i = model_->find(id);
        if (!i || i->cls().service || i == &model_->root()) continue;
        bool covered = false;
        for (u64 other : selection_) {
            Instance* o = model_->find(other);
            if (o && o != i && i->is_descendant_of(o)) covered = true;
        }
        if (!covered) victims.push_back(i);
    }
    for (Instance* v : victims) {
        a.ops.push_back(capture_destroy(*model_, *v));
        model_->destroy(v);
    }
    selection_.clear();
    const bool any = !a.ops.empty();
    record(std::move(a));
    return any;
}

std::vector<u64> Session::duplicate_selection() {
    Action a;
    a.label = "Duplicate";
    std::vector<u64> out;
    for (u64 id : selection_) {
        Instance* i = model_->find(id);
        if (!i || i->cls().service || !i->parent()) continue;
        Instance* c = model_->clone(*i, i->parent());
        if (!c) continue;
        a.ops.push_back(capture_create(*model_, *c));
        out.push_back(c->id());
    }
    record(std::move(a));
    if (!out.empty()) selection_ = out;
    return out;
}

Instance* Session::group_selection() {
    prune_selection();
    if (selection_.empty()) return nullptr;
    Instance* first = model_->find(selection_.front());
    Instance* parent = first && first->parent() ? first->parent() : model_->workspace();
    Instance* group = model_->create("Model", parent, "Model");
    Action a;
    a.label = "Group";
    a.ops.push_back(capture_create(*model_, *group));
    for (u64 id : selection_) {
        Instance* i = model_->find(id);
        if (!i || i == group || i->cls().service) continue;
        Op op;
        op.kind = Op::Kind::Reparent;
        op.id = id;
        op.old_parent = i->parent() ? i->parent()->id() : 0;
        op.parent = group->id();
        if (model_->set_parent(i, group)) a.ops.push_back(op);
        if (i->is_a("BasePart") && !group->get("PrimaryPart")->ref)
            group->set("PrimaryPart", Value::reference(i->id()));
    }
    record(std::move(a));
    select(group->id());
    return group;
}

bool Session::reparent(Instance* inst, Instance* parent) {
    if (!inst || !parent) return false;
    Op op;
    op.kind = Op::Kind::Reparent;
    op.id = inst->id();
    op.old_parent = inst->parent() ? inst->parent()->id() : 0;
    op.parent = parent->id();
    if (!model_->set_parent(inst, parent)) return false;
    Action a;
    a.label = "Move " + inst->name();
    a.ops.push_back(op);
    record(std::move(a));
    return true;
}

bool Session::rename(Instance* inst, const std::string& name) {
    return inst && !name.empty() && set_property(inst, 0, Value::string(name), "Rename");
}

bool Session::set_property(Instance* inst, u32 prop, const Value& v, const char* label) {
    if (!inst) return false;
    Op op;
    op.kind = Op::Kind::SetProperty;
    op.id = inst->id();
    op.prop = prop;
    op.before = inst->get(prop);
    if (!inst->set(prop, v)) return false;
    op.after = inst->get(prop);
    if (op.before == op.after) return true;
    Action a;
    a.label = label ? label : ("Change " + inst->cls().prop(prop)->name);
    a.ops.push_back(std::move(op));
    record(std::move(a));
    return true;
}

void Session::begin_edit(const char* label) {
    if (editing_) end_edit();
    editing_ = true;
    gesture_ = Action{};
    gesture_.label = label;
}

void Session::track(Instance* inst, u32 prop) {
    if (!editing_ || !inst) return;
    for (const Op& op : gesture_.ops)
        if (op.id == inst->id() && op.prop == prop) return;
    Op op;
    op.kind = Op::Kind::SetProperty;
    op.id = inst->id();
    op.prop = prop;
    op.before = inst->get(prop);
    gesture_.ops.push_back(std::move(op));
}

void Session::set_live(Instance* inst, u32 prop, const Value& v) {
    if (!inst) return;
    track(inst, prop);
    inst->set(prop, v);
}

void Session::end_edit() {
    if (!editing_) return;
    editing_ = false;
    Action a;
    a.label = gesture_.label;
    for (Op& op : gesture_.ops) {
        Instance* inst = model_->find(op.id);
        if (!inst) continue;
        op.after = inst->get(op.prop);
        if (!(op.after == op.before)) a.ops.push_back(std::move(op));
    }
    record(std::move(a));
    gesture_ = Action{};
}

bool Session::undo() {
    if (state_ != PlayState::Edit) return false;
    std::string label;
    const bool ok = history_.undo(*model_, &label);
    prune_selection();
    if (!label.empty()) TEFRA_LOG_INFO("Undo: %s", label.c_str());
    return ok;
}

bool Session::redo() {
    if (state_ != PlayState::Edit) return false;
    std::string label;
    const bool ok = history_.redo(*model_, &label);
    prune_selection();
    if (!label.empty()) TEFRA_LOG_INFO("Redo: %s", label.c_str());
    return ok;
}

// ---- Play mode ------------------------------------------------------------------------------------------------

void Session::play() {
    if (state_ == PlayState::Paused) {
        state_ = PlayState::Playing;
        return;
    }
    if (state_ != PlayState::Edit) return;
    if (editing_) end_edit();
    snapshot_ = model_->to_json();
    snapshot_selection_ = selection_;
    state_ = PlayState::Playing;
    play_time_ = 0.0;
    hook_running_.assign(hooks_.size(), false);
    TEFRA_LOG_INFO("Play");
    for (usize i = 0; i < hooks_.size(); ++i) {
        if (!hooks_[i].start) {
            hook_running_[i] = true;
            continue;
        }
        hook_running_[i] = hooks_[i].start(*model_);
        if (!hook_running_[i]) TEFRA_LOG_WARN("Play: '%s' did not start", hooks_[i].name.c_str());
    }
}

void Session::pause(bool paused) {
    if (state_ == PlayState::Edit) return;
    state_ = paused ? PlayState::Paused : PlayState::Playing;
}

void Session::stop() {
    if (state_ == PlayState::Edit) return;
    for (usize i = hooks_.size(); i-- > 0;)
        if (i < hook_running_.size() && hook_running_[i] && hooks_[i].stop) hooks_[i].stop();
    hook_running_.clear();
    state_ = PlayState::Edit;
    // Restore the edit-mode place (ids are preserved, so the selection survives).
    auto m = std::make_unique<world::DataModel>();
    std::string err;
    if (m->from_json(snapshot_, &err)) {
        const u64 saved_delta = model_->revision() - saved_revision_;
        model_ = std::move(m);
        ++generation_;
        saved_revision_ = saved_delta == 0 ? model_->revision() : 0;
    } else {
        TEFRA_LOG_ERROR("Stop: cannot restore the place: %s", err.c_str());
    }
    selection_ = snapshot_selection_;
    prune_selection();
    snapshot_ = JsonValue();
    TEFRA_LOG_INFO("Stop (%.1f s of play)", play_time_);
}

void Session::step(f32 dt) {
    if (state_ != PlayState::Playing) return;
    play_time_ += dt;
    for (usize i = 0; i < hooks_.size(); ++i)
        if (i < hook_running_.size() && hook_running_[i] && hooks_[i].step) hooks_[i].step(*model_, dt);
}

} // namespace tefra::studio
