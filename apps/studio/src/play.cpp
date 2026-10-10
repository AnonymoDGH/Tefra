// Play mode for Tefra Studio (spec M3 §1.3): C# scripts, Jolt physics, the player's R15 avatar driven by a character
// controller and animated by the Animator, a third-person camera, and the MCP server with Studio's own tools.
#include "tefra/studio/app.h"

#include "tefra/anim/animator.h"
#include "tefra/anim/avatar.h"
#include "tefra/core/fs.h"
#include "tefra/core/image.h"
#include "tefra/core/log.h"
#include "tefra/mcp/server.h"
#include "tefra/mcp/world_tools.h"
#include "tefra/physics/character.h"
#include "tefra/physics/physics_world.h"
#include "tefra/rhi/commands.h"
#include "tefra/rhi/device.h"
#include "tefra/rhi/frame.h"
#include "tefra/rhi/readback.h"
#include "tefra/scripting/script_host.h"
#include "tefra/world/scene_sync.h"

#include <imgui.h>

#include <cmath>
#include <cstdio>

namespace tefra::studio {

using world::Instance;
using world::Value;

namespace {
std::string base64(const std::vector<u8>& data) {
    static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((data.size() + 2) / 3 * 4);
    for (usize i = 0; i < data.size(); i += 3) {
        const u32 b0 = data[i], b1 = i + 1 < data.size() ? data[i + 1] : 0u,
                  b2 = i + 2 < data.size() ? data[i + 2] : 0u;
        const u32 v = (b0 << 16) | (b1 << 8) | b2;
        out += tbl[(v >> 18) & 63];
        out += tbl[(v >> 12) & 63];
        out += i + 1 < data.size() ? tbl[(v >> 6) & 63] : '=';
        out += i + 2 < data.size() ? tbl[v & 63] : '=';
    }
    return out;
}

Instance* find_spawn(world::DataModel& dm) {
    for (Instance* i : dm.workspace()->descendants())
        if (i->is_a("SpawnLocation") && i->get_bool("Enabled", true)) return i;
    return nullptr;
}
} // namespace

void App::setup_play_systems() {
    scripts_ = std::make_unique<scripting::ScriptHost>();
    scripts_->set_log_callback([](LogLevel level, std::string_view msg) {
        log_write(level, "%.*s", static_cast<int>(msg.size()), msg.data());
    });
    std::string err;
    scripts_available_ = scripts_->init(&err);
    if (scripts_available_)
        TEFRA_LOG_INFO("C# scripting ready (.NET)");
    else
        TEFRA_LOG_WARN("C# scripting unavailable: %s (install the .NET 8 SDK to run scripts)", err.c_str());

    PlayHooks hooks;
    hooks.name = "game";
    hooks.start = [this](world::DataModel& dm) { return play_start(dm); };
    hooks.step = [this](world::DataModel& dm, f32 dt) { play_step(dm, dt); };
    hooks.stop = [this] { play_stop(); };
    session_.add_hooks(std::move(hooks));
}

bool App::play_start(world::DataModel& dm) {
    edit_camera_ = camera_;
    physics_ = std::make_unique<physics::PhysicsWorld>(dm);
    // The player's avatar appears on the first enabled SpawnLocation (or at the origin).
    anim::AvatarDesc desc;
    desc.name = "Player";
    if (Instance* spawn = find_spawn(dm)) {
        const vec3 p = spawn->get_vec3("Position"), s = spawn->get_vec3("Size");
        desc.origin = p + vec3{0, s.y * 0.5f + 0.05f, 0};
    } else {
        desc.origin = {0, 0.5f, 0};
    }
    Instance* model = anim::build_r15_avatar(dm, dm.workspace(), desc);
    player_model_ = model ? model->id() : 0;
    if (model) {
        if (Instance* hum = model->find_first_child_of_class("Humanoid")) {
            const Instance* sp = dm.service("StarterPlayer");
            hum->set("WalkSpeed", Value::number(sp ? sp->get_float("CharacterWalkSpeed", 6.0f) : 6.0f));
            hum->set("JumpHeight", Value::number(sp ? sp->get_float("CharacterJumpHeight", 1.6f) : 1.6f));
        }
        player_ = std::make_unique<physics::CharacterController>(*physics_, *model);
        if (!player_->valid()) {
            TEFRA_LOG_WARN("Play: the player character is not valid");
            player_.reset();
        }
    }
    animator_ = std::make_unique<anim::AnimatorSystem>(dm);
    orbit_yaw_ = camera_.yaw;
    orbit_pitch_ = radians(-15.0f);
    orbit_dist_ = 9.0f;
    if (scripts_available_) scripts_->start(dm);
    return true;
}

void App::play_step(world::DataModel& dm, f32 dt) {
    if (scripts_available_ && scripts_->running()) scripts_->step(dt);
    if (player_) {
        player_->set_move_direction(play_move_);
        if (play_jump_) player_->jump();
    }
    play_jump_ = false;
    if (physics_) physics_->step(dt);
    if (player_) {
        if (Instance* model = dm.find(player_model_))
            if (Instance* root = model->find_first_child("HumanoidRootPart"))
                root->set("AssemblyLinearVelocity", Value::vector3(player_->velocity()), true);
    }
    if (animator_) animator_->update(dt);
}

void App::play_stop() {
    if (scripts_ && scripts_->running()) scripts_->stop();
    animator_.reset();
    player_.reset();
    physics_.reset();
    player_model_ = 0;
    camera_ = edit_camera_;
}

// Third-person camera: right mouse orbits, wheel zooms, WASD moves relative to the camera, Space jumps.
void App::play_camera_input(f32 dt, f32 aspect) {
    (void)aspect;
    ImGuiIO& io = ImGui::GetIO();
    if (viewport_hovered_ && io.MouseDown[1]) {
        orbit_yaw_ -= io.MouseDelta.x * 0.005f;
        orbit_pitch_ = clamp(orbit_pitch_ - io.MouseDelta.y * 0.005f, radians(-80.0f), radians(60.0f));
    }
    if (viewport_hovered_ && io.MouseWheel != 0.0f)
        orbit_dist_ = clamp(orbit_dist_ - io.MouseWheel * 1.2f, 2.0f, 40.0f);
    vec3 m{};
    const bool typing = io.WantTextInput;
    if (!typing) {
        const vec3 f{-std::sin(orbit_yaw_), 0.0f, -std::cos(orbit_yaw_)};
        const vec3 r{std::cos(orbit_yaw_), 0.0f, -std::sin(orbit_yaw_)};
        if (ImGui::IsKeyDown(ImGuiKey_W) || ImGui::IsKeyDown(ImGuiKey_UpArrow)) m = m + f;
        if (ImGui::IsKeyDown(ImGuiKey_S) || ImGui::IsKeyDown(ImGuiKey_DownArrow)) m = m - f;
        if (ImGui::IsKeyDown(ImGuiKey_D) || ImGui::IsKeyDown(ImGuiKey_RightArrow)) m = m + r;
        if (ImGui::IsKeyDown(ImGuiKey_A) || ImGui::IsKeyDown(ImGuiKey_LeftArrow)) m = m - r;
        if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) play_jump_ = true;
    }
    play_move_ = length(m) > 0.01f ? normalize(m) : vec3{};
    // Follow the head of the avatar (smoothly).
    Instance* model = session_.model().find(player_model_);
    Instance* head = model ? model->find_first_child("Head") : nullptr;
    if (!head) return;
    const vec3 target = head->get_vec3("Position") + vec3{0, 0.4f, 0};
    const vec3 dir{-std::sin(orbit_yaw_) * std::cos(orbit_pitch_), std::sin(orbit_pitch_),
                   -std::cos(orbit_yaw_) * std::cos(orbit_pitch_)};
    vec3 want = target - dir * orbit_dist_;
    // Keep the camera out of walls: pull it in front of the first part between the head and the camera.
    if (sync_) {
        world::RayHit hit = sync_->raycast(target, -dir, orbit_dist_, nullptr);
        if (hit.instance && !hit.instance->is_descendant_of(model))
            want = target - dir * max(hit.distance - 0.3f, 1.0f);
    }
    const vec3 cur{f32(camera_.position.x), f32(camera_.position.y), f32(camera_.position.z)};
    const f32 k = 1.0f - std::exp(-dt * 14.0f);
    const vec3 p = cur + (want - cur) * k;
    camera_.position = {f64(p.x), f64(p.y), f64(p.z)};
    camera_.yaw = orbit_yaw_;
    camera_.pitch = orbit_pitch_;
}

// ---- MCP ----------------------------------------------------------------------------------------------------------

bool App::capture_viewport_now(ImageRGBA8& out) {
    if (!viewport_image_.image) return false;
    frames_->wait_all();
    if (!oneshot_pool_) {
        VkCommandPoolCreateInfo ci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        ci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        ci.queueFamilyIndex = device_->queue_family();
        vkCreateCommandPool(device_->device(), &ci, nullptr, &oneshot_pool_);
    }
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = oneshot_pool_;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(device_->device(), &ai, &cmd);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    rhi::ReadbackRequest rb = rhi::cmd_readback_image(*device_, cmd, viewport_image_.image, VK_FORMAT_R8G8B8A8_SRGB,
                                                      viewport_extent_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                                      VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_NONE);
    rhi::ImageBarrier back;
    back.image = viewport_image_.image;
    back.old_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    back.new_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    back.src_stage = VK_PIPELINE_STAGE_2_COPY_BIT;
    back.dst_stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    back.dst_access = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    rhi::cmd_image_barrier(cmd, back);
    vkEndCommandBuffer(cmd);
    VkCommandBufferSubmitInfo cbi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    cbi.commandBuffer = cmd;
    VkSubmitInfo2 si{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    si.commandBufferInfoCount = 1;
    si.pCommandBufferInfos = &cbi;
    vkQueueSubmit2KHR(device_->queue(), 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(device_->queue());
    vkFreeCommandBuffers(device_->device(), oneshot_pool_, 1, &cmd);
    return rhi::finish_readback(*device_, rb, out);
}

void App::setup_mcp() {
    if (cfg_.mcp_port == 0) return;
    mcp_ = std::make_unique<mcp::Server>();
    mcp::register_world_tools(*mcp_, [this]() -> world::DataModel& { return session_.model(); });
    mcp_->add_tool("play", "Starts play mode (compiles and runs the C# scripts, spawns the player, runs physics).",
                   R"({"type":"object","properties":{}})", [this](const JsonValue&) {
                       session_.play();
                       return mcp::ToolResult::text("playing");
                   });
    mcp_->add_tool("stop", "Stops play mode and restores the place as it was before Play.",
                   R"({"type":"object","properties":{}})", [this](const JsonValue&) {
                       session_.stop();
                       return mcp::ToolResult::text("stopped");
                   });
    mcp_->add_tool("get_output", "Returns the last lines of the Output panel (engine log, script prints and errors).",
                   R"({"type":"object","properties":{"lines":{"type":"integer","minimum":1,"maximum":2000}}})",
                   [this](const JsonValue& a) {
                       const usize n = static_cast<usize>(a["lines"].as_int(100));
                       std::string text;
                       for (const LogLine& l : log_tail(n)) {
                           char t[32];
                           std::snprintf(t, sizeof(t), "[%8.2f] ", l.time);
                           text += t;
                           text += l.text;
                           text += '\n';
                       }
                       return mcp::ToolResult::text(text);
                   });
    mcp_->add_tool("capture_viewport", "Returns a PNG of the 3D viewport as the user currently sees it.",
                   R"({"type":"object","properties":{}})", [this](const JsonValue&) {
                       ImageRGBA8 img;
                       if (!capture_viewport_now(img)) return mcp::ToolResult::error("the viewport is not ready yet");
                       const std::string tmp = path_join(exe_dir(), "mcp_capture.png");
                       std::vector<u8> png;
                       if (!image_write_png(tmp.c_str(), img) || !file_read_all(tmp.c_str(), png))
                           return mcp::ToolResult::error("cannot encode the PNG");
                       return mcp::ToolResult::image(base64(png));
                   });
    mcp_->add_tool("set_camera", "Moves the editor camera to look at a point (metres).",
                   R"({"type":"object","properties":{"position":{"type":"array"},"look_at":{"type":"array"}},)"
                   R"("required":["position","look_at"]})",
                   [this](const JsonValue& a) {
                       const JsonValue &p = a["position"], &t = a["look_at"];
                       if (p.size() != 3 || t.size() != 3)
                           return mcp::ToolResult::error("position and look_at: [x,y,z]");
                       const vec3 pos{f32(p.at(0).as_number()), f32(p.at(1).as_number()), f32(p.at(2).as_number())};
                       const vec3 tgt{f32(t.at(0).as_number()), f32(t.at(1).as_number()), f32(t.at(2).as_number())};
                       const vec3 d = normalize(tgt - pos);
                       camera_.position = {f64(pos.x), f64(pos.y), f64(pos.z)};
                       camera_.yaw = std::atan2(-d.x, -d.z);
                       camera_.pitch = std::asin(clamp(d.y, -1.0f, 1.0f));
                       return mcp::ToolResult::text("ok");
                   });
    if (mcp_->start(cfg_.mcp_port))
        TEFRA_LOG_INFO("MCP server: http://127.0.0.1:%u/mcp  (claude mcp add --transport http tefra "
                       "http://127.0.0.1:%u/mcp)",
                       u32(cfg_.mcp_port), u32(cfg_.mcp_port));
    else
        TEFRA_LOG_WARN("MCP server: port %u is busy, AI tools disabled", u32(cfg_.mcp_port));
    add_frame_callback([this](f32) {
        if (mcp_) mcp_->poll();
    });
}

} // namespace tefra::studio
