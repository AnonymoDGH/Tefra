// PhysicsWorld: BaseParts <-> Jolt bodies (spec M3, ADR-014). Jolt is built without exceptions or RTTI.
#include "internal.h"

#include "tefra/core/log.h"

#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollector.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/ShapeCast.h>

#include <algorithm>
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

// Ignores sensors (they never collide, so they do not block rays) and the parts that the query filter rejects.
class SolidPartFilter final : public JPH::BodyFilter {
  public:
    SolidPartFilter(const world::DataModel& model, const world::QueryFilter* filter) : model_(model), filter_(filter) {}
    bool ShouldCollideLocked(const JPH::Body& body) const override {
        if (body.IsSensor()) return false;
        if (!filter_) return true;
        return filter_->accepts(model_.find(body.GetUserData()));
    }

  private:
    const world::DataModel& model_;
    const world::QueryFilter* filter_;
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

namespace {
// Key of an unordered pair of bodies (both orders map to the same key).
u64 pair_key(JPH::BodyID x, JPH::BodyID y) {
    u64 p = x.GetIndexAndSequenceNumber();
    u64 q = y.GetIndexAndSequenceNumber();
    if (p > q) std::swap(p, q);
    return (p << 32) | q;
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
    pending.push_back(TouchPair{a.GetUserData(), b.GetUserData(), false});
    active[pair_key(a.GetID(), b.GetID())] = {a.GetUserData(), b.GetUserData()};
}

void ContactEvents::OnContactRemoved(const JPH::SubShapeIDPair& pair) {
    std::lock_guard<std::mutex> lock(mutex);
    const auto it = active.find(pair_key(pair.GetBody1ID(), pair.GetBody2ID()));
    if (it == active.end()) return;
    pending.push_back(TouchPair{it->second.first, it->second.second, true});
    active.erase(it);
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

void WorldImpl::clear_heightfield() {
    if (!has_heightfield) return;
    JPH::BodyInterface& bi = system.GetBodyInterface();
    bi.RemoveBody(heightfield_body);
    bi.DestroyBody(heightfield_body);
    has_heightfield = false;
}

WorldImpl::~WorldImpl() {
    clear_heightfield();
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
    } else if (n == "AssemblyLinearVelocity" || n == "AssemblyAngularVelocity") {
        mark(inst, kDirtyVelocity);
    } else if (n == "Anchored" || n == "CanCollide" || n == "CanTouch" || n == "Shape" || n == "Size" ||
               n == "Density" || n == "Friction" || n == "Elasticity") {
        mark(inst, kDirtyRebuild);
    }
}

void WorldImpl::sync(f32 dt) {
    std::unordered_map<u64, u32> pending;
    pending.swap(dirty);
    JPH::BodyInterface& bi = system.GetBodyInterface();
    const f32 move_dt = dt > 1e-4f ? dt : kPhysicsStep; // kinematic velocity = displacement / dt
    std::unordered_set<u64> moved_now;
    for (const auto& [id, flags] : pending) {
        world::Instance* inst = model.find(id);
        if (!inst || !is_simulated(*inst)) {
            destroy_body(id);
            kinematic_ids.erase(id);
            continue;
        }
        const auto it = bodies.find(id);
        if (it == bodies.end() || (flags & kDirtyRebuild) != 0) {
            destroy_body(id);
            create_body(*inst);
            continue;
        }
        const JPH::BodyID bid = it->second;
        const JPH::EMotionType motion = bi.GetMotionType(bid);
        const bool dynamic = motion == JPH::EMotionType::Dynamic;
        const bool kinematic = motion == JPH::EMotionType::Kinematic;
        if ((flags & kDirtyTeleport) != 0) {
            if (motion == JPH::EMotionType::Static && inst->get_bool("Anchored")) {
                // A script moves an anchored part: from now on it is kinematic, so the characters on it are carried.
                kinematic_ids.insert(id);
                destroy_body(id);
                create_body(*inst);
                moved_now.insert(id);
                continue;
            }
            const JPH::RVec3 pos(to_jolt(inst->get_vec3("Position")));
            const JPH::Quat rot = to_jolt(world::orientation_to_quat(inst->get_vec3("Orientation")));
            if (kinematic) {
                bi.MoveKinematic(bid, pos, rot, move_dt);
                moved_now.insert(id);
            } else {
                bi.SetPositionAndRotation(bid, pos, rot,
                                          dynamic ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
            }
        }
        if ((flags & kDirtyVelocity) != 0 && dynamic) {
            bi.SetLinearVelocity(bid, to_jolt(inst->get_vec3("AssemblyLinearVelocity")));
            bi.SetAngularVelocity(bid, to_jolt(inst->get_vec3("AssemblyAngularVelocity")));
        }
    }
    // A kinematic part that stopped moving must not keep its last velocity (characters would slide off it).
    for (u64 id : moving_kinematic) {
        if (moved_now.count(id) != 0) continue;
        const auto it = bodies.find(id);
        if (it != bodies.end()) bi.SetLinearVelocity(it->second, JPH::Vec3::sZero());
    }
    moving_kinematic.swap(moved_now);
    // Impulses from scripts, after the bodies of this frame exist. Only dynamic bodies take them.
    for (const PendingImpulse& imp : impulses) {
        const auto it = bodies.find(imp.id);
        if (it == bodies.end() || bi.GetMotionType(it->second) != JPH::EMotionType::Dynamic) continue;
        if (imp.angular)
            bi.AddAngularImpulse(it->second, to_jolt(imp.value));
        else
            bi.AddImpulse(it->second, to_jolt(imp.value));
    }
    impulses.clear();
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

    const bool kinematic = anchored && kinematic_ids.count(part.id()) != 0;
    const JPH::EMotionType motion =
        !anchored ? JPH::EMotionType::Dynamic : (kinematic ? JPH::EMotionType::Kinematic : JPH::EMotionType::Static);
    // Kinematic bodies move, so they sit on the moving broad-phase layer like dynamic ones.
    JPH::ObjectLayer layer = kLayerDynamic;
    if (anchored && !kinematic)
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
        bcs.mAngularVelocity = to_jolt(part.get_vec3("AssemblyAngularVelocity"));
        bcs.mMotionQuality = JPH::EMotionQuality::LinearCast; // no tunnelling through thin parts
    }
    const JPH::EActivation activation =
        motion == JPH::EMotionType::Static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate;
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
        inst->set("AssemblyAngularVelocity", world::Value::vector3(from_jolt(bi.GetAngularVelocity(bid))));
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

// Character contacts: the characters' capsules overlap every part that has CanTouch (solid or sensor) within a few
// centimetres. Parts that appear in the scan are the touching set; new ones are Touched, missing ones TouchEnded.
void WorldImpl::scan_character_touches() {
    constexpr f32 kTouchDistance = 0.05f; // standing on a floor counts as touching it
    for (CharacterImpl* c : characters) {
        if (!c->cv || !c->world || model.find(c->root_id) == nullptr) continue;
        JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> hits;
        JPH::CollideShapeSettings settings;
        settings.mMaxSeparationDistance = kTouchDistance;
        system.GetNarrowPhaseQuery().CollideShape(c->cv->GetShape(), JPH::Vec3::sReplicate(1.0f),
                                                  c->cv->GetCenterOfMassTransform(), settings, JPH::RVec3::sZero(),
                                                  hits, system.GetDefaultBroadPhaseLayerFilter(kLayerDynamic),
                                                  system.GetDefaultLayerFilter(kLayerDynamic));
        std::vector<u64> now;
        for (const JPH::CollideShapeResult& r : hits.mHits) {
            const u64 part = system.GetBodyInterfaceNoLock().GetUserData(r.mBodyID2);
            const auto info = infos.find(part);
            if (info == infos.end() || !info->second.can_touch || part == c->root_id) continue;
            if (std::find(now.begin(), now.end(), part) == now.end()) now.push_back(part);
        }
        for (u64 id : now)
            if (std::find(c->touching.begin(), c->touching.end(), id) == c->touching.end())
                touch_queue.push_back(TouchPair{c->root_id, id, false});
        for (u64 id : c->touching)
            if (std::find(now.begin(), now.end(), id) == now.end())
                touch_queue.push_back(TouchPair{c->root_id, id, true});
        c->touching = std::move(now);
    }
}

void WorldImpl::dispatch_touched() {
    std::vector<TouchPair> events;
    {
        std::lock_guard<std::mutex> lock(contacts.mutex);
        events.swap(contacts.pending);
    }
    events.insert(events.end(), touch_queue.begin(), touch_queue.end());
    touch_queue.clear();
    if (events.empty() || touched.empty()) return;
    const std::vector<TouchEntry> listeners = touched;
    for (const TouchPair& p : events) {
        world::Instance* a = model.find(p.a);
        world::Instance* b = model.find(p.b);
        if (!a || !b) continue;
        for (const TouchEntry& e : listeners)
            if (e.fn) e.fn(*a, *b, p.ended);
    }
}

} // namespace detail

using detail::WorldImpl;

WorldImpl& detail::WorldAccess::impl(PhysicsWorld& w) { return *w.impl_; }

PhysicsWorld::PhysicsWorld(world::DataModel& model) : model_(model), impl_(std::make_unique<WorldImpl>(model)) {}

PhysicsWorld::~PhysicsWorld() = default;

void PhysicsWorld::sync() { impl_->sync(kPhysicsStep); }

void PhysicsWorld::step(f32 dt) {
    WorldImpl& w = *impl_;
    w.sync(dt);
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
    w.scan_character_touches();
    w.dispatch_touched();
}

bool PhysicsWorld::raycast(vec3 origin, vec3 direction, f32 max_distance, RaycastResult& out,
                           const world::QueryFilter* filter) const {
    out = RaycastResult{};
    const f32 len = length(direction);
    if (len <= 0.0f || max_distance <= 0.0f) return false;
    const vec3 dir = direction / len;
    const JPH::RRayCast ray(JPH::RVec3(to_jolt(origin)), to_jolt(dir * max_distance));
    JPH::RayCastResult hit;
    const detail::SolidPartFilter body_filter(impl_->model, filter);
    if (!impl_->system.GetNarrowPhaseQuery().CastRay(ray, hit, {}, {}, body_filter)) return false;
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

namespace detail {

bool cast_shape(WorldImpl& w, JPH::RefConst<JPH::Shape> shape, vec3 center, quat rot, vec3 direction, f32 max_distance,
                RaycastResult& out, const world::QueryFilter* filter) {
    out = RaycastResult{};
    const f32 len = length(direction);
    if (!shape || len <= 0.0f || max_distance <= 0.0f) return false;
    const vec3 dir = direction / len;
    const JPH::RMat44 start = JPH::RMat44::sRotationTranslation(to_jolt(rot), JPH::RVec3(to_jolt(center)));
    const JPH::RShapeCast cast(shape.GetPtr(), JPH::Vec3::sReplicate(1.0f), start, to_jolt(dir * max_distance));
    const JPH::ShapeCastSettings settings;
    JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
    const SolidPartFilter body_filter(w.model, filter);
    w.system.GetNarrowPhaseQuery().CastShape(cast, settings, JPH::RVec3::sZero(), collector, {}, {}, body_filter);
    if (!collector.HadHit()) return false;
    const JPH::ShapeCastResult& hit = collector.mHit;
    out.distance = hit.mFraction * max_distance;
    out.position = from_jolt(JPH::Vec3(hit.mContactPointOn2));
    out.instance = w.model.find(w.system.GetBodyInterfaceNoLock().GetUserData(hit.mBodyID2));
    {
        JPH::BodyLockRead lock(w.system.GetBodyLockInterface(), hit.mBodyID2);
        if (lock.Succeeded())
            out.normal = from_jolt(lock.GetBody().GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, hit.mContactPointOn2));
    }
    return out.instance != nullptr;
}

} // namespace detail

bool PhysicsWorld::spherecast(vec3 origin, f32 radius, vec3 direction, f32 max_distance, RaycastResult& out,
                              const world::QueryFilter* filter) const {
    if (!(radius > 0.0f)) {
        out = RaycastResult{};
        return false;
    }
    JPH::RefConst<JPH::Shape> shape;
    {
        JPH::SphereShapeSettings s(max(radius, 0.01f));
        shape = s.Create().Get();
    }
    return detail::cast_shape(*impl_, shape, origin, quat{}, direction, max_distance, out, filter);
}

bool PhysicsWorld::blockcast(vec3 center, vec3 orientation_deg, vec3 size, vec3 direction, f32 max_distance,
                             RaycastResult& out, const world::QueryFilter* filter) const {
    const vec3 half{max(std::fabs(size.x) * 0.5f, 0.01f), max(std::fabs(size.y) * 0.5f, 0.01f),
                    max(std::fabs(size.z) * 0.5f, 0.01f)};
    JPH::BoxShapeSettings s(to_jolt(half));
    // Jolt wants the convex radius below the smallest half extent.
    s.mConvexRadius = min(0.05f, 0.9f * min(half.x, min(half.y, half.z)));
    JPH::RefConst<JPH::Shape> shape = s.Create().Get();
    return detail::cast_shape(*impl_, shape, center, world::orientation_to_quat(orientation_deg), direction,
                              max_distance, out, filter);
}

void PhysicsWorld::touching_parts(const world::Instance& part, std::vector<world::Instance*>& out) const {
    out.clear();
    detail::WorldImpl& w = *impl_;
    const u64 id = part.id();
    auto add = [&](u64 other) {
        if (other == id || other == 0) return;
        world::Instance* o = w.model.find(other);
        if (o && std::find(out.begin(), out.end(), o) == out.end()) out.push_back(o);
    };
    // Bodies: a shape query at the part's current transform, so a part at rest still counts as touching. (The
    // contact listener is not used: Jolt drops the contacts of bodies that fall asleep.)
    const auto body = w.bodies.find(id);
    if (body != w.bodies.end()) {
        constexpr f32 kTouchDistance = 0.05f;
        const JPH::BodyID bid = body->second;
        JPH::BodyInterface& bi = w.system.GetBodyInterface();
        const JPH::ObjectLayer layer = bi.GetObjectLayer(bid);
        JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> hits;
        JPH::CollideShapeSettings settings;
        settings.mMaxSeparationDistance = kTouchDistance;
        w.system.GetNarrowPhaseQuery().CollideShape(bi.GetShape(bid), JPH::Vec3::sReplicate(1.0f),
                                                    bi.GetCenterOfMassTransform(bid), settings, JPH::RVec3::sZero(),
                                                    hits, w.system.GetDefaultBroadPhaseLayerFilter(layer),
                                                    w.system.GetDefaultLayerFilter(layer));
        for (const JPH::CollideShapeResult& r : hits.mHits) {
            if (r.mBodyID2 == bid) continue;
            const u64 other = w.system.GetBodyInterfaceNoLock().GetUserData(r.mBodyID2);
            const auto info = w.infos.find(other);
            if (info != w.infos.end() && info->second.can_touch) add(other);
        }
    }
    // Characters: their root part is not a body, the scan keeps the parts each one touches.
    for (const detail::CharacterImpl* c : w.characters) {
        if (c->root_id == id) {
            for (u64 t : c->touching) add(t);
        } else if (std::find(c->touching.begin(), c->touching.end(), id) != c->touching.end()) {
            add(c->root_id);
        }
    }
}

bool PhysicsWorld::apply_impulse(const world::Instance& part, vec3 impulse, bool angular) {
    if (!part.is_a("BasePart") || part.get_bool("Anchored") || !impl_->is_simulated(part)) return false;
    impl_->impulses.push_back(detail::WorldImpl::PendingImpulse{part.id(), impulse, angular});
    return true;
}

void PhysicsWorld::set_heightfield(const f32* heights, u32 n, f32 cell, vec2 origin_xz, const world::Instance* owner) {
    detail::WorldImpl& w = *impl_;
    w.clear_heightfield();
    if (!heights || n < 2 || (n & 1u) != 0 || !(cell > 0.0f)) {
        TEFRA_LOG_WARN("physics: invalid heightfield (%u samples per side, cell %.3f m)", n, static_cast<f64>(cell));
        return;
    }
    JPH::HeightFieldShapeSettings hs(heights, JPH::Vec3(origin_xz.x, 0.0f, origin_xz.y), JPH::Vec3(cell, 1.0f, cell),
                                     n);
    JPH::Shape::ShapeResult res = hs.Create();
    if (res.HasError()) {
        TEFRA_LOG_WARN("physics: Jolt rejected the %u x %u heightfield", n, n);
        return;
    }
    JPH::BodyCreationSettings bcs(res.Get(), JPH::RVec3(0.0, 0.0, 0.0), JPH::Quat::sIdentity(),
                                  JPH::EMotionType::Static, detail::kLayerStatic);
    bcs.mFriction = 0.8f; // grass, sand and rock: walkable without sliding
    bcs.mUserData = owner ? owner->id() : 0;
    const JPH::BodyID id = w.system.GetBodyInterface().CreateAndAddBody(bcs, JPH::EActivation::DontActivate);
    if (id.IsInvalid()) {
        TEFRA_LOG_WARN("physics: no room for the heightfield body");
        return;
    }
    w.heightfield_body = id;
    w.has_heightfield = true;
}

void PhysicsWorld::clear_heightfield() { impl_->clear_heightfield(); }

bool PhysicsWorld::has_heightfield() const { return impl_->has_heightfield; }

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
