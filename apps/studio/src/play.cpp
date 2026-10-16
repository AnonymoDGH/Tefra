// Play mode for Tefra Studio (spec M3 §1.3): C# scripts, Jolt physics, the player's R15 avatar driven by a character
// controller and animated by the Animator, a third-person camera, and the MCP server with Studio's own tools.
#include "tefra/studio/app.h"

#include "tefra/audio/audio_system.h"
#include "tefra/core/fs.h"
#include "tefra/core/image.h"
#include "tefra/core/log.h"
#include "tefra/mcp/server.h"
#include "tefra/mcp/world_tools.h"
#include "tefra/platform/window.h"
#include "tefra/rbx/importer.h"
#include "tefra/rhi/commands.h"
#include "tefra/rhi/device.h"
#include "tefra/rhi/frame.h"
#include "tefra/rhi/readback.h"
#include "tefra/runtime/game_runtime.h"
#include "tefra/scripting/script_host.h"
#include "tefra/studio/templates.h"
#include "tefra/ui/game_gui.h"
#include "tefra/ui/imgui_input.h"
#include "tefra/world/environment_sync.h"
#include "tefra/world/scene_sync.h"

#include <GLFW/glfw3.h>
#include <imgui.h>

#include <cmath>
#include <cstdio>
#include <functional>

namespace tefra::studio {

using world::Instance;
using world::Value;

namespace {

// Floating objects (BasePart with the attribute Floating = true): each step the part rides the animated sea. Four
// samples of the ocean surface (bow, stern, port, starboard; FloatLength / FloatWidth metres apart) give heave,
// pitch and roll; a first-order lag stands in for the hull's inertia. Descendant parts (lights, masts) move with it.
class BuoyancySystem final : public runtime::RuntimeSystem {
  public:
    using Sampler = std::function<bool(f32, f32, f32, f32&)>;
    BuoyancySystem(Sampler sample, std::function<f32()> clock) : sample_(std::move(sample)), clock_(std::move(clock)) {}
    const char* name() const override { return "buoyancy"; }

    bool start(runtime::GameRuntime& rt) override {
        for (world::Instance* i : rt.model().workspace()->descendants()) {
            const world::Value* f = i->get_attribute("Floating");
            if (!f || !i->is_a("BasePart") || f->n[0] == 0.0) continue;
            Body b;
            b.part = i;
            b.pos = i->get_vec3("Position");
            b.rot = world::orientation_to_quat(i->get_vec3("Orientation"));
            auto num = [&](const char* n, f32 def) {
                const world::Value* v = i->get_attribute(n);
                return v ? f32(v->n[0]) : def;
            };
            b.length = max(num("FloatLength", 4.0f), 0.5f);
            b.width = max(num("FloatWidth", 1.6f), 0.3f);
            b.draft = num("FloatDraft", 0.3f);
            b.heave = b.pos.y;
            const quat inv{-b.rot.x, -b.rot.y, -b.rot.z, b.rot.w};
            for (world::Instance* c : i->descendants())
                if (c->is_a("BasePart"))
                    b.attached.push_back({c, rotate(inv, c->get_vec3("Position") - b.pos),
                                          inv * world::orientation_to_quat(c->get_vec3("Orientation"))});
            bodies_.push_back(std::move(b));
        }
        return true;
    }

    void pre_physics(runtime::GameRuntime& rt, f32 dt) override {
        (void)rt;
        const f32 t = clock_();
        const f32 k = 1.0f - std::exp(-dt / 0.35f);
        for (Body& b : bodies_) {
            const vec3 fwd = rotate(b.rot, vec3{0, 0, -1}), right = rotate(b.rot, vec3{1, 0, 0});
            auto h = [&](vec3 o) {
                f32 y = b.pos.y;
                sample_(b.pos.x + o.x, b.pos.z + o.z, t, y);
                return y;
            };
            const f32 hb = h(fwd * (b.length * 0.5f)), hs = h(fwd * (-b.length * 0.5f));
            const f32 hr = h(right * (b.width * 0.5f)), hl = h(right * (-b.width * 0.5f));
            b.heave += ((hb + hs + hr + hl) * 0.25f - b.draft - b.heave) * k;
            b.pitch += (std::atan2(hb - hs, b.length) - b.pitch) * k;
            b.roll += (std::atan2(hr - hl, b.width) - b.roll) * k;
            const quat r = b.rot * quat_axis_angle({1, 0, 0}, b.pitch) * quat_axis_angle({0, 0, 1}, b.roll);
            const vec3 p{b.pos.x, b.heave, b.pos.z};
            b.part->set("Position", world::Value::vector3(p));
            b.part->set("Orientation", world::Value::vector3(world::quat_to_orientation(r)));
            for (const Attached& a : b.attached) {
                a.part->set("Position", world::Value::vector3(p + rotate(r, a.offset)));
                a.part->set("Orientation", world::Value::vector3(world::quat_to_orientation(r * a.rot)));
            }
        }
    }

  private:
    struct Attached {
        world::Instance* part;
        vec3 offset;
        quat rot;
    };
    struct Body {
        world::Instance* part = nullptr;
        vec3 pos{};
        quat rot{};
        f32 length = 4.0f, width = 1.6f, draft = 0.3f;
        f32 heave = 0.0f, pitch = 0.0f, roll = 0.0f;
        std::vector<Attached> attached;
    };
    Sampler sample_;
    std::function<f32()> clock_;
    std::vector<Body> bodies_;
};

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

} // namespace

void App::setup_play_systems() {
    scripts_ = std::make_unique<scripting::ScriptHost>();
    scripts_->set_log_callback([](LogLevel level, std::string_view msg) {
        log_write(level, "%.*s", static_cast<int>(msg.size()), msg.data());
    });
    std::string err;
    if (cfg_.player && !is_dir(path_join(exe_dir(), "managed"))) {
        // Exported games without C# scripts ship no managed runtime.
        scripts_available_ = false;
        TEFRA_LOG_INFO("No C# scripts in this game");
    } else {
        scripts_available_ = scripts_->init(&err);
    }
    if (scripts_available_)
        TEFRA_LOG_INFO("C# scripting ready (.NET)");
    else if (!err.empty())
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
    runtime::RuntimeConfig rc;
    rc.scripts = scripts_available_ ? scripts_.get() : nullptr;
    rc.project_dir = session_.path().empty() ? std::string() : path_parent(session_.path());
    runtime_ = std::make_unique<runtime::GameRuntime>(dm, rc);
    runtime_->add_system(audio::make_audio_system());
    runtime_->add_system(std::make_unique<BuoyancySystem>(
        [this](f32 x, f32 z, f32 t, f32& y) {
            auto* env = static_cast<const world::EnvironmentSync*>(environment_.get());
            return env && env->ocean_height(x, z, t, y);
        },
        [this] { return f32(time_); }));
    if (!game_gui_) game_gui_ = std::make_unique<ui::GameGui>();
    game_gui_->set_asset_root(rc.project_dir);
    return runtime_->start();
}

void App::play_step(world::DataModel& dm, f32 dt) {
    (void)dm;
    if (!runtime_) return;
    runtime_->step(dt, play_input_);
    const runtime::CameraState& c = runtime_->camera();
    camera_.position = {f64(c.position.x), f64(c.position.y), f64(c.position.z)};
    camera_.yaw = c.yaw;
    camera_.pitch = c.pitch;
    camera_.fovy = c.fov_y;
    camera_.z_near = runtime_->first_person() ? 0.05f : 0.1f;
    // Relative mouse while the game steers the camera with it (orbit, shift lock, first person).
    auto* win = static_cast<GLFWwindow*>(window_->native_handle());
    const bool lock = runtime_->mouse_locked();
    if (lock != cursor_locked_) {
        glfwSetInputMode(win, GLFW_CURSOR, lock ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        if (glfwRawMouseMotionSupported()) glfwSetInputMode(win, GLFW_RAW_MOUSE_MOTION, lock ? GLFW_TRUE : GLFW_FALSE);
        ImGuiIO& io = ImGui::GetIO();
        if (lock)
            io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
        else
            io.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
        cursor_locked_ = lock;
    }
}

void App::play_stop() {
    if (cursor_locked_) {
        glfwSetInputMode(static_cast<GLFWwindow*>(window_->native_handle()), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
        cursor_locked_ = false;
    }
    camera_.fovy = radians(70.0f);
    camera_.z_near = 0.1f;
    if (runtime_) runtime_->stop();
    runtime_.reset();
    camera_ = edit_camera_;
}

// Play input: the viewport is the game view (WASD move, Space jump, right mouse orbits, wheel zooms); the in-game
// UI is drawn over it and gets the clicks first.
void App::play_input(vec2 view_min, vec2 view_size) {
    const ImGuiIO& io = ImGui::GetIO();
    ui::input_from_imgui(play_input_, view_min, view_size, viewport_hovered_ || cursor_locked_, !io.WantTextInput);
    if (runtime_ && game_gui_) {
        game_gui_->set_fonts(font_regular_, font_bold_, font_mono_);
        game_gui_->enable_textures(*device_);
        if (game_gui_->wants_keyboard()) play_input_.keyboard = false; // a TextBox is being typed into
        const runtime::CameraState& cs = runtime_->camera();
        const ui::GuiCamera gui_cam = ui::make_gui_camera(cs.position, cs.yaw, cs.pitch, cs.fov_y);
        if (world::Instance* gui = runtime_->player_gui())
            game_gui_->draw(ImGui::GetWindowDrawList(), {view_min, view_size}, *gui, &play_input_, true, &gui_cam);
    }
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

    // ---- Landscape measurement, places from templates and asset import (for agents that build places) ----
    mcp_->add_tool("terrain_height",
                   "Height (metres) of the procedural Terrain at points [[x,z],...] (up to 10000). Use it to put "
                   "parts on the ground: Position.y = height + Size.y/2.",
                   R"({"type":"object","properties":{"points":{"type":"array","description":"[[x,z],...]"}},)"
                   R"("required":["points"]})",
                   [this](const JsonValue& a) {
                       const Instance* terrain = world::find_terrain(session_.model());
                       if (!terrain)
                           return mcp::ToolResult::error("the place has no Terrain with a Kind other than None");
                       const JsonValue& pts = a["points"];
                       if (!pts.is_array() || pts.size() == 0 || pts.size() > 10000)
                           return mcp::ToolResult::error("points: 1..10000 pairs [x,z]");
                       JsonValue out = JsonValue::array();
                       for (usize i = 0; i < pts.size(); ++i) {
                           const JsonValue& q = pts.at(i);
                           const f32 x = f32(q.at(0).as_number()), z = f32(q.at(1).as_number());
                           out.push(JsonValue(f64(world::terrain_height(*terrain, x, z))));
                       }
                       JsonValue r = JsonValue::object();
                       r.set("heights", std::move(out));
                       return mcp::ToolResult::json(r);
                   });
    mcp_->add_tool("raycast",
                   "Casts a ray against parts and the Terrain. Returns the first hit: position, normal, distance and "
                   "the part (path) or \"Terrain\".",
                   R"({"type":"object","properties":{"origin":{"type":"array"},"direction":{"type":"array"},)"
                   R"("max_distance":{"type":"number"}},"required":["origin","direction"]})",
                   [this](const JsonValue& a) {
                       const JsonValue &o = a["origin"], &d = a["direction"];
                       if (o.size() != 3 || d.size() != 3)
                           return mcp::ToolResult::error("origin and direction: [x,y,z]");
                       const vec3 org{f32(o.at(0).as_number()), f32(o.at(1).as_number()), f32(o.at(2).as_number())};
                       vec3 dir{f32(d.at(0).as_number()), f32(d.at(1).as_number()), f32(d.at(2).as_number())};
                       if (length(dir) < 1e-6f) return mcp::ToolResult::error("direction is zero");
                       dir = normalize(dir);
                       const f32 maxd = clamp(f32(a["max_distance"].as_number(5000.0)), 0.1f, 20000.0f);
                       world::RayHit hit;
                       if (sync_) hit = sync_->raycast(org, dir, maxd);
                       f32 best = hit.instance ? hit.distance : maxd;
                       JsonValue r = JsonValue::object();
                       bool any = hit.instance != nullptr;
                       vec3 pos = hit.position, nrm = hit.normal;
                       std::string what = hit.instance ? hit.instance->full_name() : std::string();
                       // Terrain: march in 0.5 m steps, then bisect the crossing.
                       if (const Instance* terrain = world::find_terrain(session_.model())) {
                           auto above = [&](f32 t) {
                               const vec3 p = org + dir * t;
                               return p.y - world::terrain_height(*terrain, p.x, p.z);
                           };
                           f32 prev = 0.0f, prev_h = above(0.0f);
                           const f32 step = 0.5f;
                           for (f32 t = step; t <= best && prev_h > 0.0f; t += step) {
                               const f32 h = above(t);
                               if (h <= 0.0f) {
                                   f32 lo = prev, hi = t;
                                   for (int k = 0; k < 20; ++k) {
                                       const f32 m = 0.5f * (lo + hi);
                                       (above(m) > 0.0f ? lo : hi) = m;
                                   }
                                   best = hi;
                                   pos = org + dir * hi;
                                   const f32 e = 0.25f, hx = world::terrain_height(*terrain, pos.x + e, pos.z) -
                                                             world::terrain_height(*terrain, pos.x - e, pos.z);
                                   const f32 hz = world::terrain_height(*terrain, pos.x, pos.z + e) -
                                                  world::terrain_height(*terrain, pos.x, pos.z - e);
                                   nrm = normalize(vec3{-hx, 2.0f * e, -hz});
                                   what = "Terrain";
                                   any = true;
                                   break;
                               }
                               prev = t;
                               prev_h = h;
                           }
                       }
                       r.set("hit", JsonValue(any));
                       if (any) {
                           auto v3 = [](vec3 v) {
                               JsonValue j = JsonValue::array();
                               j.push(JsonValue(f64(v.x)));
                               j.push(JsonValue(f64(v.y)));
                               j.push(JsonValue(f64(v.z)));
                               return j;
                           };
                           r.set("position", v3(pos));
                           r.set("normal", v3(nrm));
                           r.set("distance", JsonValue(f64(what == "Terrain" ? best : hit.distance)));
                           r.set("instance", JsonValue(what));
                       }
                       return mcp::ToolResult::json(r);
                   });
    {
        std::string ids;
        for (const TemplateInfo& t : templates()) ids += std::string(ids.empty() ? "" : ", ") + t.id;
        const std::string desc = "Replaces the open place with a new one built from a template (" + ids +
                                 "). meadow = grassy plain with wind-animated grass, beach = coast with ocean, "
                                 "valley = forest valley, obby = complete obstacle game. project_name saves it as a "
                                 "new project. Refused when the place has unsaved changes unless discard=true.";
        mcp_->add_tool(
            "new_place_from_template", desc,
            R"({"type":"object","properties":{"template":{"type":"string"},"project_name":{"type":"string"},)"
            R"("discard":{"type":"boolean"}},"required":["template"]})",
            [this](const JsonValue& a) {
                const TemplateInfo* t = find_template(a["template"].as_string());
                if (!t) return mcp::ToolResult::error("unknown template");
                if (session_.modified() && !a["discard"].as_bool(false))
                    return mcp::ToolResult::error("the place has unsaved changes: save it or pass discard=true");
                if (session_.play_state() != PlayState::Edit) session_.stop();
                auto m = std::make_unique<world::DataModel>();
                build_template(t->id, *m);
                std::string path;
                const std::string name = a["project_name"].as_string();
                if (!name.empty()) path = projects_.create_project(name);
                session_.adopt(std::move(m), path);
                if (!path.empty() && session_.save()) projects_.touch_recent(path);
                screen_ = Screen::Editor;
                frame_camera_for_template(t->id);
                projects_dirty_ = true;
                return mcp::ToolResult::text(std::string("new place from ") + t->name +
                                             (path.empty() ? " (untitled)" : ": " + path));
            });
    }
    mcp_->add_tool(
        "import_model",
        "Inserts a 3D model as a MeshPart. Accepts .glb/.gltf (used as they are) and .fbx, .obj (+ .mtl), .stl and "
        ".ply (converted to .glb in metres, +Y up). In a saved project the file goes into its assets folder. scale "
        "multiplies the model's own size; unit_scale sets the unit of OBJ/STL/PLY files (1 = metres, 0.01 = cm); "
        "max_triangles simplifies the model (0 = keep).",
        R"({"type":"object","properties":{"path":{"type":"string"},"name":{"type":"string"},)"
        R"("position":{"type":"array"},"scale":{"type":"number"},"orientation":{"type":"array"},)"
        R"("unit_scale":{"type":"number"},"max_triangles":{"type":"number"},)"
        R"("parent_path":{"type":"string"}},"required":["path"]})",
        [this](const JsonValue& a) {
            const std::string src = a["path"].as_string();
            render::ModelImportOptions opt;
            opt.unit_scale = a["unit_scale"].is_number() ? f32(a["unit_scale"].as_number()) : 1.0f;
            const f64 max_tris = a["max_triangles"].as_number(0.0);
            opt.max_triangles = max_tris > 0.0 ? u32(max_tris) : 0u;
            std::string mesh_id, err;
            render::ModelImportReport rep;
            if (!import_model_asset(src, opt, mesh_id, err, &rep)) return mcp::ToolResult::error(err);
            world::DataModel& dm = session_.model();
            Instance* parent = dm.workspace();
            if (a["parent_path"].is_string()) {
                parent = dm.find_path(a["parent_path"].as_string());
                if (!parent) return mcp::ToolResult::error("parent not found");
            }
            const std::string name = a["name"].is_string() ? a["name"].as_string() : path_stem(src);
            Instance* mp = dm.create("MeshPart", parent, name);
            if (!mp) return mcp::ToolResult::error("cannot create the MeshPart");
            const f32 s = clamp(f32(a["scale"].as_number(1.0)), 0.001f, 1000.0f);
            mp->set("MeshId", Value::string(mesh_id));
            mp->set("Size", Value::vector3({s, s, s}));
            mp->set("Anchored", Value::boolean(true));
            const JsonValue& p = a["position"];
            if (p.size() == 3)
                mp->set("Position",
                        Value::vector3({f32(p.at(0).as_number()), f32(p.at(1).as_number()), f32(p.at(2).as_number())}));
            const JsonValue& o = a["orientation"];
            if (o.size() == 3)
                mp->set("Orientation",
                        Value::vector3({f32(o.at(0).as_number()), f32(o.at(1).as_number()), f32(o.at(2).as_number())}));
            JsonValue r = JsonValue::object();
            r.set("id", JsonValue(f64(mp->id())));
            r.set("path", JsonValue(mp->full_name()));
            r.set("MeshId", JsonValue(mesh_id));
            if (!rep.format.empty()) r.set("format", JsonValue(rep.format));
            if (!rep.warnings.empty()) {
                JsonValue ws = JsonValue::array();
                for (const std::string& w : rep.warnings) ws.push(JsonValue(w));
                r.set("warnings", ws);
            }
            return mcp::ToolResult::json(r);
        });
    mcp_->add_tool(
        "import_roblox",
        "Imports a Roblox file the user owns. .rbxm/.rbxmx models go into the open place (under parent_path or the "
        "Workspace); .rbxl/.rbxlx places become a new project (discard=true needed when there are unsaved changes). "
        "Luau scripts are kept disabled in LuauSource.",
        R"({"type":"object","properties":{"path":{"type":"string"},"parent_path":{"type":"string"},)"
        R"("discard":{"type":"boolean"}},"required":["path"]})",
        [this](const JsonValue& a) {
            const std::string path = a["path"].as_string();
            const std::string ext = path_extension(path);
            if (!file_exists(path.c_str())) return mcp::ToolResult::error("file not found: " + path);
            rbx::ImportOptions opt;
            rbx::ImportReport rep;
            std::string err;
            if (ext == ".rbxm" || ext == ".rbxmx") {
                Instance* parent = session_.model().workspace();
                if (a["parent_path"].is_string()) parent = session_.model().find_path(a["parent_path"].as_string());
                if (!parent) return mcp::ToolResult::error("parent not found");
                if (!rbx::import_model(path.c_str(), parent, opt, &rep, &err, nullptr))
                    return mcp::ToolResult::error("cannot import: " + err);
            } else if (ext == ".rbxl" || ext == ".rbxlx") {
                if (session_.modified() && !a["discard"].as_bool(false))
                    return mcp::ToolResult::error("the place has unsaved changes: save it or pass discard=true");
                if (session_.play_state() != PlayState::Edit) session_.stop();
                auto dm = std::make_unique<world::DataModel>();
                if (!rbx::import_place(path.c_str(), *dm, opt, &rep, &err))
                    return mcp::ToolResult::error("cannot import: " + err);
                const std::string place = projects_.create_project(path_stem(path));
                session_.adopt(std::move(dm), place);
                if (session_.save()) projects_.touch_recent(place);
                screen_ = Screen::Editor;
                projects_dirty_ = true;
            } else {
                return mcp::ToolResult::error("expected .rbxl, .rbxlx, .rbxm or .rbxmx");
            }
            std::string text = rbx::report_summary(rep);
            for (usize i = 0; i < rep.warnings.size() && i < 30; ++i) text += "\n- " + rep.warnings[i];
            return mcp::ToolResult::text(text);
        });
    mcp_->add_tool(
        "save_project",
        "Saves the open place in Studio (to its project file, or to path when given) and clears the unsaved flag.",
        R"({"type":"object","properties":{"path":{"type":"string"}}})", [this](const JsonValue& a) {
            const std::string p = a["path"].as_string();
            if (p.empty() && session_.path().empty())
                return mcp::ToolResult::error("the place is untitled: pass a path ending in .tefra");
            if (!session_.save(p)) return mcp::ToolResult::error("cannot save");
            projects_.touch_recent(session_.path());
            return mcp::ToolResult::text("saved " + session_.path());
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
