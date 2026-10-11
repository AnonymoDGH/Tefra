#pragma once

// Tefra Studio application: window + Vulkan + Dear ImGui shell around the engine renderer (spec M3, ADR-011).
// The 3D scene is rendered to an offscreen image shown in the Viewport panel; the panels are drawn in ui.cpp.

#include "tefra/studio/projects.h"
#include "tefra/studio/session.h"
#include "tefra/studio/templates.h"

#include "tefra/core/input.h"
#include "tefra/core/log.h"

#include "tefra/renderer/camera.h"
#include "tefra/renderer/renderer.h"
#include "tefra/rhi/resources.h"

#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <volk.h>

struct ImFont;

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
namespace tefra::scripting {
class ScriptHost;
}
namespace tefra::runtime {
class GameRuntime;
}
namespace tefra::ui {
class GameGui;
}
namespace tefra::mcp {
class Server;
}
namespace tefra {
struct ImageRGBA8;
}

namespace tefra::studio {

struct StudioConfig {
    u32 width = 1600, height = 900;
    bool validation = false;
    i32 gpu_index = -1;       // -1 = auto; --gpu N or TEFRA_GPU=N picks a Vulkan device by index (see the log)
    std::string open_path;    // .tefra to open at start
    bool demo = false;        // build the demo scene instead of the baseplate
    std::string capture_path; // non-interactive: render `frames` frames, save the window as PNG and quit
    u32 frames = 0;
    bool play_at_start = false;    // press Play after loading (with --capture: screenshots of play mode)
    u16 mcp_port = 44755;          // 0 = MCP server off
    std::string start_template;    // open a new untitled place from this template ("obby"); skips the start page
    bool capture_viewport = false; // --capture writes the 3D viewport instead of the whole window
    bool show_home = true;         // start page at launch (false when a place, template or demo is given)
    // Player mode (tefra_player): the game fills the window, no editor UI, Esc opens the pause menu.
    bool player = false;
    std::string title = "Tefra Studio";
    bool fullscreen = false;
};

struct LogLine {
    LogLevel level;
    f64 time;
    std::string text;
};

enum class Tool : u8 { Select, Move, Rotate, Scale };
enum class Screen : u8 { Home, Editor, Player };
enum class HomeTab : u8 { Home, Experiences, Templates, Archive };

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
    void sync_environment(); // environment.cpp: Terrain / Ocean of the Workspace (world::EnvironmentSync)
    void frame(f32 dt, bool capture_window);

    // ---- Play mode systems (play.cpp) ----
    void setup_play_systems();
    bool play_start(world::DataModel& dm);
    void play_step(world::DataModel& dm, f32 dt);
    void play_stop();
    void play_input(vec2 view_min, vec2 view_size);
    void setup_mcp();
    bool capture_viewport_now(ImageRGBA8& out);

    // ---- Start page and projects (home.cpp) ----
    void draw_home();
    void draw_home_sidebar(f32 height);
    void draw_home_content();
    void draw_project_grid(const std::vector<ProjectInfo>& items, bool archived, usize max_items);
    bool draw_project_card(const ProjectInfo& p, bool archived);
    void draw_template_grid(bool with_descriptions, const char* category);
    void draw_home_modals();
    void refresh_projects();
    void new_from_template(const TemplateInfo& t, bool create_project);
    void open_place(const std::string& path);
    void open_place_dialog();
    void import_roblox_dialog(); // import.cpp
    bool import_roblox_file(const std::string& path);
    void export_game_dialog(); // export.cpp
    void draw_export_modal();
    void save_place(bool save_as);
    void save_thumbnail();
    void go_home();
    void confirm_discard(std::function<void()> action); // runs now, or after the unsaved-changes modal
    void frame_camera_for_template(std::string_view id);
    void update_window_title();
    void load_fonts();
    std::string resources_dir() const;
    // Image files as ImGui textures (thumbnails); reloaded when the file changes.
    void* texture(const std::string& path, vec2* size = nullptr);
    void release_textures();
    // ---- Player mode (home.cpp) ----
    void draw_player(f32 dt);

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
    std::shared_ptr<void> environment_; // world::EnvironmentSync (type-erased: no world/renderer types here)
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
    bool show_open_dialog_ = false, show_save_dialog_ = false, show_import_dialog_ = false;
    char path_buf_[512] = {};
    bool quit_ = false;
    bool layout_built_ = false;

    // Start page.
    Screen screen_ = Screen::Editor;
    HomeTab home_tab_ = HomeTab::Home;
    ProjectManager projects_;
    std::vector<ProjectInfo> recent_, all_projects_, archived_;
    bool projects_dirty_ = true;
    char search_[96] = {};
    int sort_mode_ = 0; // 0 = last modified, 1 = name
    ProjectInfo modal_project_;
    bool open_rename_ = false, open_delete_ = false, open_discard_ = false, open_error_ = false;
    char rename_project_buf_[128] = {};
    std::function<void()> discard_action_;
    std::string error_text_;
    struct UiTexture {
        rhi::Image image;
        VkDescriptorSet set = VK_NULL_HANDLE;
        vec2 size{};
        i64 stamp = 0;
        bool failed = false;
    };
    std::unordered_map<std::string, UiTexture> textures_;
    ImFont* font_regular_ = nullptr;
    ImFont* font_bold_ = nullptr;
    ImFont* font_mono_ = nullptr;
    std::string window_title_;
    bool pause_menu_ = false;     // player mode
    int thumbnail_countdown_ = 0; // frames until an automatic thumbnail of a new project

    std::vector<std::function<void(f32)>> frame_callbacks_;
    // Play mode.
    std::unique_ptr<scripting::ScriptHost> scripts_;
    bool scripts_available_ = false;
    std::unique_ptr<runtime::GameRuntime> runtime_;
    std::unique_ptr<ui::GameGui> game_gui_;
    InputState play_input_;
    render::Camera edit_camera_;
    // MCP.
    std::unique_ptr<mcp::Server> mcp_;
    VkCommandPool oneshot_pool_ = VK_NULL_HANDLE;
    std::string pending_capture_path_;
    std::function<void(bool)> pending_capture_done_;
};

} // namespace tefra::studio
