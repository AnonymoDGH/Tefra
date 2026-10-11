#include "tefra/runtime/game_runtime.h"

#include "tefra/anim/animator.h"
#include "tefra/anim/avatar.h"
#include "tefra/core/fs.h"
#include "tefra/core/log.h"
#include "tefra/physics/character.h"
#include "tefra/physics/physics_world.h"
#include "tefra/renderer/terrain_field.h"
#include "tefra/scripting/script_host.h"
#include "tefra/world/datamodel.h"
#include "tefra/world/environment_sync.h"

#include <cmath>

namespace tefra::runtime {

using world::Instance;
using world::Value;

namespace {
constexpr u32 kStateDead = 6; // HumanoidState enum (classes.cpp)

Instance* find_spawn(world::DataModel& dm) {
    for (Instance* i : dm.workspace()->descendants())
        if (i->is_a("SpawnLocation") && i->get_bool("Enabled", true)) return i;
    return nullptr;
}

// Copies every child of `from` into `to` (StarterGui -> PlayerGui, StarterPlayerScripts -> PlayerScripts).
void clone_children(world::DataModel& dm, const Instance* from, Instance* to, bool reset_only) {
    if (!from || !to) return;
    for (const Instance* c : from->children()) {
        if (reset_only && !(c->is_a("ScreenGui") && c->get_bool("ResetOnSpawn", true))) continue;
        dm.clone(*c, to);
    }
}
} // namespace

GameRuntime::GameRuntime(world::DataModel& model, RuntimeConfig cfg) : model_(model), cfg_(std::move(cfg)) {}

GameRuntime::~GameRuntime() { stop(); }

void GameRuntime::add_system(std::unique_ptr<RuntimeSystem> system) {
    if (system) systems_.push_back(std::move(system));
}

RuntimeSystem* GameRuntime::system(std::string_view name) const {
    for (const auto& s : systems_)
        if (name == s->name()) return s.get();
    return nullptr;
}

Instance* GameRuntime::local_player() const { return player_id_ ? model_.find(player_id_) : nullptr; }

Instance* GameRuntime::player_gui() const {
    Instance* p = local_player();
    return p ? p->find_first_child_of_class("PlayerGui") : nullptr;
}

Instance* GameRuntime::character() const { return character_id_ ? model_.find(character_id_) : nullptr; }

Instance* GameRuntime::humanoid() const {
    Instance* c = character();
    return c ? c->find_first_child_of_class("Humanoid") : nullptr;
}

std::string GameRuntime::resolve_asset(std::string_view path) const {
    if (path.empty() || cfg_.project_dir.empty()) return std::string(path);
    if (path[0] == '/' || path[0] == '\\' || (path.size() > 1 && path[1] == ':')) return std::string(path);
    return path_join(cfg_.project_dir, std::string(path));
}

bool GameRuntime::start() {
    if (running_) return true;
    running_ = true;
    time_ = 0.0;
    frame_ = 0;
    died_at_ = -1.0;
    physics_ = std::make_unique<physics::PhysicsWorld>(model_);
    // Physics contacts become Touched / TouchEnded events on both parts (scripts, kill bricks, checkpoints).
    touched_listener_ = physics_->add_touched_listener([this](Instance& a, Instance& b, bool ended) {
        const char* name = ended ? "TouchEnded" : "Touched";
        model_.fire_event(a, name, &b);
        model_.fire_event(b, name, &a);
    });
    // Landscape: the Workspace Terrain becomes a static heightfield with the same samples as the GPU terrain.
    if (const Instance* terrain = world::find_terrain(model_)) {
        render::TerrainDesc td;
        if (world::terrain_desc(*terrain, td)) {
            const auto field = render::TerrainField::acquire(td);
            physics_->set_heightfield(field->heights().data(), field->resolution(), field->texel_size(),
                                      field->sample_origin(), terrain);
            TEFRA_LOG_INFO("runtime: landscape heightfield %ux%u at %.1f m", field->resolution(), field->resolution(),
                           static_cast<f64>(field->texel_size()));
        }
    }
    animator_ = std::make_unique<anim::AnimatorSystem>(model_);

    // The local player and its containers.
    if (Instance* players = model_.service("Players")) {
        Instance* player = model_.create_internal("Player", players, cfg_.player_name);
        if (player) {
            player->set("DisplayName", Value::string(cfg_.player_name));
            player_id_ = player->id();
            Instance* gui = model_.create_internal("PlayerGui", player, "PlayerGui");
            Instance* scripts = model_.create_internal("PlayerScripts", player, "PlayerScripts");
            clone_children(model_, model_.service("StarterGui"), gui, false);
            if (const Instance* sp = model_.service("StarterPlayer"))
                clone_children(model_, sp->find_first_child_of_class("StarterPlayerScripts"), scripts, false);
        }
    }
    if (cfg_.spawn_character) spawn_character();
    camera_.yaw = orbit_yaw_;
    // Bodies of the parts that exist now: a script's Start may raycast before the first step.
    physics_->sync();
    if (cfg_.scripts && cfg_.scripts->available()) {
        // Scripts read the input snapshot (UserInputService) and cast rays against the physics world.
        cfg_.scripts->set_input(&input_);
        cfg_.scripts->set_raycaster([this](vec3 origin, vec3 direction, f32 max_distance, scripting::RayHit& out) {
            physics::RaycastResult hit;
            if (!physics_ || !physics_->raycast(origin, direction, max_distance, hit) || !hit.instance) return false;
            out.instance = hit.instance;
            out.position = hit.position;
            out.normal = hit.normal;
            out.distance = hit.distance;
            return true;
        });
        cfg_.scripts->start(model_);
    }
    for (auto& s : systems_)
        if (!s->start(*this)) TEFRA_LOG_WARN("runtime: system '%s' failed to start", s->name());
    return true;
}

void GameRuntime::spawn_character() {
    anim::AvatarDesc desc;
    desc.name = cfg_.player_name;
    Instance* player = local_player();
    Instance* spawn = nullptr;
    if (player)
        if (const Value* r = player->get("RespawnLocation"); r && r->ref) spawn = model_.find(r->ref);
    if (!spawn) spawn = find_spawn(model_);
    if (spawn) {
        const vec3 p = spawn->get_vec3("Position"), s = spawn->get_vec3("Size");
        desc.origin = p + vec3{0, s.y * 0.5f + 0.05f, 0};
        desc.yaw_deg = spawn->get_vec3("Orientation").y;
    } else if (const Instance* terrain = world::find_terrain(model_)) {
        // No SpawnLocation: stand on the landscape at the origin.
        desc.origin = {0, world::terrain_height(*terrain, 0.0f, 0.0f) + 0.5f, 0};
    } else {
        desc.origin = {0, 0.5f, 0};
    }
    // StarterPlayer.CharacterMesh: an imported rigged character replaces the default R15 avatar.
    Instance* model = nullptr;
    const Instance* starter = model_.service("StarterPlayer");
    const std::string mesh = starter ? starter->get_string("CharacterMesh") : std::string();
    if (!mesh.empty()) {
        anim::SkinnedAvatarDesc sd;
        sd.name = cfg_.player_name;
        sd.mesh_path = resolve_asset(mesh);
        sd.scale = starter->get_float("CharacterMeshScale", 1.0f);
        sd.origin = desc.origin;
        sd.yaw_deg = desc.yaw_deg;
        model = anim::build_skinned_avatar(model_, model_.workspace(), sd);
        if (!model) TEFRA_LOG_WARN("runtime: cannot build the character from %s, using the R15 avatar", mesh.c_str());
    }
    if (!model) model = anim::build_r15_avatar(model_, model_.workspace(), desc);
    if (!model) return;
    character_id_ = model->id();
    if (player) player->set("Character", Value::reference(model->id()), true);
    if (Instance* hum = model->find_first_child_of_class("Humanoid")) {
        const Instance* sp = model_.service("StarterPlayer");
        hum->set("WalkSpeed", Value::number(sp ? sp->get_float("CharacterWalkSpeed", 6.0f) : 6.0f));
        hum->set("JumpHeight", Value::number(sp ? sp->get_float("CharacterJumpHeight", 1.6f) : 1.6f));
        hum->set("DisplayName", Value::string(cfg_.player_name));
        last_health_ = hum->get_float("Health", 100.0f);
    }
    if (const Instance* sp = model_.service("StarterPlayer"))
        clone_children(model_, sp->find_first_child_of_class("StarterCharacterScripts"), model, false);
    controller_ = std::make_unique<physics::CharacterController>(*physics_, *model);
    if (!controller_->valid()) {
        TEFRA_LOG_WARN("runtime: the player character is not valid");
        controller_.reset();
    }
    orbit_yaw_ = radians(desc.yaw_deg);
    died_at_ = -1.0;
    if (player) model_.fire_event(*player, "CharacterAdded", model);
}

void GameRuntime::despawn_character() {
    controller_.reset();
    if (Instance* c = character()) {
        if (Instance* p = local_player()) model_.fire_event(*p, "CharacterRemoving", c);
        model_.destroy(c);
    }
    character_id_ = 0;
    if (Instance* p = local_player()) p->set("Character", Value::reference(0), true);
}

void GameRuntime::respawn() {
    if (!running_) return;
    despawn_character();
    // ScreenGuis with ResetOnSpawn are replaced by fresh copies from StarterGui.
    if (Instance* gui = player_gui()) {
        std::vector<Instance*> old;
        for (Instance* c : gui->children())
            if (c->is_a("ScreenGui") && c->get_bool("ResetOnSpawn", true)) old.push_back(c);
        for (Instance* c : old) model_.destroy(c);
        clone_children(model_, model_.service("StarterGui"), gui, true);
    }
    spawn_character();
}

void GameRuntime::step(f32 dt, const InputState& input) {
    if (!running_) return;
    input_ = input;
    time_ += f64(dt);
    ++frame_;
    // UserInputService.InputBegan / InputEnded: raised on the DataModel root, text = key name (or MouseButtonN).
    static constexpr const char* kMouseNames[] = {"MouseButton1", "MouseButton2", "MouseButton3"};
    if (input_.keyboard) {
        for (usize k = 1; k < static_cast<usize>(Key::Count); ++k) {
            if (input_.pressed[k])
                model_.fire_event(model_.root(), "InputBegan", nullptr, 0.0, key_name(static_cast<Key>(k)));
            if (input_.released[k])
                model_.fire_event(model_.root(), "InputEnded", nullptr, 0.0, key_name(static_cast<Key>(k)));
        }
    }
    for (usize b = 0; b < 3; ++b) {
        if (input_.mouse_pressed[b]) model_.fire_event(model_.root(), "InputBegan", nullptr, 0.0, kMouseNames[b]);
        if (input_.mouse_released[b]) model_.fire_event(model_.root(), "InputEnded", nullptr, 0.0, kMouseNames[b]);
    }
    if (cfg_.scripts && cfg_.scripts->running()) cfg_.scripts->step(dt);
    for (auto& s : systems_) s->pre_physics(*this, dt);

    // Character intent: WASD relative to the camera, Space jumps (held = jumps again on landing).
    if (input_.mouse(MouseButton::Right)) {
        orbit_yaw_ -= input_.mouse_delta.x * 0.005f;
        orbit_pitch_ = clamp(orbit_pitch_ - input_.mouse_delta.y * 0.005f, radians(-80.0f), radians(60.0f));
    }
    if (input_.hovered && input_.wheel != 0.0f) orbit_dist_ = clamp(orbit_dist_ - input_.wheel * 1.2f, 0.5f, 40.0f);
    vec3 move{};
    const vec3 fwd{-std::sin(orbit_yaw_), 0.0f, -std::cos(orbit_yaw_)};
    const vec3 right{std::cos(orbit_yaw_), 0.0f, -std::sin(orbit_yaw_)};
    if (input_.key(Key::W) || input_.key(Key::Up)) move = move + fwd;
    if (input_.key(Key::S) || input_.key(Key::Down)) move = move - fwd;
    if (input_.key(Key::D) || input_.key(Key::Right)) move = move + right;
    if (input_.key(Key::A) || input_.key(Key::Left)) move = move - right;
    move = length(move) > 0.01f ? normalize(move) : vec3{};
    Instance* hum = humanoid();
    const bool alive = hum && died_at_ < 0.0;
    if (controller_ && alive) {
        controller_->set_move_direction(move);
        if (input_.key(Key::Space)) controller_->jump();
        hum->set("MoveDirection", Value::vector3(move), true);
    } else if (controller_) {
        controller_->set_move_direction({});
    }

    physics_->step(dt);
    if (controller_)
        if (Instance* c = character())
            if (Instance* root = c->find_first_child("HumanoidRootPart"))
                root->set("AssemblyLinearVelocity", Value::vector3(controller_->velocity()), true);
    animator_->update(dt);
    update_humanoids(dt);
    update_camera(dt);
    for (auto& s : systems_) s->post_physics(*this, dt);
}

void GameRuntime::update_humanoids(f32 dt) {
    (void)dt;
    Instance* hum = humanoid();
    if (!hum) return;
    const f32 health = hum->get_float("Health", 100.0f);
    if (health != last_health_) {
        last_health_ = health;
        model_.fire_event(*hum, "HealthChanged", nullptr, f64(health));
    }
    // Falling out of the world kills.
    if (died_at_ < 0.0) {
        const f32 floor_y = model_.workspace()->get_float("FallenPartsDestroyHeight", -500.0f);
        if (Instance* c = character())
            if (Instance* root = c->find_first_child("HumanoidRootPart"))
                if (root->get_vec3("Position").y < floor_y) hum->set("Health", Value::number(0.0));
    }
    if (died_at_ < 0.0 && hum->get_float("Health", 100.0f) <= 0.0f) {
        died_at_ = time_;
        hum->set("State", Value::enumeration(kStateDead), true);
        controller_.reset();
        model_.fire_event(*hum, "Died");
    }
    if (died_at_ >= 0.0) {
        const Instance* players = model_.service("Players");
        const f64 wait = players ? f64(players->get_float("RespawnTime", 3.0f)) : 3.0;
        if (time_ - died_at_ >= wait) respawn();
    }
}

void GameRuntime::update_camera(f32 dt) {
    Instance* c = character();
    Instance* head = c ? c->find_first_child("Head") : nullptr;
    if (!head) return;
    const vec3 target = head->get_vec3("Position") + vec3{0, 0.4f, 0};
    const vec3 dir{-std::sin(orbit_yaw_) * std::cos(orbit_pitch_), std::sin(orbit_pitch_),
                   -std::cos(orbit_yaw_) * std::cos(orbit_pitch_)};
    vec3 want = target - dir * orbit_dist_;
    // Keep the camera out of walls: stop in front of the first solid part (not the character) behind the head.
    vec3 origin = target;
    f32 travelled = 0.0f;
    for (int i = 0; i < 4 && travelled < orbit_dist_; ++i) {
        physics::RaycastResult hit;
        if (!physics_->raycast(origin, -dir, orbit_dist_ - travelled, hit) || !hit.instance) break;
        if (!hit.instance->is_descendant_of(c)) {
            want = target - dir * max(travelled + hit.distance - 0.3f, 0.5f);
            break;
        }
        travelled += hit.distance + 0.05f;
        origin = target - dir * travelled;
    }
    const f32 k = frame_ <= 1 ? 1.0f : 1.0f - std::exp(-dt * 14.0f);
    camera_.position = camera_.position + (want - camera_.position) * k;
    camera_.yaw = orbit_yaw_;
    camera_.pitch = orbit_pitch_;
}

void GameRuntime::stop() {
    if (!running_) return;
    for (auto it = systems_.rbegin(); it != systems_.rend(); ++it) (*it)->stop(*this);
    if (cfg_.scripts && cfg_.scripts->running()) cfg_.scripts->stop();
    // The raycaster captures this runtime and the input snapshot is a member of it: the host must not reach either
    // after the physics world or this runtime is gone.
    if (cfg_.scripts) {
        cfg_.scripts->set_raycaster(nullptr);
        cfg_.scripts->set_input(nullptr);
    }
    controller_.reset();
    animator_.reset();
    if (physics_ && touched_listener_ != ~usize(0)) physics_->remove_touched_listener(touched_listener_);
    touched_listener_ = ~usize(0);
    physics_.reset();
    character_id_ = 0;
    player_id_ = 0;
    running_ = false;
}

} // namespace tefra::runtime
