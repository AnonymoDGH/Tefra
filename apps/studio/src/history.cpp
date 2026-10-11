#include "tefra/studio/history.h"

namespace tefra::studio {

namespace {
const std::string kEmpty;
} // namespace

bool apply_op(world::DataModel& model, const Op& op, bool forward) {
    using Kind = Op::Kind;
    switch (op.kind) {
    case Kind::SetProperty: {
        world::Instance* inst = model.find(op.id);
        return inst && inst->set(op.prop, forward ? op.after : op.before, true);
    }
    case Kind::Create:
    case Kind::Destroy: {
        // Create forward == Destroy backward: the subtree must exist afterwards.
        const bool make = (op.kind == Kind::Create) == forward;
        if (make) {
            world::Instance* parent = op.parent ? model.find(op.parent) : nullptr;
            if (op.parent && !parent) return false;
            world::Instance* inst = model.instance_from_json(op.snapshot, parent);
            if (!inst) return false;
            for (const RefFix& f : op.refs)
                if (world::Instance* h = model.find(f.holder)) h->set(f.prop, world::Value::reference(f.ref), true);
            return true;
        }
        world::Instance* inst = model.find(op.id);
        return inst && model.destroy(inst);
    }
    case Kind::Reparent: {
        world::Instance* inst = model.find(op.id);
        if (!inst) return false;
        const u64 target = forward ? op.parent : op.old_parent;
        world::Instance* parent = target ? model.find(target) : nullptr;
        if (target && !parent) return false;
        return model.set_parent(inst, parent);
    }
    }
    return false;
}

Op capture_create(const world::DataModel& model, const world::Instance& inst) {
    Op op;
    op.kind = Op::Kind::Create;
    op.id = inst.id();
    op.parent = inst.parent() ? inst.parent()->id() : 0;
    op.snapshot = model.instance_to_json(inst, true);
    return op;
}

Op capture_destroy(const world::DataModel& model, const world::Instance& inst) {
    Op op;
    op.kind = Op::Kind::Destroy;
    op.id = inst.id();
    op.parent = inst.parent() ? inst.parent()->id() : 0;
    op.snapshot = model.instance_to_json(inst, true);
    // References from outside the subtree into it (they are cleared by destroy()).
    std::vector<u64> ids{inst.id()};
    for (const world::Instance* d : inst.descendants()) ids.push_back(d->id());
    auto in_subtree = [&](u64 id) {
        for (u64 x : ids)
            if (x == id) return true;
        return false;
    };
    std::vector<const world::Instance*> stack{&model.root()};
    while (!stack.empty()) {
        const world::Instance* h = stack.back();
        stack.pop_back();
        for (const world::Instance* c : h->children()) stack.push_back(c);
        if (in_subtree(h->id())) continue;
        for (u32 i = 0; i < h->cls().prop_count(); ++i) {
            const world::Value& v = h->get(i);
            if (v.type == world::PropType::Ref && v.ref && in_subtree(v.ref)) op.refs.push_back({h->id(), i, v.ref});
        }
    }
    return op;
}

void History::push(Action action) {
    if (action.ops.empty()) return;
    undo_.push_back(std::move(action));
    redo_.clear();
    if (undo_.size() > 500) undo_.erase(undo_.begin());
}

bool History::undo(world::DataModel& model, std::string* label) {
    if (undo_.empty()) return false;
    Action a = std::move(undo_.back());
    undo_.pop_back();
    bool ok = true;
    for (auto it = a.ops.rbegin(); it != a.ops.rend(); ++it) ok = apply_op(model, *it, false) && ok;
    if (label) *label = a.label;
    redo_.push_back(std::move(a));
    return ok;
}

bool History::redo(world::DataModel& model, std::string* label) {
    if (redo_.empty()) return false;
    Action a = std::move(redo_.back());
    redo_.pop_back();
    bool ok = true;
    for (const Op& op : a.ops) ok = apply_op(model, op, true) && ok;
    if (label) *label = a.label;
    undo_.push_back(std::move(a));
    return ok;
}

const std::string& History::undo_label() const { return undo_.empty() ? kEmpty : undo_.back().label; }
const std::string& History::redo_label() const { return redo_.empty() ? kEmpty : redo_.back().label; }

void History::clear() {
    undo_.clear();
    redo_.clear();
}

} // namespace tefra::studio
