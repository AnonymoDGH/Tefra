#include "tefra/anim/animator.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace tefra::anim {

namespace {

constexpr f32 kAutoFade = 0.2f;   // cross-fade time of the automatic clips (s)
constexpr f32 kWalkStride = 1.3f; // metres covered per gait cycle when walking
constexpr f32 kRunStride = 3.0f;  // metres covered per gait cycle when running

// Humanoid.State values (classes.cpp, HumanoidState enum).
constexpr u32 kStateJumping = 3;
constexpr u32 kStateFreefall = 4;
constexpr u32 kStateClimbing = 8;

struct Track {
    ClipPtr clip;
    std::string key; // clip name
    bool automatic = false;
    bool phase_driven = false; // walk and run read their time from the shared gait phase
    f32 time = 0.0f;
    f32 speed = 1.0f;
    f32 weight = 0.0f; // current strength (0-1)
    f32 target = 0.0f; // strength the weight moves towards
    f32 rate = 1.0f;   // weight change per second (1 / fade time)
};

struct Motor {
    world::Instance* motor = nullptr;
    world::Instance* part0 = nullptr;
    world::Instance* part1 = nullptr;
    std::string name;
};

f32 smooth_step(f32 e0, f32 e1, f32 x) {
    const f32 t = saturate((x - e0) / (e1 - e0));
    return t * t * (3.0f - 2.0f * t);
}

f32 move_toward(f32 v, f32 target, f32 step) {
    if (v < target) return std::min(v + step, target);
    return std::max(v - step, target);
}

Track* find_track(std::vector<Track>& tracks, std::string_view key, bool automatic) {
    for (Track& t : tracks)
        if (t.automatic == automatic && t.key == key) return &t;
    return nullptr;
}

// Motors of the model whose parents are reachable from the root, ordered parent first.
std::vector<Motor> rig_joints(const world::DataModel& dm, world::Instance& model, world::Instance* root) {
    std::vector<Motor> all;
    for (world::Instance* d : model.descendants()) {
        if (!d->is_a("Motor6D")) continue;
        const world::Value* r0 = d->get("Part0");
        const world::Value* r1 = d->get("Part1");
        if (!r0 || !r1) continue;
        world::Instance* p0 = dm.find(r0->ref);
        world::Instance* p1 = dm.find(r1->ref);
        if (!p0 || !p1) continue;
        all.push_back({d, p0, p1, d->name()});
    }
    std::vector<Motor> ordered;
    std::vector<u64> resolved{root->id()};
    std::vector<bool> used(all.size(), false);
    bool progress = true;
    while (progress) {
        progress = false;
        for (usize i = 0; i < all.size(); ++i) {
            if (used[i]) continue;
            if (std::find(resolved.begin(), resolved.end(), all[i].part0->id()) == resolved.end()) continue;
            used[i] = true;
            progress = true;
            ordered.push_back(all[i]);
            resolved.push_back(all[i].part1->id());
        }
    }
    return ordered;
}

// Transform of a joint from its Motor6D properties: C0 or C1 as position and orientation (degrees).
CFrame joint_frame(const world::Instance& m, const char* pos_prop, const char* rot_prop) {
    return {m.get_vec3(pos_prop), world::orientation_to_quat(m.get_vec3(rot_prop))};
}

} // namespace

struct AnimatorSystem::State {
    std::vector<Track> tracks;
    f32 phase = 0.0f; // gait cycle in [0, 1)
    bool ready = false;
};

AnimatorSystem::AnimatorSystem(world::DataModel& model) : model_(model) {}
AnimatorSystem::~AnimatorSystem() = default;

AnimatorSystem::State& AnimatorSystem::state_for(const world::Instance& humanoid) {
    std::unique_ptr<State>& slot = states_[humanoid.id()];
    if (!slot) slot = std::make_unique<State>();
    State& st = *slot;
    if (!st.ready) {
        st.ready = true;
        const char* names[] = {"idle", "walk", "run", "jump", "fall", "climb"};
        for (const char* n : names) {
            Track t;
            t.clip = builtin_clip(n);
            t.key = n;
            t.automatic = true;
            t.phase_driven = std::string_view(n) == "walk" || std::string_view(n) == "run";
            t.rate = 1.0f / kAutoFade;
            st.tracks.push_back(std::move(t));
        }
    }
    return st;
}

void AnimatorSystem::update(f32 dt) {
    // Forget humanoids that left the DataModel.
    for (auto it = states_.begin(); it != states_.end();) {
        if (model_.find(it->first))
            ++it;
        else
            it = states_.erase(it);
    }
    for (auto it = skinned_.begin(); it != skinned_.end();) {
        if (model_.find(it->first))
            ++it;
        else
            it = skinned_.erase(it);
    }
    const std::vector<world::Instance*> all = model_.root().descendants();
    for (world::Instance* hum : all) {
        if (!hum->is_a("Humanoid") || !hum->find_first_child_of_class("Animator")) continue;
        world::Instance* model = hum->parent();
        if (!model) continue;
        if (world::Instance* part = skinned_part(*model)) {
            advance_skinned(*hum, *model, *part, skinned_state_for(*hum), dt);
            continue;
        }
        advance(*hum, *model, state_for(*hum), dt);
    }
}

void AnimatorSystem::advance(const world::Instance& humanoid, world::Instance& model, State& st, f32 dt) {
    world::Instance* root = model.find_first_child("HumanoidRootPart");
    const world::Value* primary = model.get("PrimaryPart");
    if (!root && primary && primary->ref) root = model_.find(primary->ref);
    if (!root || !root->is_a("BasePart")) return;

    // ---- Locomotion: weights from horizontal speed, gait phase from the distance covered --------------------
    const vec3 vel = root->get_vec3("AssemblyLinearVelocity");
    const f32 speed = std::sqrt(vel.x * vel.x + vel.z * vel.z);
    const f32 w_run = smooth_step(3.0f, 5.0f, speed);
    const f32 w_walk = smooth_step(0.1f, 0.6f, speed) * (1.0f - w_run);
    const f32 w_idle = 1.0f - w_walk - w_run;
    if (speed > 0.05f) {
        const f32 stride = lerp(kWalkStride, kRunStride, w_run);
        st.phase = std::fmod(st.phase + dt * speed / stride, 1.0f);
    }

    // ---- Airborne and climbing: from Humanoid.State ---------------------------------------------------------
    const world::Value* state_value = humanoid.get("State");
    const u32 state = state_value ? state_value->as_enum() : 0u;

    // Automatic targets; a manual clip on the same priority masks them (it takes over those joints).
    f32 mask[3] = {0.0f, 0.0f, 0.0f};
    for (const Track& t : st.tracks)
        if (!t.automatic && t.clip && t.clip->priority >= 0 && t.clip->priority < 3)
            mask[t.clip->priority] = std::max(mask[t.clip->priority], t.target);

    for (Track& t : st.tracks) {
        if (!t.automatic) continue; // manual targets are set by play() and stop()
        f32 target = 0.0f;
        if (t.key == "idle")
            target = w_idle;
        else if (t.key == "walk")
            target = w_walk;
        else if (t.key == "run")
            target = w_run;
        else if (t.key == "jump")
            target = state == kStateJumping ? 1.0f : 0.0f;
        else if (t.key == "fall")
            target = state == kStateFreefall ? 1.0f : 0.0f;
        else if (t.key == "climb")
            target = state == kStateClimbing ? 1.0f : 0.0f;
        const i32 prio = t.clip ? t.clip->priority : 0;
        if (prio >= 0 && prio < 3) target *= 1.0f - mask[prio];
        t.target = saturate(target);
    }

    // ---- Playback: fades and clip times -----------------------------------------------------------------------
    for (Track& t : st.tracks) {
        if (!t.clip) continue;
        // Non-looped clips (jump, wave) start again from their first key when they are triggered.
        if (t.target > 0.0f && t.weight <= 0.0f && !t.clip->looped) t.time = 0.0f;
        t.weight = move_toward(t.weight, t.target, t.rate * dt);
        if (t.phase_driven)
            t.time = st.phase * t.clip->duration;
        else
            t.time += dt * t.speed;
    }
    st.tracks.erase(std::remove_if(st.tracks.begin(), st.tracks.end(),
                                   [](const Track& t) { return !t.automatic && t.weight <= 0.0f && t.target <= 0.0f; }),
                    st.tracks.end());

    // ---- Pose: layers by priority, joints blended by weight ----------------------------------------------------
    const std::vector<Motor> joints = rig_joints(model_, model, root);
    std::vector<CFrame> pose(joints.size());
    std::vector<i32> layers;
    for (const Track& t : st.tracks)
        if (t.weight > 0.0f && t.clip && std::find(layers.begin(), layers.end(), t.clip->priority) == layers.end())
            layers.push_back(t.clip->priority);
    std::sort(layers.begin(), layers.end());
    for (i32 layer : layers) {
        for (usize j = 0; j < joints.size(); ++j) {
            bool any = false;
            f32 total = 0.0f;
            CFrame acc;
            for (const Track& t : st.tracks) {
                if (t.weight <= 0.0f || !t.clip || t.clip->priority != layer) continue;
                CFrame s;
                if (!t.clip->sample_joint(joints[j].name, t.time, s)) continue;
                if (!any) {
                    acc = s;
                    total = t.weight;
                    any = true;
                } else {
                    // Weighted blend of the clips of this layer that animate the joint.
                    const f32 next = total + t.weight;
                    const f32 k = t.weight / next;
                    acc.q = slerp(acc.q, s.q, k);
                    acc.p = lerp(acc.p, s.p, k);
                    total = next;
                }
            }
            if (!any) continue;
            // A layer that is still fading in or out covers the joint only in proportion to its weight.
            const f32 cover = std::min(1.0f, total);
            pose[j].q = slerp(pose[j].q, acc.q, cover);
            pose[j].p = lerp(pose[j].p, acc.p, cover);
        }
    }

    // ---- Write the rig: Part1 = Part0 * C0 * Transform(anim) * inverse(C1) ------------------------------------
    std::vector<std::pair<u64, CFrame>> placed;
    auto frame_of = [&](u64 id, CFrame& out) {
        for (const auto& e : placed)
            if (e.first == id) {
                out = e.second;
                return true;
            }
        return false;
    };
    const CFrame root_frame{root->get_vec3("Position"), world::orientation_to_quat(root->get_vec3("Orientation"))};
    placed.emplace_back(root->id(), root_frame);
    for (usize j = 0; j < joints.size(); ++j) {
        CFrame parent;
        if (!frame_of(joints[j].part0->id(), parent)) continue;
        const CFrame c0 = joint_frame(*joints[j].motor, "C0", "C0Orientation");
        const CFrame c1 = joint_frame(*joints[j].motor, "C1", "C1Orientation");
        const CFrame child = parent * c0 * pose[j] * inverse(c1);
        placed.emplace_back(joints[j].part1->id(), child);
        joints[j].part1->set("Position", world::Value::vector3(child.p));
        joints[j].part1->set("Orientation", world::Value::vector3(world::quat_to_orientation(child.q)));
    }
}

bool AnimatorSystem::play(world::Instance& humanoid, ClipPtr clip, f32 fade, f32 weight, f32 speed) {
    if (!clip || !humanoid.is_a("Humanoid") || !humanoid.parent()) return false;
    State& st = state_for(humanoid);
    const std::string key = clip->name;
    const i32 priority = clip->priority;
    const f32 rate = fade > 1e-4f ? 1.0f / fade : 1e6f;

    Track* t = find_track(st.tracks, key, false);
    if (!t) {
        st.tracks.push_back(Track{});
        t = &st.tracks.back();
        t->key = key;
    }
    if (t->target <= 0.0f) t->time = 0.0f;
    t->clip = std::move(clip);
    t->speed = speed;
    t->target = saturate(weight);
    t->rate = rate;

    // Cross-fade: the previous manual clip on the same priority fades out.
    for (Track& o : st.tracks) {
        if (o.automatic || &o == t || o.key == key || !o.clip || o.clip->priority != priority) continue;
        o.target = 0.0f;
        o.rate = rate;
    }
    return true;
}

bool AnimatorSystem::play(world::Instance& humanoid, std::string_view builtin, f32 fade, f32 weight, f32 speed) {
    return play(humanoid, builtin_clip(builtin), fade, weight, speed);
}

void AnimatorSystem::stop(world::Instance& humanoid, std::string_view clip, f32 fade) {
    State& st = state_for(humanoid);
    const f32 rate = fade > 1e-4f ? 1.0f / fade : 1e6f;
    for (Track& t : st.tracks) {
        if (t.automatic || (!clip.empty() && t.key != clip)) continue;
        t.target = 0.0f;
        t.rate = rate;
    }
}

// ---- Skinned characters (imported glTF rigs, spec M3b) -------------------------------------------------------

namespace {

struct SkinnedTrack {
    SkeletalClipPtr clip;
    i32 clip_index = -1;
    std::string key;
    bool automatic = false;
    bool phase_driven = false; // walk and run read their time from the shared gait phase
    bool looped = true;
    i32 priority = 0;
    f32 time = 0.0f;
    f32 speed = 1.0f;
    f32 weight = 0.0f;
    f32 target = 0.0f;
    f32 rate = 1.0f;
};

std::string lower_name(std::string_view s) {
    std::string r(s);
    for (char& c : r)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return r;
}

// Clip for a locomotion name, chosen from the pool: exact name first, then substring; "#n" selects by index.
i32 pick_clip(const SkeletonAsset& asset, const std::vector<i32>& pool, const std::string& name) {
    if (name.empty()) return -1;
    if (name[0] == '#') {
        const i32 i = find_clip(asset, name);
        return std::find(pool.begin(), pool.end(), i) != pool.end() ? i : -1;
    }
    const std::string want = lower_name(name);
    for (i32 i : pool)
        if (lower_name(asset.clips[static_cast<usize>(i)]->name) == want) return i;
    for (i32 i : pool)
        if (lower_name(asset.clips[static_cast<usize>(i)]->name).find(want) != std::string::npos) return i;
    return -1;
}

} // namespace

struct AnimatorSystem::SkinnedState {
    const SkeletonAsset* asset = nullptr; // owned by assets_ (never erased)
    std::vector<SkinnedTrack> tracks;
    f32 phase = 0.0f;
    bool ready = false;
};

world::Instance* AnimatorSystem::skinned_part(world::Instance& model) {
    for (world::Instance* d : model.descendants()) {
        if (!d->is_a("MeshPart")) continue;
        const std::string& path = d->get_string("MeshId");
        if (path.empty()) continue;
        const SkeletonAsset* a = asset_for(path);
        if (a && a->ok) return d;
    }
    return nullptr;
}

const SkeletonAsset* AnimatorSystem::asset_for(const std::string& path) {
    auto it = assets_.find(path);
    if (it == assets_.end()) {
        auto asset = std::make_shared<SkeletonAsset>();
        load_skeleton_asset(path.c_str(), *asset); // ok stays false for files without a skin
        it = assets_.emplace(path, std::move(asset)).first;
    }
    return it->second.get();
}

AnimatorSystem::SkinnedState& AnimatorSystem::skinned_state_for(const world::Instance& humanoid) {
    std::unique_ptr<SkinnedState>& slot = skinned_[humanoid.id()];
    if (!slot) slot = std::make_unique<SkinnedState>();
    return *slot;
}

void AnimatorSystem::advance_skinned(const world::Instance& humanoid, world::Instance& model, world::Instance& part,
                                     SkinnedState& st, f32 dt) {
    const SkeletonAsset* asset = asset_for(part.get_string("MeshId"));
    if (!asset || !asset->ok) return;
    world::Instance* root = model.find_first_child("HumanoidRootPart");
    const world::Value* primary = model.get("PrimaryPart");
    if (!root && primary && primary->ref) root = model_.find(primary->ref);
    if (!root || !root->is_a("BasePart")) return;

    const world::Instance* animator = humanoid.find_first_child_of_class("Animator");
    if (!st.ready) {
        // Clip pool: the clips of the Animation children (AnimationId = 'file.glb#Clip' or '#index'), or every clip.
        st.ready = true;
        st.asset = asset;
        std::vector<i32> pool;
        if (animator)
            for (world::Instance* c : animator->children()) {
                if (!c->is_a("Animation")) continue;
                const std::string& id = c->get_string("AnimationId");
                const usize hash = id.find('#');
                if (hash == std::string::npos) continue;
                const i32 i = find_clip(*asset, std::string_view(id).substr(hash));
                if (i >= 0 && std::find(pool.begin(), pool.end(), i) == pool.end()) pool.push_back(i);
            }
        if (pool.empty())
            for (usize i = 0; i < asset->clips.size(); ++i) pool.push_back(static_cast<i32>(i));

        // Clip names come from the Animator's properties (IdleClip, WalkClip, ...); the defaults are the locomotion
        // names.
        auto name_of = [&](const char* prop, const char* fallback) -> std::string {
            if (animator && animator->get(prop)) return animator->get_string(prop);
            return std::string(fallback);
        };
        // Idle falls back to the first pool clip, walk to idle and run to walk, so every character moves.
        i32 idle = pick_clip(*asset, pool, name_of("IdleClip", "idle"));
        if (idle < 0 && !pool.empty()) idle = pool[0];
        i32 walk = pick_clip(*asset, pool, name_of("WalkClip", "walk"));
        if (walk < 0) walk = idle;
        i32 run = pick_clip(*asset, pool, name_of("RunClip", "run"));
        if (run < 0) run = walk;
        const i32 jump = pick_clip(*asset, pool, name_of("JumpClip", "jump"));
        const i32 fall = pick_clip(*asset, pool, name_of("FallClip", "fall"));
        const f32 rate = 1.0f / kAutoFade;
        auto add = [&](const char* key, i32 index, bool phase, bool looped, i32 prio) {
            if (index < 0) return;
            SkinnedTrack t;
            t.clip = asset->clips[static_cast<usize>(index)];
            t.clip_index = index;
            t.key = key;
            t.automatic = true;
            t.phase_driven = phase;
            t.looped = looped;
            t.priority = prio;
            t.rate = rate;
            st.tracks.push_back(std::move(t));
        };
        add("idle", idle, false, true, 0);
        add("walk", walk, true, true, 0);
        add("run", run, true, true, 0);
        add("jump", jump, false, false, 1); // jumps hold their last pose
        add("fall", fall, false, true, 1);
    }

    // ---- Locomotion (same rules as the R15 rig) ---------------------------------------------------------------
    const vec3 vel = root->get_vec3("AssemblyLinearVelocity");
    const f32 speed = std::sqrt(vel.x * vel.x + vel.z * vel.z);
    const f32 w_run = smooth_step(3.0f, 5.0f, speed);
    const f32 w_walk = smooth_step(0.1f, 0.6f, speed) * (1.0f - w_run);
    const f32 w_idle = 1.0f - w_walk - w_run;
    if (speed > 0.05f) {
        const f32 stride = lerp(kWalkStride, kRunStride, w_run);
        st.phase = std::fmod(st.phase + dt * speed / stride, 1.0f);
    }
    const world::Value* state_value = humanoid.get("State");
    const u32 state = state_value ? state_value->as_enum() : 0u;

    for (SkinnedTrack& t : st.tracks) {
        if (!t.automatic) continue;
        f32 target = 0.0f;
        if (t.key == "idle")
            target = w_idle;
        else if (t.key == "walk")
            target = w_walk;
        else if (t.key == "run")
            target = w_run;
        else if (t.key == "jump")
            target = state == kStateJumping ? 1.0f : 0.0f;
        else if (t.key == "fall")
            target = state == kStateFreefall ? 1.0f : 0.0f;
        t.target = saturate(target);
    }
    for (SkinnedTrack& t : st.tracks) {
        if (t.target > 0.0f && t.weight <= 0.0f && !t.looped) t.time = 0.0f;
        t.weight = move_toward(t.weight, t.target, t.rate * dt);
        if (t.phase_driven)
            t.time = st.phase * t.clip->duration;
        else
            t.time += dt * t.speed;
    }
    st.tracks.erase(
        std::remove_if(st.tracks.begin(), st.tracks.end(),
                       [](const SkinnedTrack& t) { return !t.automatic && t.weight <= 0.0f && t.target <= 0.0f; }),
        st.tracks.end());

    // ---- Pose: layers by priority, joints blended by weight (as the R15 path) --------------------------------
    const Skeleton& sk = asset->skeleton;
    std::vector<Trs> pose = sk.rest_pose();
    std::vector<i32> layers;
    for (const SkinnedTrack& t : st.tracks)
        if (t.weight > 0.0f && std::find(layers.begin(), layers.end(), t.priority) == layers.end())
            layers.push_back(t.priority);
    std::sort(layers.begin(), layers.end());
    for (i32 layer : layers) {
        for (usize j = 0; j < sk.joints.size(); ++j) {
            bool any = false;
            f32 total = 0.0f;
            Trs acc;
            for (const SkinnedTrack& t : st.tracks) {
                if (t.weight <= 0.0f || t.priority != layer || !t.clip) continue;
                Trs s = pose[j]; // components the clip does not animate keep the layers below
                if (!t.clip->sample_joint(static_cast<u32>(j), t.time, t.looped, s)) continue;
                if (!any) {
                    acc = s;
                    total = t.weight;
                    any = true;
                } else {
                    const f32 next = total + t.weight;
                    acc = blend(acc, s, t.weight / next);
                    total = next;
                }
            }
            if (!any) continue;
            pose[j] = blend(pose[j], acc, min(1.0f, total));
        }
    }

    std::vector<mat4> palette;
    sk.palette(pose, palette);
    model_.set_skin_pose(part.id(), std::move(palette));

    // The mesh follows the root: its feet sit HipHeight + Size.y / 2 below the root centre (the controller's capsule
    // convention), and glTF models face +Z (Tefra: -Z).
    const f32 drop = humanoid.get_float("HipHeight", 0.9f) + root->get_vec3("Size", {2.0f, 2.0f, 1.0f}).y * 0.5f;
    const quat root_rot = world::orientation_to_quat(root->get_vec3("Orientation"));
    const quat face = normalize(root_rot * quat{0.0f, 1.0f, 0.0f, 0.0f});
    part.set("Position", world::Value::vector3(root->get_vec3("Position") + rotate(root_rot, vec3{0.0f, -drop, 0.0f})));
    part.set("Orientation", world::Value::vector3(world::quat_to_orientation(face)));
}

bool AnimatorSystem::play_clip(world::Instance& humanoid, std::string_view clip, f32 fade, f32 weight, bool looped) {
    if (!humanoid.is_a("Humanoid") || !humanoid.parent()) return false;
    world::Instance* part = skinned_part(*humanoid.parent());
    if (!part) return false;
    const SkeletonAsset* asset = asset_for(part->get_string("MeshId"));
    const i32 index = find_clip(*asset, clip);
    if (index < 0) return false;
    SkinnedState& st = skinned_state_for(humanoid);
    if (!st.asset) st.asset = asset;
    const f32 rate = fade > 1e-4f ? 1.0f / fade : 1e6f;
    const SkeletalClipPtr& ptr = asset->clips[static_cast<usize>(index)];

    SkinnedTrack* t = nullptr;
    for (SkinnedTrack& o : st.tracks)
        if (!o.automatic && o.clip_index == index) t = &o;
    if (!t) {
        st.tracks.push_back(SkinnedTrack{});
        t = &st.tracks.back();
    }
    if (t->target <= 0.0f) t->time = 0.0f;
    t->clip = ptr;
    t->clip_index = index;
    t->key = ptr->name;
    t->automatic = false;
    t->looped = looped;
    t->priority = 2; // above locomotion (0) and airborne (1)
    t->target = saturate(weight);
    t->rate = rate;
    for (SkinnedTrack& o : st.tracks) {
        if (o.automatic || &o == t || o.priority != 2) continue;
        o.target = 0.0f;
        o.rate = rate;
    }
    return true;
}

bool AnimatorSystem::is_playing(const world::Instance& humanoid, std::string_view clip) const {
    if (const auto sk = skinned_.find(humanoid.id()); sk != skinned_.end() && sk->second) {
        for (const SkinnedTrack& t : sk->second->tracks)
            if (t.key == clip && t.target > 0.0f) return true;
    }
    const auto it = states_.find(humanoid.id());
    if (it == states_.end() || !it->second) return false;
    for (const Track& t : it->second->tracks)
        if (t.key == clip && t.target > 0.0f) return true;
    return false;
}

} // namespace tefra::anim
