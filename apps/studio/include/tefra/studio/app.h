#pragma once

// Tefra Studio application: window + Vulkan + Dear ImGui shell around the engine renderer (spec M3, ADR-011).
// The 3D scene is rendered to an offscreen image shown in the Viewport panel; the panels are drawn in ui.cpp.

#include "tefra/studio/session.h"

#include "tefra/core/log.h"

#include "tefra/renderer/camera.h"
#include "tefra/renderer/renderer.h"
#include "tefra/rhi/resources.h"

#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <volk.h>

namespace tefra::platform {
class Window;
}
namespace tefra::rhi {
class Device;
class Swapchain;
class FrameLoop;
class BindlessHeap;
class GpuTimer;
} // namespace tefra::rhi
namespace tefra::render {
class Scene;
class Environment;
class Atmosphere;
class Renderer;
} // namespace tefra::render
namespace tefra::world {
class SceneSync;
}

namespace tefra::studio {

struct StudioConfig {
    u32 width = 1600, height = 900;
    bool validation = false;
    std::string open_path;    // .tefra to open at start
    bool demo = false;        // build the demo scene instead of the baseplate
    std::string capture_path; // non-interactive: render `frames` frames, save the window as PNG and quit
    u32 frames = 0;
};

struct LogLine {
    LogLevel level;
    f64 time;
    std::string text;
};

enum class Tool : u8 { Select, Move, Rotate, Scale };

class App {
  public:
    explicit App(const StudioConfig& cfg);
    ~App();
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    bool init(std::string* error);
    int run();

    Session& session() { return session_; }
    // Extension points for engine modules (scripting, physics, MCP): called every frame on the main thread.
    void add_frame_callback(std::function<void(f32 dt)> fn) { frame_callbacks_.push_back(std::move(fn)); }
    // Renders the viewport to a PNG on the next frame (MCP capture tool); the callback receives the PNG path.
    void request_viewport_capture(std::string path, std::function<void(bool ok)> done);
    std::vector<LogLine> log_tail(usize max_lines) const;

  private:
    // ---- Platform / GPU ----
    bool create_ui_pass();
    void destroy_ui_targets();
    void create_ui_framebuffers();
    void ensure_viewport_target(u32 w, u32 h);
    void destroy_viewport_target();
    void init_imgui();
    void apply_theme();
    void update_lighting(bool force);
    void sync_world();
    void frame(f32 dt, bool capture_window);

    // ---- UI (ui.cpp) ----
    void draw_ui(f32 dt);
    void draw_menu_bar();
    void draw_toolbar();
    void draw_explorer();
    void draw_explorer_node(world::Instance& inst, int depth);
    void draw_properties();
    void draw_viewport(f32 dt);
    void draw_output();
    void draw_script_editors();
    void draw_insert_menu(world::Instance* parent);
    void draw_context_menu(world::Instance& inst);
    void draw_file_dialogs();
    void handle_shortcuts();
    void focus_selection();
    void build_demo_scene();
    void open_script(u64 id);

    StudioConfig cfg_;
    Session session_;
    std::unique_ptr<platform::Window> window_;
    std::unique_ptr<rhi::Device> device_;
    std::unique_ptr<rhi::Swapchain> swapchain_;
    std::unique_ptr<rhi::FrameLoop> frames_;
    std::unique_ptr<rhi::BindlessHeap> heap_;
    std::unique_ptr<render::Scene> scene_;
    std::unique_ptr<render::Environment> env_;
    std::unique_ptr<render::Atmosphere> atmosphere_;
    std::unique_ptr<render::Renderer> renderer_;
    std::unique_ptr<world::SceneSync> sync_;
    u64 synced_generation_ = ~0ull;

    VkRenderPass ui_pass_ = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> ui_framebuffers_;
    VkExtent2D ui_extent_{};

    rhi::Image viewport_image_;
    VkDescriptorSet viewport_tex_ = VK_NULL_HANDLE;
    VkExtent2D viewport_extent_{};
    VkExtent2D viewport_wanted_{960, 540};
    bool viewport_hovered_ = false;
    f32 viewport_x_ = 0, viewport_y_ = 0; // screen position of the image (for picking)

    render::Camera camera_;
    render::RenderSettings settings_;
    u64 frame_index_ = 0;
    f64 time_ = 0.0;
    Tool tool_ = Tool::Move;
    bool gizmo_local_ = false;
    bool gizmo_active_ = false;
    f32 move_snap_ = 0.5f, rotate_snap_ = 15.0f;
    bool snap_ = true;

    // Lighting state last applied to the environment.
    f32 applied_clock_ = -1.0f, applied_lat_ = -1000.0f, applied_az_ = -1000.0f;

    // Output panel.
    mutable std::mutex log_mutex_;
    std::deque<LogLine> log_;
    bool log_autoscroll_ = true;
    char log_filter_[64] = {};

    // Explorer rename / script tabs / dialogs.
    u64 renaming_ = 0;
    char rename_buf_[128] = {};
    std::vector<u64> open_scripts_;
    u64 focus_script_ = 0;
    bool show_open_dialog_ = false, show_save_dialog_ = false;
    char path_buf_[512] = {};
    bool quit_ = false;
    bool layout_built_ = false;

    std::vector<std::function<void(f32)>> frame_callbacks_;
    std::string pending_capture_path_;
    std::function<void(bool)> pending_capture_done_;
};

} // namespace tefra::studio
