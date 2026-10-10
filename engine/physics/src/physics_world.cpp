// PhysicsWorld: BaseParts <-> Jolt bodies (spec M3, ADR-014). Jolt is built without exceptions or RTTI.
#include "internal.h"

#include "tefra/core/log.h"

#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>

#include <cmath>
#include <cstdarg>
#include <cstdio>

namespace tefra::physics {

using detail::from_jolt;
using detail::to_jolt;

namespace detail {
namespace {

constexpr usize kTempBytes = 16u * 1024u * 1024u;
constexpr u32 kMaxJobs = 2048;
constexpr u32 kMaxBarriers = 16;
constexpr int kJobThreads = 2; // leaves cores for the render thread on a 2C/4T machine
constexpr u32 kMaxBodies = 8192;
constexpr u32 kMaxBodyPairs = 16384;
constexpr u32 kMaxContacts = 8192;
constexpr u32 kMaxSubsteps = 8;
constexpr f32 kMaxFrameSeconds = 0.25f;
constexpr f32 kMinSize = 0.02f;
constexpr f32 kDefaultFallLimit = -500.0f;

// Shape kinds of the Part.Shape enum (world/src/classes.cpp: Block, Ball, Cylinder, Wedge).
enum ShapeKind : u32 { kBlock = 0, kBall = 1, kCylinder = 2, kWedge = 3 };

void trace_impl(const char* fmt, ...) TEFRA_PRINTF(1, 2);
void trace_impl(const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    TEFRA_LOG_INFO("jolt: %s", buf);
}

#ifdef JPH_ENABLE_ASSERTS
bool assert_impl(const char* expr, const char* msg, const char* file, u32 line) {
    TEFRA_LOG_ERROR("jolt assert failed: %s (%s) at %s:%u", expr, msg ? msg : "", file, line);
    return true; // break
}
#endif

// Jolt needs its allocator and diagnostics hooks before the first use. Idempotent.
// Jolt's global setup: allocator, diagnostics, the factory and the collision dispatch tables (RegisterTypes fills
// the shape-vs-shape tables; without it the first Update calls null functions).
void register_jolt_hooks() {
    static std::once_flag once;
    std::call_once(once, [] {
        JPH::RegisterDefaultAllocator();
        JPH::Trace = trace_impl;
#ifdef JPH_ENABLE_ASSERTS
        JPH::AssertFailed = assert_impl;
#endif
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
    });
}

bool near(f32 a, f32 b) { return std::fabs(a - b) < 1e-4f; }

// Ignores sensors: they do not block rays (they never collide).
class SolidOnlyFilter final : public JPH::BodyFilter {
  public:
    bool ShouldCollideLocked(const JPH::Body& body) const override { return !body.IsSensor(); }
};

// Collision shape for a part. The Part.Shape enum picks the primitive; MeshPart (no Shape) is a box for now.
JPH::RefConst<JPH::Shape> make_shape(const world::Instance& part, vec3 size, f32 density) {
    size = {clamp(size.x, kMinSize, 1.0e4f), clamp(size.y, kMinSize, 1.0e4f), clamp(size.z, kMinSize, 1.0e4f)};
    const world::Value* shape_prop = part.get("Shape");
    const u32 kind = shape_prop ? shape_prop->as_enum() : kBlock;
    const vec3 h = size * 0.5f;
    JPH::Shape::ShapeResult result;
    switch (kind) {
    case kBall:
        if (near(size.x, size.y) && near(size.y, size.z)) {
            JPH::SphereShapeSettings s(h.x);
            s.mDensity = density;
            result = s.Create();
        } else { // unit sphere scaled to the ellipsoid of Size
            JPH::Ref<JPH::SphereShapeSettings> unit = new JPH::SphereShapeSettings(0.5f);
            unit->mDensity = density;
            JPH::ScaledShapeSettings s(unit.GetPtr(), to_jolt(size));
            result = s.Create();
        }
        break;
    case kCylinder: // Y axis, as primitives.cpp
        if (near(size.x, size.z)) {
            JPH::CylinderShapeSettings s(h.y, h.x);
            s.mDensity = density;
            result = s.Create();
        } else {
            JPH::Ref<JPH::CylinderShapeSettings> unit = new JPH::CylinderShapeSettings(0.5f, 0.5f);
            unit->mDensity = density;
            JPH::ScaledShapeSettings s(unit.GetPtr(), to_jolt(size));
            result = s.Create();
        }
        break;
    case kWedge: { // slope rises towards +Z, as primitives.cpp make_unit_wedge
        JPH::Array<JPH::Vec3> pts;
        for (f32 x : {-h.x, h.x}) {
            pts.push_back(JPH::Vec3(x, -h.y, -h.z));
            pts.push_back(JPH::Vec3(x, -h.y, h.z));
            pts.push_back(JPH::Vec3(x, h.y, h.z));
        }
        JPH::ConvexHullShapeSettings s(pts);
        s.mDensity = density;
        result = s.Create();
        break;
    }
    default: {
        JPH::BoxShapeSettings s(to_jolt(h));
        s.mDensity = density;
        result = s.Create();
        break;
    }
    }
    if (result.HasError()) {
        TEFRA_LOG_WARN("physics: cannot build the collision shape of %s: %s", part.full_name().c_str(),
                       result.GetError().c_str());
        return nullptr;
    }
    return result.Get();
}

} // namespace

void ContactEvents::OnContactAdded(const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold&,
                                   JPH::ContactSettings&) {
    if (!infos) return;
    const auto ia = infos->find(a.GetUserData());
    const auto ib = infos->find(b.GetUserData());
    if (ia == infos->end() || ib == infos->end()) return;
    if (!ia->second.can_touch || !ib->second.can_touch) return;
    std::lock_guard<std::mutex> lock(mutex);
    pending.emplace_back(a.GetUserData(), b.GetUserData());
}

bool ensure_jolt_initialized() {
    register_jolt_hooks();
    return true;
}

WorldImpl::WorldImpl(world::DataModel& m) : model(m), temp(kTempBytes), jobs(kMaxJobs, kMaxBarriers, kJobThreads) {
    contacts.infos = &infos;
    system.Init(kMaxBodies, 0, kMaxBodyPairs, kMaxContacts, broad_layers, object_vs_broad, pair_filter);
    system.SetContactListener(&contacts);
    system.SetGravity(JPH::Vec3(0.0f, -gravity(), 0.0f));

    world::WorldListener l;
    l.added = [this](world::Instance& inst) { mark_tree(inst); };
    l.reparented = [this](world::Instance& inst, world::Instance*) { mark_tree(inst); };
    l.removing = [this](world::Instance& inst) {
        destroy_body(inst.id());
        dirty.erase(inst.id());
    };
    l.changed = [this](world::Instance& inst, u32 prop) { on_changed(inst, prop); };
    listener = model.add_listener(std::move(l));
    // Parts that already exist (the world may be created after the scene was built) get bodies on the first sync.
    for (world::Instance* d : model.root().descendants()) mark(*d, kDirtyRebuild);
}

WorldImpl::~WorldImpl() {
    model.remove_listener(listener);
    for (CharacterImpl* c : characters) character_detach(*c);
    characters.clear();
}

f32 WorldImpl::gravity() const {
    const world::Instance* ws = model.workspace();
    return ws ? ws->get_float("Gravity", 9.81f) : 9.81f;
}

// A part is simulated when it lives in the Workspace and is not part of a character (CharacterController owns those).
bool WorldImpl::is_simulated(const world::Instance& inst) const {
    if (!inst.is_a("BasePart")) return false;
    const world::Instance* ws = model.workspace();
    for (const world::Instance* p = inst.parent(); p; p = p->parent()) {
        if (p == ws) return true;
        if (p->find_first_child_of_class("Humanoid")) return false;
    }
    return false;
}

void WorldImpl::mark(world::Instance& inst, u32 flags) {
    if (inst.is_a("BasePart")) dirty[inst.id()] |= flags;
}

void WorldImpl::mark_tree(world::Instance& inst) {
    mark(inst, kDirtyRebuild);
    for (world::Instance* d : inst.descendants()) mark(*d, kDirtyRebuild);
}

void WorldImpl::on_changed(world::Instance& inst, u32 prop) {
    if (writing_back || !inst.is_a("BasePart")) return;
    const world::PropInfo* p = inst.cls().prop(prop);
    if (!p) return;
    const std::string& n = p->name;
    if (n == "Position" || n == "Orientation") {
        mark(inst, kDirtyTeleport);
    } else if (n == "AssemblyLinearVelocity") {
        mark(inst, kDirtyVelocity);
    } else if (n == "Anchored" || n == "CanCollide" || n == "CanTouch" || n == "Shape" || n == "Size" ||
               n == "Density" || n == "Friction" || n == "Elasticity") {
        mark(inst, kDirtyRebuild);
    }
}

void WorldImpl::sync() {
    std::unordered_map<u64, u32> pending;
    pending.swap(dirty);
    JPH::BodyInterface& bi = system.GetBodyInterface();
    for (const auto& [id, flags] : pending) {
        world::Instance* inst = model.find(id);
        if (!inst || !is_simulated(*inst)) {
            destroy_body(id);
            continue;
        }
        const auto it = bodies.find(id);
        if (it == bodies.end() || (flags & kDirtyRebuild) != 0) {
            destroy_body(id);
            create_body(*inst);
            continue;
        }
        const JPH::BodyID bid = it->second;
        const bool dynamic = bi.GetMotionType(bid) == JPH::EMotionType::Dynamic;
        if ((flags & kDirtyTeleport) != 0) {
            bi.SetPositionAndRotation(bid, JPH::RVec3(to_jolt(inst->get_vec3("Position"))),
                                      to_jolt(world::orientation_to_quat(inst->get_vec3("Orientation"))),
                                      dynamic ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
        }
        if ((flags & kDirtyVelocity) != 0 && dynamic) {
            bi.SetLinearVelocity(bid, to_jolt(inst->get_vec3("AssemblyLinearVelocity")));
        }
    }
}

void WorldImpl::create_body(world::Instance& part) {
    const vec3 pos = part.get_vec3("Position");
    const vec3 size = part.get_vec3("Size", {4.0f, 1.0f, 2.0f});
    const bool anchored = part.get_bool("Anchored");
    const bool can_collide = part.get_bool("CanCollide", true);
    const bool can_touch = part.get_bool("CanTouch", true);
    const f32 density = max(part.get_float("Density", 700.0f), 1.0f);
    JPH::RefConst<JPH::Shape> shape = make_shape(part, size, density);
    if (!shape) return;

    const JPH::EMotionType motion = anchored ? JPH::EMotionType::Static : JPH::EMotionType::Dynamic;
    JPH::ObjectLayer layer = kLayerDynamic;
    if (anchored)
        layer = can_collide ? kLayerStatic : kLayerStaticSensor;
    else
        layer = can_collide ? kLayerDynamic : kLayerDynamicSensor;

    JPH::BodyCreationSettings bcs(shape, JPH::RVec3(to_jolt(pos)),
                                  to_jolt(world::orientation_to_quat(part.get_vec3("Orientation"))), motion, layer);
    bcs.mFriction = max(part.get_float("Friction", 0.5f), 0.0f);
    bcs.mRestitution = max(part.get_float("Elasticity", 0.3f), 0.0f);
    bcs.mIsSensor = !can_collide;
    bcs.mUserData = part.id();
    if (!anchored) {
        bcs.mLinearVelocity = to_jolt(part.get_vec3("AssemblyLinearVelocity"));
        bcs.mMotionQuality = JPH::EMotionQuality::LinearCast; // no tunnelling through thin parts
    }
    const JPH::EActivation activation = anchored ? JPH::EActivation::DontActivate : JPH::EActivation::Activate;
    const JPH::BodyID id = system.GetBodyInterface().CreateAndAddBody(bcs, activation);
    if (id.IsInvalid()) {
        TEFRA_LOG_WARN("physics: no room for body %s", part.full_name().c_str());
        return;
    }
    bodies[part.id()] = id;
    infos[part.id()] = BodyInfo{!can_collide, can_touch};
}

void WorldImpl::destroy_body(u64 id) {
    const auto it = bodies.find(id);
    if (it != bodies.end()) {
        JPH::BodyInterface& bi = system.GetBodyInterface();
        bi.RemoveBody(it->second);
        bi.DestroyBody(it->second);
        bodies.erase(it);
    }
    infos.erase(id);
}

void WorldImpl::write_back() {
    JPH::BodyInterface& bi = system.GetBodyInterface();
    writing_back = true;
    for (const auto& [id, bid] : bodies) {
        if (bi.GetMotionType(bid) != JPH::EMotionType::Dynamic) continue;
        world::Instance* inst = model.find(id);
        if (!inst) continue;
        inst->set("Position", world::Value::vector3(from_jolt(bi.GetPosition(bid))));
        inst->set("Orientation", world::Value::vector3(world::quat_to_orientation(from_jolt(bi.GetRotation(bid)))));
        inst->set("AssemblyLinearVelocity", world::Value::vector3(from_jolt(bi.GetLinearVelocity(bid))));
    }
    writing_back = false;
}

void WorldImpl::destroy_fallen() {
    const world::Instance* ws = model.workspace();
    const f32 limit = ws ? ws->get_float("FallenPartsDestroyHeight", kDefaultFallLimit) : kDefaultFallLimit;
    JPH::BodyInterface& bi = system.GetBodyInterface();
    std::vector<u64> fallen;
    for (const auto& [id, bid] : bodies)
        if (bi.GetMotionType(bid) == JPH::EMotionType::Dynamic && bi.GetPosition(bid).GetY() < limit)
            fallen.push_back(id);
    for (u64 id : fallen)
        if (world::Instance* inst = model.find(id)) model.destroy(inst);
}

void WorldImpl::dispatch_touched() {
    std::vector<std::pair<u64, u64>> events;
    {
        std::lock_guard<std::mutex> lock(contacts.mutex);
        events.swap(contacts.pending);
    }
    if (events.empty() || touched.empty()) return;
    const std::vector<TouchEntry> listeners = touched;
    for (const auto& [a_id, b_id] : events) {
        world::Instance* a = model.find(a_id);
        world::Instance* b = model.find(b_id);
        if (!a || !b) continue;
        for (const TouchEntry& e : listeners)
            if (e.fn) e.fn(*a, *b);
    }
}

} // namespace detail

using detail::WorldImpl;

WorldImpl& detail::WorldAccess::impl(PhysicsWorld& w) { return *w.impl_; }

PhysicsWorld::PhysicsWorld(world::DataModel& model) : model_(model), impl_(std::make_unique<WorldImpl>(model)) {}

PhysicsWorld::~PhysicsWorld() = default;

void PhysicsWorld::sync() { impl_->sync(); }

void PhysicsWorld::step(f32 dt) {
    WorldImpl& w = *impl_;
    w.sync();
    w.system.SetGravity(JPH::Vec3(0.0f, -w.gravity(), 0.0f));
    w.accumulator += clamp(dt, 0.0f, detail::kMaxFrameSeconds);
    u32 substeps = 0;
    while (w.accumulator >= kPhysicsStep && substeps < detail::kMaxSubsteps) {
        for (detail::CharacterImpl* c : w.characters) detail::character_pre_step(*c, w, kPhysicsStep);
        const JPH::EPhysicsUpdateError err = w.system.Update(kPhysicsStep, 1, &w.temp, &w.jobs);
        if (err != JPH::EPhysicsUpdateError::None && !w.update_error_logged) {
            w.update_error_logged = true;
            TEFRA_LOG_WARN("physics: Jolt update error %u", static_cast<unsigned>(err));
        }
        w.accumulator -= kPhysicsStep;
        ++substeps;
    }
    if (substeps == detail::kMaxSubsteps) w.accumulator = 0.0f; // fell behind: drop the backlog
    w.write_back();
    for (detail::CharacterImpl* c : w.characters) detail::character_post_step(*c, w, dt);
    w.destroy_fallen();
    w.dispatch_touched();
}

bool PhysicsWorld::raycast(vec3 origin, vec3 direction, f32 max_distance, RaycastResult& out) const {
    out = RaycastResult{};
    const f32 len = length(direction);
    if (len <= 0.0f || max_distance <= 0.0f) return false;
    const vec3 dir = direction / len;
    const JPH::RRayCast ray(JPH::RVec3(to_jolt(origin)), to_jolt(dir * max_distance));
    JPH::RayCastResult hit;
    const detail::SolidOnlyFilter filter;
    if (!impl_->system.GetNarrowPhaseQuery().CastRay(ray, hit, {}, {}, filter)) return false;
    out.distance = hit.mFraction * max_distance;
    out.position = origin + dir * out.distance;
    out.instance = impl_->model.find(impl_->system.GetBodyInterfaceNoLock().GetUserData(hit.mBodyID));
    {
        JPH::BodyLockRead lock(impl_->system.GetBodyLockInterface(), hit.mBodyID);
        if (lock.Succeeded())
            out.normal = from_jolt(
                lock.GetBody().GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, JPH::RVec3(to_jolt(out.position))));
    }
    return out.instance != nullptr;
}

usize PhysicsWorld::add_touched_listener(TouchedFn fn) {
    const usize handle = impl_->next_touch_handle++;
    impl_->touched.push_back({handle, std::move(fn)});
    return handle;
}

void PhysicsWorld::remove_touched_listener(usize handle) {
    auto& list = impl_->touched;
    for (auto it = list.begin(); it != list.end(); ++it) {
        if (it->handle == handle) {
            list.erase(it);
            return;
        }
    }
}

bool PhysicsWorld::has_body(const world::Instance& part) const { return impl_->bodies.count(part.id()) != 0; }

u32 PhysicsWorld::body_count() const { return static_cast<u32>(impl_->bodies.size()); }

} // namespace tefra::physics
