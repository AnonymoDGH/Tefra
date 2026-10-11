// Tefra Studio start page (spec M3b §1): sidebar (New, Home, My Experiences, Templates, Archive), recent projects
// with thumbnails and a "..." menu, templates, the unsaved-changes / rename / delete modals, the project file
// commands (open, save, thumbnails) and the player-mode screen used by tefra_player.
#include "tefra/studio/app.h"

#include "file_dialog.h"

#include "tefra/core/fs.h"
#include "tefra/core/image.h"
#include "tefra/core/log.h"
#include "tefra/platform/window.h"
#include "tefra/rhi/device.h"
#include "tefra/rhi/frame.h"
#include "tefra/rhi/upload.h"
#include "tefra/runtime/game_runtime.h"
#include "tefra/ui/game_gui.h"

#include <imgui.h>
#include <imgui_impl_vulkan.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

#ifndef TEFRA_SOURCE_DIR
#define TEFRA_SOURCE_DIR "."
#endif

namespace tefra::studio {

namespace {

// Colours are given in sRGB and converted for the sRGB swapchain (ImGui blends in linear space there).
ImU32 srgb(int r, int g, int b, int a = 255) {
    auto lin = [](int c) { return static_cast<int>(std::pow(static_cast<f32>(c) / 255.0f, 2.2f) * 255.0f + 0.5f); };
    return IM_COL32(lin(r), lin(g), lin(b), a);
}
ImVec4 srgb4(int r, int g, int b, int a = 255) { return ImGui::ColorConvertU32ToFloat4(srgb(r, g, b, a)); }

constexpr f32 kSidebarW = 236.0f;
constexpr f32 kCardW = 232.0f;
constexpr f32 kCardGap = 20.0f;

ImU32 c_bg() { return srgb(30, 31, 34); }
ImU32 c_sidebar() { return srgb(20, 21, 24); }
ImU32 c_card() { return srgb(46, 48, 53); }
ImU32 c_card_hover() { return srgb(58, 61, 68); }
ImU32 c_accent() { return srgb(51, 133, 255); }
ImU32 c_text() { return srgb(232, 234, 237); }
ImU32 c_text_dim() { return srgb(150, 155, 163); }

// ---- Line icons drawn with the draw list (no icon font) -----------------------------------------------------
void icon_home(ImDrawList* dl, ImVec2 c, f32 s, ImU32 col) {
    const f32 h = s * 0.5f;
    dl->AddLine({c.x - h, c.y - 0.05f * s}, {c.x, c.y - h}, col, 1.6f);
    dl->AddLine({c.x, c.y - h}, {c.x + h, c.y - 0.05f * s}, col, 1.6f);
    dl->AddRect({c.x - h * 0.7f, c.y - 0.1f * s}, {c.x + h * 0.7f, c.y + h}, col, 1.0f, 0, 1.6f);
}
void icon_grid(ImDrawList* dl, ImVec2 c, f32 s, ImU32 col) {
    const f32 h = s * 0.45f, g = s * 0.08f;
    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 2; ++j) {
            const f32 x0 = c.x - h + f32(i) * (h + g * 0.5f), y0 = c.y - h + f32(j) * (h + g * 0.5f);
            dl->AddRect({x0, y0}, {x0 + h - g * 0.5f, y0 + h - g * 0.5f}, col, 2.0f, 0, 1.5f);
        }
}
void icon_layers(ImDrawList* dl, ImVec2 c, f32 s, ImU32 col) {
    const f32 h = s * 0.5f;
    for (int i = 0; i < 2; ++i) {
        const f32 y = c.y - h * 0.35f + f32(i) * h * 0.55f;
        const ImVec2 p[4] = {{c.x - h, y}, {c.x, y - h * 0.45f}, {c.x + h, y}, {c.x, y + h * 0.45f}};
        dl->AddPolyline(p, 4, col, ImDrawFlags_Closed, 1.5f);
    }
}
void icon_box(ImDrawList* dl, ImVec2 c, f32 s, ImU32 col) {
    const f32 h = s * 0.5f;
    dl->AddRect({c.x - h, c.y - h * 0.7f}, {c.x + h, c.y - h * 0.25f}, col, 1.5f, 0, 1.5f);
    dl->AddRect({c.x - h * 0.85f, c.y - h * 0.25f}, {c.x + h * 0.85f, c.y + h * 0.8f}, col, 1.5f, 0, 1.5f);
    dl->AddLine({c.x - h * 0.3f, c.y + h * 0.1f}, {c.x + h * 0.3f, c.y + h * 0.1f}, col, 1.5f);
}
void icon_folder(ImDrawList* dl, ImVec2 c, f32 s, ImU32 col) {
    const f32 h = s * 0.5f;
    const ImVec2 p[6] = {{c.x - h, c.y + h * 0.7f}, {c.x - h, c.y - h * 0.7f}, {c.x - h * 0.2f, c.y - h * 0.7f},
                         {c.x, c.y - h * 0.4f},     {c.x + h, c.y - h * 0.4f}, {c.x + h, c.y + h * 0.7f}};
    dl->AddPolyline(p, 6, col, ImDrawFlags_Closed, 1.5f);
}
void icon_import(ImDrawList* dl, ImVec2 c, f32 s, ImU32 col) {
    const f32 h = s * 0.5f;
    dl->AddLine({c.x, c.y - h}, {c.x, c.y + h * 0.35f}, col, 1.6f);
    dl->AddLine({c.x - h * 0.45f, c.y - h * 0.1f}, {c.x, c.y + h * 0.35f}, col, 1.6f);
    dl->AddLine({c.x + h * 0.45f, c.y - h * 0.1f}, {c.x, c.y + h * 0.35f}, col, 1.6f);
    dl->AddLine({c.x - h, c.y + h * 0.8f}, {c.x + h, c.y + h * 0.8f}, col, 1.6f);
}
void icon_plus(ImDrawList* dl, ImVec2 c, f32 s, ImU32 col) {
    const f32 h = s * 0.4f;
    dl->AddLine({c.x - h, c.y}, {c.x + h, c.y}, col, 2.0f);
    dl->AddLine({c.x, c.y - h}, {c.x, c.y + h}, col, 2.0f);
}

// Tefra logo: a rounded tile with a stylised "T" (volcanic ash cone).
void draw_logo(ImDrawList* dl, ImVec2 p, f32 s) {
    dl->AddRectFilledMultiColor(p, {p.x + s, p.y + s}, srgb(255, 120, 60), srgb(240, 70, 70), srgb(150, 50, 160),
                                srgb(255, 90, 50));
    dl->AddRect(p, {p.x + s, p.y + s}, srgb(0, 0, 0, 60), s * 0.22f, 0, 1.0f);
    const ImU32 w = srgb(255, 255, 255);
    dl->AddRectFilled({p.x + s * 0.22f, p.y + s * 0.24f}, {p.x + s * 0.78f, p.y + s * 0.38f}, w, 2.0f);
    dl->AddRectFilled({p.x + s * 0.43f, p.y + s * 0.24f}, {p.x + s * 0.57f, p.y + s * 0.78f}, w, 2.0f);
}

// Placeholder when a project has no thumbnail yet: a soft sky gradient with the project's initial.
void draw_placeholder(ImDrawList* dl, ImVec2 a, ImVec2 b, const std::string& name, ImFont* font, u32 seed) {
    const int hue = static_cast<int>(seed % 6u);
    static const int top[6][3] = {{70, 120, 190}, {60, 140, 140}, {120, 90, 170},
                                  {170, 110, 70}, {80, 130, 90},  {150, 80, 110}};
    const ImU32 t = srgb(top[hue][0], top[hue][1], top[hue][2]);
    const ImU32 d = srgb(top[hue][0] / 3, top[hue][1] / 3, top[hue][2] / 3);
    dl->AddRectFilledMultiColor(a, b, t, t, d, d);
    char initial[2] = {name.empty() ? '?' : static_cast<char>(std::toupper(static_cast<unsigned char>(name[0]))), 0};
    const f32 size = (b.y - a.y) * 0.45f;
    const ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, initial);
    dl->AddText(font, size, {(a.x + b.x - ts.x) * 0.5f, (a.y + b.y - ts.y) * 0.5f}, srgb(255, 255, 255, 200), initial);
}

u32 hash_str(const std::string& s) {
    u32 h = 2166136261u;
    for (unsigned char c : s) h = (h ^ c) * 16777619u;
    return h;
}

// Text clipped to `width` with an ellipsis.
std::string ellipsize(const std::string& s, f32 width) {
    if (ImGui::CalcTextSize(s.c_str()).x <= width) return s;
    std::string out = s;
    while (!out.empty() && ImGui::CalcTextSize((out + "...").c_str()).x > width) out.pop_back();
    return out + "...";
}

bool lower_contains(const std::string& hay, const char* needle) {
    if (!needle[0]) return true;
    std::string h = hay, n = needle;
    for (char& c : h) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (char& c : n) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return h.find(n) != std::string::npos;
}

const std::vector<FileFilter> kPlaceFilter = {{"Tefra places", "*.tefra"}};

// Downscales (box filter) and centre-crops to w x h.
ImageRGBA8 make_thumbnail(const ImageRGBA8& src, u32 w, u32 h) {
    ImageRGBA8 out;
    out.width = w;
    out.height = h;
    out.pixels.assign(usize(w) * h * 4, 255);
    if (src.width == 0 || src.height == 0) return out;
    // Crop the source to the target aspect.
    f32 cw = f32(src.width), ch = f32(src.width) * f32(h) / f32(w);
    if (ch > f32(src.height)) {
        ch = f32(src.height);
        cw = ch * f32(w) / f32(h);
    }
    const f32 x0 = (f32(src.width) - cw) * 0.5f, y0 = (f32(src.height) - ch) * 0.5f;
    const f32 sx = cw / f32(w), sy = ch / f32(h);
    for (u32 y = 0; y < h; ++y)
        for (u32 x = 0; x < w; ++x) {
            const u32 ax = u32(x0 + f32(x) * sx), bx = max(ax + 1, u32(x0 + f32(x + 1) * sx));
            const u32 ay = u32(y0 + f32(y) * sy), by = max(ay + 1, u32(y0 + f32(y + 1) * sy));
            u32 acc[3] = {0, 0, 0}, n = 0;
            for (u32 yy = ay; yy < by && yy < src.height; ++yy)
                for (u32 xx = ax; xx < bx && xx < src.width; ++xx) {
                    const u8* p = &src.pixels[(usize(yy) * src.width + xx) * 4];
                    acc[0] += p[0];
                    acc[1] += p[1];
                    acc[2] += p[2];
                    ++n;
                }
            u8* d = &out.pixels[(usize(y) * w + x) * 4];
            for (int c = 0; c < 3; ++c) d[c] = static_cast<u8>(n ? acc[c] / n : 0);
            d[3] = 255;
        }
    return out;
}

} // namespace

// ---- Resources, fonts, textures ---------------------------------------------------------------------------------

std::string App::resources_dir() const {
    const std::string next_to_exe = path_join(exe_dir(), "resources");
    if (is_dir(path_join(next_to_exe, "fonts"))) return next_to_exe;
    return path_join(TEFRA_SOURCE_DIR, "apps/studio/resources");
}

void App::load_fonts() {
    ImGuiIO& io = ImGui::GetIO();
    const std::string fonts = path_join(resources_dir(), "fonts");
    auto load = [&](const char* file, f32 size) -> ImFont* {
        const std::string p = path_join(fonts, file);
        if (!file_exists(p.c_str())) return nullptr;
        ImFontConfig cfg;
        cfg.OversampleH = 2;
        cfg.PixelSnapH = false;
        return io.Fonts->AddFontFromFileTTF(p.c_str(), size, &cfg);
    };
    font_regular_ = load("Inter-Regular.ttf", 16.0f);
    font_bold_ = load("Inter-SemiBold.ttf", 16.0f);
    font_mono_ = load("JetBrainsMono-Regular.ttf", 15.0f);
    if (!font_regular_) {
        TEFRA_LOG_WARN("Studio: fonts not found in %s, using the built-in font", fonts.c_str());
        font_regular_ = io.Fonts->AddFontDefault();
    }
    if (!font_bold_) font_bold_ = font_regular_;
    if (!font_mono_) font_mono_ = font_regular_;
    io.FontDefault = font_regular_;
}

void* App::texture(const std::string& path, vec2* size) {
    if (path.empty()) return nullptr;
    const i64 stamp = file_mtime(path);
    auto it = textures_.find(path);
    if (it != textures_.end() && it->second.stamp == stamp) {
        if (size) *size = it->second.size;
        return it->second.failed ? nullptr : reinterpret_cast<void*>(it->second.set);
    }
    if (it != textures_.end()) { // the file changed: drop the old image (rare, so wait for the GPU)
        frames_->wait_all();
        if (it->second.set) ImGui_ImplVulkan_RemoveTexture(it->second.set);
        if (it->second.image.image) rhi::destroy_image(*device_, it->second.image);
        textures_.erase(it);
    }
    UiTexture t;
    t.stamp = stamp;
    ImageRGBA8 img;
    if (stamp == 0 || !image_read(path.c_str(), img)) {
        t.failed = true;
        textures_.emplace(path, t);
        return nullptr;
    }
    rhi::TextureUpload up;
    up.pixels = img.pixels.data();
    up.width = img.width;
    up.height = img.height;
    up.format = VK_FORMAT_R8G8B8A8_SRGB;
    up.generate_mips = false;
    up.name = "ui_texture";
    t.image = rhi::upload_texture(*device_, up);
    t.set = ImGui_ImplVulkan_AddTexture(VK_NULL_HANDLE, t.image.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    t.size = {f32(img.width), f32(img.height)};
    if (size) *size = t.size;
    void* id = reinterpret_cast<void*>(t.set);
    textures_.emplace(path, std::move(t));
    return id;
}

void App::release_textures() {
    if (textures_.empty()) return;
    if (frames_) frames_->wait_all();
    for (auto& [path, t] : textures_) {
        if (t.set && ImGui::GetCurrentContext()) ImGui_ImplVulkan_RemoveTexture(t.set);
        if (t.image.image) rhi::destroy_image(*device_, t.image);
    }
    textures_.clear();
}

// ---- Project commands ---------------------------------------------------------------------------------------

void App::refresh_projects() {
    const std::vector<ProjectInfo> opened = projects_.recent(24);
    all_projects_ = projects_.projects();
    // Loose files from the recent list that are not project folders also count as experiences.
    for (const ProjectInfo& r : opened)
        if (r.folder.empty()) all_projects_.push_back(r);
    // Home shows the most recently opened or edited ones.
    recent_ = all_projects_;
    std::stable_sort(recent_.begin(), recent_.end(), [](const ProjectInfo& a, const ProjectInfo& b) {
        return max(a.opened, a.modified) > max(b.opened, b.modified);
    });
    archived_ = projects_.archived();
    projects_dirty_ = false;
}

void App::update_window_title() {
    std::string t;
    if (cfg_.player)
        t = cfg_.title;
    else if (screen_ == Screen::Home)
        t = "Tefra Studio";
    else {
        t = session_.path().empty() ? std::string("Untitled") : path_stem(session_.path());
        if (session_.modified()) t += " *";
        t += "  -  Tefra Studio";
    }
    if (t != window_title_) {
        window_title_ = t;
        window_->set_title(t.c_str());
    }
}

void App::confirm_discard(std::function<void()> action) {
    if (screen_ == Screen::Editor && session_.modified() && session_.play_state() == PlayState::Edit) {
        discard_action_ = std::move(action);
        open_discard_ = true;
        return;
    }
    if (session_.play_state() != PlayState::Edit) session_.stop();
    action();
}

void App::go_home() {
    confirm_discard([this] {
        session_.new_place(false); // nothing is left "modified" behind the start page
        screen_ = Screen::Home;
        projects_dirty_ = true;
    });
}

void App::frame_camera_for_template(std::string_view id) {
    auto look = [&](vec3 pos, vec3 at) {
        const vec3 d = normalize(at - pos);
        camera_.position = {f64(pos.x), f64(pos.y), f64(pos.z)};
        camera_.yaw = std::atan2(-d.x, -d.z);
        camera_.pitch = std::asin(clamp(d.y, -1.0f, 1.0f));
    };
    if (id == "obby")
        look({48, 30, -40}, {0, 4, -110});
    else if (id == "flat_terrain")
        look({20, 9, 26}, {0, 1, 0});
    else if (id == "beach" || id == "valley" || id == "meadow") {
        // Behind the spawn, looking the way it faces.
        for (world::Instance* i : session_.model().workspace()->children())
            if (i->is_a("SpawnLocation")) {
                const vec3 p = i->get_vec3("Position");
                const f32 yaw = radians(i->get_vec3("Orientation").y);
                const vec3 f{-std::sin(yaw), 0, -std::cos(yaw)};
                const f32 up = id == "meadow" ? 2.6f : 8.0f;
                look(p - f * (id == "meadow" ? 16.0f : 18.0f) + vec3{0, up, 0}, p + f * 40.0f + vec3{0, 1, 0});
                return;
            }
        look({0, 40, 60}, {0, 10, 0});
    } else
        look({18, 12, 22}, {0, 0, 0});
}

void App::new_from_template(const TemplateInfo& t, bool create_project) {
    const std::string id = t.id, name = t.name;
    confirm_discard([this, id, name, create_project] {
        auto m = std::make_unique<world::DataModel>();
        build_template(id, *m);
        std::string path;
        if (create_project) path = projects_.create_project(name);
        session_.adopt(std::move(m), path);
        if (!path.empty() && session_.save()) projects_.touch_recent(path);
        screen_ = Screen::Editor;
        frame_camera_for_template(id);
        thumbnail_countdown_ = path.empty() ? 0 : 45; // after TAA and the sky settle
        projects_dirty_ = true;
        TEFRA_LOG_INFO("New place from the %s template%s%s", name.c_str(), path.empty() ? "" : ": ", path.c_str());
    });
}

void App::open_place(const std::string& path) {
    confirm_discard([this, path] {
        std::string err;
        if (!session_.open(path, &err)) {
            error_text_ = "Cannot open " + path + "\n" + err;
            open_error_ = true;
            return;
        }
        projects_.touch_recent(path);
        screen_ = Screen::Editor;
        projects_dirty_ = true;
    });
}

void App::open_place_dialog() {
    std::string path;
    const DialogStatus st = dialog_open_file("Open place", kPlaceFilter, projects_.projects_dir(), path);
    if (st == DialogStatus::Ok)
        open_place(path);
    else if (st == DialogStatus::Unavailable)
        show_open_dialog_ = true;
}

void App::save_place(bool save_as) {
    if (session_.play_state() != PlayState::Edit) return;
    std::string path = session_.path();
    if (save_as || path.empty()) {
        const std::string def = path.empty() ? std::string("My Game.tefra") : path_filename(path);
        const std::string dir = path.empty() ? projects_.projects_dir() : path_parent(path);
        make_dirs(dir.c_str());
        const DialogStatus st = dialog_save_file("Save place as", kPlaceFilter, dir, def, path);
        if (st == DialogStatus::Unavailable) {
            show_save_dialog_ = true;
            return;
        }
        if (st != DialogStatus::Ok) return;
        if (path_extension(path).empty()) path += ".tefra";
    }
    if (!session_.save(path)) {
        error_text_ = "Cannot save " + path;
        open_error_ = true;
        return;
    }
    projects_.touch_recent(path);
    save_thumbnail();
    projects_dirty_ = true;
}

void App::save_thumbnail() {
    if (session_.path().empty() || !viewport_image_.image) return;
    ImageRGBA8 img;
    if (!capture_viewport_now(img)) return;
    const std::string thumb = projects_.thumbnail_for(session_.path());
    make_dirs(path_parent(thumb).c_str());
    if (!image_write_png(thumb.c_str(), make_thumbnail(img, 480, 270)))
        TEFRA_LOG_WARN("Cannot write the thumbnail %s", thumb.c_str());
}

// ---- Start page -------------------------------------------------------------------------------------------------

void App::draw_home() {
    if (projects_dirty_) refresh_projects();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::ColorConvertU32ToFloat4(c_bg()));
    ImGui::Begin("##home", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    draw_home_sidebar(vp->Size.y);
    ImGui::SameLine(0, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {36, 28});
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::BeginChild("##content", {0, 0}, ImGuiChildFlags_AlwaysUseWindowPadding);
    draw_home_content();
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    ImGui::End();
    draw_file_dialogs();
    draw_home_modals();
}

void App::draw_home_sidebar(f32 height) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 o = ImGui::GetCursorScreenPos();
    dl->AddRectFilled(o, {o.x + kSidebarW, o.y + height}, c_sidebar());
    ImGui::BeginChild("##sidebar", {kSidebarW, height}, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    ImDrawList* sl = ImGui::GetWindowDrawList();
    // Logo.
    const ImVec2 lp{o.x + 20, o.y + 20};
    draw_logo(sl, lp, 30);
    sl->AddText(font_bold_, 19.0f, {lp.x + 42, lp.y + 5}, c_text(), "Tefra Studio");
    ImGui::SetCursorPos({16, 74});

    // "+ New" button.
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const ImVec2 sz{kSidebarW - 32, 42};
        if (ImGui::InvisibleButton("##new", sz)) home_tab_ = HomeTab::Templates;
        const bool hov = ImGui::IsItemHovered();
        sl->AddRectFilled(p, {p.x + sz.x, p.y + sz.y}, hov ? srgb(80, 152, 255) : c_accent(), 8.0f);
        icon_plus(sl, {p.x + 24, p.y + sz.y * 0.5f}, 16, srgb(255, 255, 255));
        sl->AddText(font_bold_, 16.0f, {p.x + 42, p.y + 12}, srgb(255, 255, 255), "New Experience");
    }
    ImGui::Dummy({0, 14});

    struct Nav {
        const char* label;
        HomeTab tab;
        void (*icon)(ImDrawList*, ImVec2, f32, ImU32);
    };
    const Nav nav[] = {{"Home", HomeTab::Home, icon_home},
                       {"My Experiences", HomeTab::Experiences, icon_grid},
                       {"Templates", HomeTab::Templates, icon_layers},
                       {"Archive", HomeTab::Archive, icon_box}};
    auto item = [&](const char* label, bool selected, void (*icon)(ImDrawList*, ImVec2, f32, ImU32)) {
        ImGui::SetCursorPosX(10);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const ImVec2 sz{kSidebarW - 20, 38};
        const bool clicked = ImGui::InvisibleButton(label, sz);
        const bool hov = ImGui::IsItemHovered();
        if (selected || hov)
            sl->AddRectFilled(p, {p.x + sz.x, p.y + sz.y}, selected ? srgb(44, 47, 54) : srgb(34, 36, 41), 6.0f);
        if (selected) sl->AddRectFilled({p.x, p.y + 8}, {p.x + 3, p.y + sz.y - 8}, c_accent(), 2.0f);
        const ImU32 col = selected ? c_text() : c_text_dim();
        icon(sl, {p.x + 22, p.y + sz.y * 0.5f}, 16, selected ? c_accent() : col);
        sl->AddText(selected ? font_bold_ : font_regular_, 15.5f, {p.x + 44, p.y + 10}, col, label);
        return clicked;
    };
    for (const Nav& n : nav)
        if (item(n.label, home_tab_ == n.tab, n.icon)) home_tab_ = n.tab;
    ImGui::Dummy({0, 6});
    ImGui::SetCursorPosX(20);
    const ImVec2 sep = ImGui::GetCursorScreenPos();
    sl->AddLine(sep, {sep.x + kSidebarW - 40, sep.y}, srgb(50, 52, 58));
    ImGui::Dummy({0, 10});
    if (item("Open File...", false, icon_folder)) open_place_dialog();
    if (item("Import Roblox Place...", false, icon_import)) import_roblox_dialog();

    // Footer.
    ImGui::SetCursorPos({20, height - 58});
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(c_text_dim()));
    if (ImGui::SmallButton("Projects folder")) {
        make_dirs(projects_.projects_dir().c_str());
        reveal_in_file_manager(projects_.projects_dir());
    }
    ImGui::SetCursorPosX(20);
    ImGui::Text("Tefra %s", TEFRA_VERSION);
    ImGui::PopStyleColor();
    ImGui::EndChild();
}

void App::draw_home_content() {
    auto title = [&](const char* t) {
        ImGui::PushFont(font_bold_, 30.0f);
        ImGui::TextUnformatted(t);
        ImGui::PopFont();
        ImGui::Dummy({0, 6});
    };
    auto section = [&](const char* t, const char* link, HomeTab target) {
        ImGui::Dummy({0, 8});
        ImGui::PushFont(font_bold_, 19.0f);
        ImGui::TextUnformatted(t);
        ImGui::PopFont();
        if (link) {
            ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(link).x - 4);
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(c_accent()));
            ImGui::TextUnformatted(link);
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            if (ImGui::IsItemClicked()) home_tab_ = target;
        }
        ImGui::Dummy({0, 4});
    };

    switch (home_tab_) {
    case HomeTab::Home:
        title("Home");
        section("Recent Experiences", "See all", HomeTab::Experiences);
        if (recent_.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(c_text_dim()));
            ImGui::TextUnformatted("No experiences yet. Start one from a template below: it is saved in your "
                                   "Projects folder automatically.");
            ImGui::PopStyleColor();
        } else {
            draw_project_grid(recent_, false, 12);
        }
        ImGui::Dummy({0, 12});
        section("Start from a Template", "All templates", HomeTab::Templates);
        draw_template_grid(false, nullptr);
        break;
    case HomeTab::Experiences: {
        title("My Experiences");
        ImGui::SetNextItemWidth(300);
        ImGui::InputTextWithHint("##search", "Search experiences", search_, sizeof(search_));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(180);
        const char* sorts[] = {"Last modified", "Name"};
        ImGui::Combo("##sort", &sort_mode_, sorts, 2);
        ImGui::Dummy({0, 10});
        std::vector<ProjectInfo> items;
        for (const ProjectInfo& p : all_projects_)
            if (lower_contains(p.name, search_)) items.push_back(p);
        if (sort_mode_ == 1)
            std::sort(items.begin(), items.end(),
                      [](const ProjectInfo& a, const ProjectInfo& b) { return a.name < b.name; });
        if (items.empty()) {
            ImGui::TextDisabled(search_[0] ? "Nothing matches the search." : "No experiences yet.");
            ImGui::TextDisabled("Projects folder: %s", projects_.projects_dir().c_str());
        }
        draw_project_grid(items, false, items.size());
        break;
    }
    case HomeTab::Templates:
        title("Templates");
        ImGui::TextDisabled("Each template creates a new experience in your Projects folder.");
        section("Basic", nullptr, HomeTab::Templates);
        draw_template_grid(true, "Basic");
        section("Games", nullptr, HomeTab::Templates);
        draw_template_grid(true, "Game");
        section("Environments", nullptr, HomeTab::Templates);
        draw_template_grid(true, "Environment");
        break;
    case HomeTab::Archive:
        title("Archive");
        ImGui::TextDisabled("Archived experiences are kept in %s. Restore one to see it in Home again.",
                            projects_.archive_dir().c_str());
        ImGui::Dummy({0, 10});
        if (archived_.empty()) ImGui::TextDisabled("The archive is empty.");
        draw_project_grid(archived_, true, archived_.size());
        break;
    }
}

void App::draw_project_grid(const std::vector<ProjectInfo>& items, bool archived, usize max_items) {
    const f32 avail = ImGui::GetContentRegionAvail().x;
    const int cols = max(1, static_cast<int>((avail + kCardGap) / (kCardW + kCardGap)));
    const usize n = min(items.size(), max_items);
    for (usize i = 0; i < n; ++i) {
        if (i % usize(cols) != 0) ImGui::SameLine(0, kCardGap);
        if (draw_project_card(items[i], archived)) {
            if (!archived) open_place(items[i].path);
        }
    }
    if (n) ImGui::Dummy({0, 6});
}

bool App::draw_project_card(const ProjectInfo& p, bool archived) {
    ImGui::PushID(p.path.c_str());
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const f32 th = kCardW * 9.0f / 16.0f, h = th + 62.0f;
    ImGui::SetNextItemAllowOverlap();
    const bool clicked = ImGui::InvisibleButton("##card", {kCardW, h});
    const bool hov = ImGui::IsItemHovered();
    if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    const ImVec2 end{pos.x + kCardW, pos.y + h};
    dl->AddRectFilled(pos, end, hov ? c_card_hover() : c_card(), 10.0f);
    const ImVec2 ta = pos, tb{pos.x + kCardW, pos.y + th};
    if (void* tex = texture(p.thumbnail))
        dl->AddImageRounded(reinterpret_cast<ImTextureID>(tex), ta, tb, {0, 0}, {1, 1}, IM_COL32_WHITE, 10.0f,
                            ImDrawFlags_RoundCornersTop);
    else {
        dl->PushClipRect(ta, tb, true);
        draw_placeholder(dl, ta, tb, p.name, font_bold_, hash_str(p.name));
        dl->PopClipRect();
    }
    if (hov) dl->AddRect(pos, end, c_accent(), 10.0f, 0, 2.0f);
    // Badge: project folder or loose file.
    const char* badge = p.folder.empty() ? "FILE" : (archived ? "ARCHIVED" : "PROJECT");
    const ImVec2 bs = font_bold_->CalcTextSizeA(11.0f, FLT_MAX, 0.0f, badge);
    dl->AddRectFilled({ta.x + 8, ta.y + 8}, {ta.x + 18 + bs.x, ta.y + 12 + bs.y}, srgb(0, 0, 0, 150), 4.0f);
    dl->AddText(font_bold_, 11.0f, {ta.x + 13, ta.y + 10}, srgb(255, 255, 255, 230), badge);
    // Name and date.
    const std::string name = ellipsize(p.name, kCardW - 48);
    dl->AddText(font_bold_, 15.5f, {pos.x + 12, pos.y + th + 10}, c_text(), name.c_str());
    const std::string sub = "Edited " + format_age(p.modified, unix_time_now());
    dl->AddText(font_regular_, 13.5f, {pos.x + 12, pos.y + th + 33}, c_text_dim(), sub.c_str());

    // "..." menu button.
    ImGui::SetCursorScreenPos({pos.x + kCardW - 36, pos.y + th + 12});
    if (ImGui::InvisibleButton("##more", {28, 28})) ImGui::OpenPopup("##cardmenu");
    const bool mhov = ImGui::IsItemHovered();
    const ImVec2 mc{pos.x + kCardW - 22, pos.y + th + 26};
    if (mhov) dl->AddCircleFilled(mc, 14, srgb(70, 73, 80));
    for (int k = -1; k <= 1; ++k) dl->AddCircleFilled({mc.x, mc.y + f32(k) * 5.5f}, 2.0f, c_text());
    bool open_now = false;
    if (ImGui::BeginPopup("##cardmenu")) {
        std::string err;
        if (!archived) {
            if (ImGui::MenuItem("Open")) open_now = true;
            if (ImGui::MenuItem("Rename...")) {
                modal_project_ = p;
                std::snprintf(rename_project_buf_, sizeof(rename_project_buf_), "%s", p.name.c_str());
                open_rename_ = true;
            }
            if (ImGui::MenuItem("Duplicate")) {
                if (!projects_.duplicate(p, nullptr, &err)) {
                    error_text_ = err;
                    open_error_ = true;
                }
                projects_dirty_ = true;
            }
        } else if (ImGui::MenuItem("Restore")) {
            if (!projects_.set_archived(p, false, &err)) {
                error_text_ = err;
                open_error_ = true;
            }
            projects_dirty_ = true;
        }
        if (ImGui::MenuItem("Show in Folder")) reveal_in_file_manager(p.path);
        ImGui::Separator();
        if (!archived && ImGui::MenuItem(p.folder.empty() ? "Remove from Recent" : "Archive")) {
            if (!projects_.set_archived(p, true, &err)) {
                error_text_ = err;
                open_error_ = true;
            }
            projects_dirty_ = true;
        }
        ImGui::PushStyleColor(ImGuiCol_Text, srgb4(255, 110, 100));
        if (ImGui::MenuItem("Delete...")) {
            modal_project_ = p;
            open_delete_ = true;
        }
        ImGui::PopStyleColor();
        ImGui::EndPopup();
    }
    ImGui::SetCursorScreenPos(pos);
    ImGui::Dummy({kCardW, h});
    ImGui::PopID();
    return (clicked && !mhov) || open_now;
}

void App::draw_template_grid(bool with_descriptions, const char* category) {
    const f32 avail = ImGui::GetContentRegionAvail().x;
    const int cols = max(1, static_cast<int>((avail + kCardGap) / (kCardW + kCardGap)));
    const std::string tdir = path_join(resources_dir(), "templates");
    int i = 0;
    for (const TemplateInfo& t : templates()) {
        if (category && std::strcmp(category, t.category) != 0) continue;
        if (i % cols != 0) ImGui::SameLine(0, kCardGap);
        ++i;
        ImGui::PushID(t.id);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const f32 th = kCardW * 9.0f / 16.0f, h = th + (with_descriptions ? 84.0f : 44.0f);
        const bool clicked = ImGui::InvisibleButton("##tpl", {kCardW, h});
        const bool hov = ImGui::IsItemHovered();
        if (hov) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            if (!with_descriptions) ImGui::SetTooltip("%s", t.description);
        }
        const ImVec2 end{pos.x + kCardW, pos.y + h};
        dl->AddRectFilled(pos, end, hov ? c_card_hover() : c_card(), 10.0f);
        const ImVec2 tb{pos.x + kCardW, pos.y + th};
        if (void* tex = texture(path_join(tdir, std::string(t.id) + ".png")))
            dl->AddImageRounded(reinterpret_cast<ImTextureID>(tex), pos, tb, {0, 0}, {1, 1}, IM_COL32_WHITE, 10.0f,
                                ImDrawFlags_RoundCornersTop);
        else {
            dl->PushClipRect(pos, tb, true);
            draw_placeholder(dl, pos, tb, t.name, font_bold_, hash_str(t.id));
            dl->PopClipRect();
        }
        if (hov) dl->AddRect(pos, end, c_accent(), 10.0f, 0, 2.0f);
        dl->AddText(font_bold_, 15.5f, {pos.x + 12, pos.y + th + 12}, c_text(), t.name);
        if (with_descriptions) {
            ImGui::PushClipRect(pos, end, true);
            dl->AddText(font_regular_, 13.5f, {pos.x + 12, pos.y + th + 36}, c_text_dim(), t.description, nullptr,
                        kCardW - 24);
            ImGui::PopClipRect();
        }
        if (clicked) new_from_template(t, true);
        ImGui::PopID();
    }
    if (i) ImGui::Dummy({0, 6});
}

void App::draw_home_modals() {
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    auto open = [](bool& flag, const char* id) {
        if (flag) {
            ImGui::OpenPopup(id);
            flag = false;
        }
    };
    open(open_rename_, "Rename Experience");
    open(open_delete_, "Delete Experience");
    open(open_error_, "Tefra Studio##error");
    std::string err;

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, {0.5f, 0.5f});
    if (ImGui::BeginPopupModal("Rename Experience", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("New name:");
        ImGui::SetNextItemWidth(360);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        const bool enter = ImGui::InputText("##name", rename_project_buf_, sizeof(rename_project_buf_),
                                            ImGuiInputTextFlags_EnterReturnsTrue);
        if (ImGui::Button("Rename", {120, 0}) || enter) {
            std::string new_path;
            const bool was_open = session_.path() == modal_project_.path;
            if (!projects_.rename(modal_project_, rename_project_buf_, &new_path, &err)) {
                error_text_ = err;
                open_error_ = true;
            } else if (was_open) {
                session_.open(new_path);
            }
            projects_dirty_ = true;
            release_textures();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {120, 0}) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, {0.5f, 0.5f});
    if (ImGui::BeginPopupModal("Delete Experience", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Delete \"%s\" permanently?", modal_project_.name.c_str());
        ImGui::TextDisabled("%s", modal_project_.folder.empty() ? modal_project_.path.c_str()
                                                                : modal_project_.folder.c_str());
        ImGui::TextDisabled("This cannot be undone.");
        ImGui::Dummy({0, 6});
        ImGui::PushStyleColor(ImGuiCol_Button, srgb4(190, 50, 45));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, srgb4(220, 70, 60));
        if (ImGui::Button("Delete", {120, 0})) {
            if (session_.path() == modal_project_.path) session_.new_place(false);
            if (!projects_.remove(modal_project_, &err)) {
                error_text_ = err;
                open_error_ = true;
            }
            projects_dirty_ = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor(2);
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {120, 0}) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, {0.5f, 0.5f});
    if (ImGui::BeginPopupModal("Tefra Studio##error", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(error_text_.c_str());
        if (ImGui::Button("OK", {120, 0}) || ImGui::IsKeyPressed(ImGuiKey_Enter)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // Unsaved changes (editor -> home / open / new / quit).
    open(open_discard_, "Unsaved Changes");
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, {0.5f, 0.5f});
    if (ImGui::BeginPopupModal("Unsaved Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const std::string name = session_.path().empty() ? std::string("Untitled") : path_stem(session_.path());
        ImGui::Text("Save changes to \"%s\"?", name.c_str());
        ImGui::TextDisabled("Your changes will be lost if you don't save them.");
        ImGui::Dummy({0, 6});
        bool run = false;
        if (ImGui::Button("Save", {120, 0})) {
            save_place(false);
            run = !session_.modified();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Don't Save", {120, 0})) {
            run = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {120, 0}) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            discard_action_ = nullptr;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
        if (run && discard_action_) {
            auto action = std::move(discard_action_);
            discard_action_ = nullptr;
            session_.new_place(false); // the old place is dropped either way
            action();
        }
    }
}

// ---- Player mode ------------------------------------------------------------------------------------------------

void App::draw_player(f32 dt) {
    (void)dt;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("##player", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleVar(2);
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    viewport_wanted_ = {static_cast<u32>(max(avail.x, 16.0f)), static_cast<u32>(max(avail.y, 16.0f))};
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    viewport_x_ = origin.x;
    viewport_y_ = origin.y;
    if (viewport_tex_)
        ImGui::Image(reinterpret_cast<ImTextureID>(viewport_tex_), avail);
    else
        ImGui::Dummy(avail);
    viewport_hovered_ = ImGui::IsItemHovered() && !pause_menu_;
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) pause_menu_ = !pause_menu_;
    if (!pause_menu_) {
        play_input({origin.x, origin.y}, {avail.x, avail.y});
    } else {
        play_input_ = InputState{}; // the character stops while the menu is open
        if (runtime_ && game_gui_)
            if (world::Instance* gui = runtime_->player_gui())
                game_gui_->draw(ImGui::GetWindowDrawList(), {{origin.x, origin.y}, {avail.x, avail.y}}, *gui, nullptr,
                                false);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(origin, {origin.x + avail.x, origin.y + avail.y}, srgb(0, 0, 0, 140));
        const ImVec2 size{360, 360};
        const ImVec2 p{origin.x + (avail.x - size.x) * 0.5f, origin.y + (avail.y - size.y) * 0.5f};
        dl->AddRectFilled(p, {p.x + size.x, p.y + size.y}, srgb(30, 32, 36, 245), 16.0f);
        const ImVec2 ts = font_bold_->CalcTextSizeA(26.0f, FLT_MAX, 0.0f, cfg_.title.c_str());
        dl->AddText(font_bold_, 26.0f, {p.x + (size.x - ts.x) * 0.5f, p.y + 26}, c_text(), cfg_.title.c_str());
        auto button = [&](const char* label, f32 y, ImU32 col) {
            ImGui::SetCursorScreenPos({p.x + 40, p.y + y});
            const ImVec2 bp = ImGui::GetCursorScreenPos();
            const bool clicked = ImGui::InvisibleButton(label, {size.x - 80, 48});
            const bool hov = ImGui::IsItemHovered();
            dl->AddRectFilled(bp, {bp.x + size.x - 80, bp.y + 48}, hov ? col : srgb(52, 55, 62), 10.0f);
            const ImVec2 ls = font_bold_->CalcTextSizeA(18.0f, FLT_MAX, 0.0f, label);
            dl->AddText(font_bold_, 18.0f, {bp.x + (size.x - 80 - ls.x) * 0.5f, bp.y + 14}, c_text(), label);
            return clicked;
        };
        if (button("Resume", 84, c_accent())) pause_menu_ = false;
        if (button("Reset Character", 144, srgb(80, 84, 94)) && runtime_) {
            runtime_->respawn();
            pause_menu_ = false;
        }
        const std::string gfx = std::string("Graphics: ") + render::quality_name(quality_);
        if (button(gfx.c_str(), 204, srgb(80, 84, 94)))
            set_quality(static_cast<render::Quality>((static_cast<int>(quality_) + 1) % 4), true);
        if (button("Leave Game", 264, srgb(200, 60, 50))) quit_ = true;
    }
    ImGui::End();
}

} // namespace tefra::studio
