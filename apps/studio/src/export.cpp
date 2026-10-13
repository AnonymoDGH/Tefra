// Export Game (spec M3b §2, ADR-018): writes a self-contained folder that runs the place with tefra_player:
//
//   <Game>/
//     <Game>(.exe)   tefra_player renamed after the game
//     game.json      title, place file, window
//     game.tefra     the place
//     shaders/       SPIR-V
//     resources/     fonts, built-in sounds
//     managed/       C# runtime support (only when the place has scripts; needs the .NET 8 runtime)
//     <assets>       every file the place references by a relative path (meshes, sounds, images)
#include "tefra/studio/app.h"

#include "file_dialog.h"

#include "tefra/core/fs.h"
#include "tefra/core/json_value.h"
#include "tefra/core/log.h"

#include <imgui.h>

#include <cstdio>
#include <set>

namespace tefra::studio {

namespace {

#if defined(_WIN32)
constexpr const char* kExeSuffix = ".exe";
#else
constexpr const char* kExeSuffix = "";
#endif

struct ExportState {
    bool open = false;
    char title[128] = {};
    char folder[512] = {};
    int width = 1280, height = 720;
    bool fullscreen = false;
    std::string result;
    bool ok = false;
    std::string out_dir;
};
ExportState g_export;

bool is_relative_asset(const std::string& v) {
    if (v.empty() || v.size() > 400) return false;
    if (v.find("://") != std::string::npos || v.rfind("builtin:", 0) == 0) return false;
    if (v[0] == '/' || v[0] == '\\' || (v.size() > 1 && v[1] == ':')) return false;
    if (v.find('\n') != std::string::npos || v.find("..") != std::string::npos) return false;
    return v.find('.') != std::string::npos;
}

u64 tree_size(const std::string& path) {
    std::vector<DirEntry> entries;
    if (!list_dir(path, entries)) return 0;
    u64 total = 0;
    for (const DirEntry& e : entries) total += e.is_dir ? tree_size(path_join(path, e.name)) : e.size;
    return total;
}

} // namespace

void App::export_game_dialog() {
    const std::string name = session_.path().empty() ? std::string("My Game") : path_stem(session_.path());
    std::snprintf(g_export.title, sizeof(g_export.title), "%s", name.c_str());
    const std::string dir = path_join(path_join(projects_.root(), "Exports"), ProjectManager::sanitize_name(name));
    std::snprintf(g_export.folder, sizeof(g_export.folder), "%s", dir.c_str());
    g_export.result.clear();
    g_export.open = true;
}

void App::draw_export_modal() {
    if (g_export.open) {
        ImGui::OpenPopup("Export Game");
        g_export.open = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, {0.5f, 0.5f});
    if (!ImGui::BeginPopupModal("Export Game", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::TextDisabled("Creates a folder with a standalone executable that runs this place.");
    ImGui::Dummy({0, 4});
    ImGui::TextUnformatted("Game title");
    ImGui::SetNextItemWidth(520);
    ImGui::InputText("##title", g_export.title, sizeof(g_export.title));
    ImGui::TextUnformatted("Output folder");
    ImGui::SetNextItemWidth(430);
    ImGui::InputText("##folder", g_export.folder, sizeof(g_export.folder));
    ImGui::SameLine();
    if (ImGui::Button("Browse...", {82, 0})) {
        std::string picked;
        if (dialog_pick_folder("Export to", g_export.folder, picked) == DialogStatus::Ok)
            std::snprintf(g_export.folder, sizeof(g_export.folder), "%s",
                          path_join(picked, ProjectManager::sanitize_name(g_export.title)).c_str());
    }
    ImGui::TextUnformatted("Window");
    ImGui::SetNextItemWidth(120);
    ImGui::InputInt("##w", &g_export.width, 0);
    ImGui::SameLine();
    ImGui::TextUnformatted("x");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    ImGui::InputInt("##h", &g_export.height, 0);
    ImGui::SameLine();
    ImGui::Checkbox("Fullscreen", &g_export.fullscreen);
    g_export.width = clamp(g_export.width, 640, 7680);
    g_export.height = clamp(g_export.height, 360, 4320);
    ImGui::Dummy({0, 6});

    if (ImGui::Button("Export", {130, 0})) {
        g_export.ok = false;
        std::string log;
        const std::string out = g_export.folder;
        const std::string title = ProjectManager::sanitize_name(g_export.title);
        const std::string bin = exe_dir();
        const std::string player = path_join(bin, std::string("tefra_player") + kExeSuffix);
        if (!file_exists(player.c_str())) {
            g_export.result = "tefra_player was not found next to Tefra Studio (" + player + ").";
        } else if (!make_dirs(out.c_str())) {
            g_export.result = "Cannot create " + out;
        } else {
            u32 files = 0;
            bool ok = copy_file(player, path_join(out, title + kExeSuffix));
            ok = ok && copy_tree(path_join(bin, "shaders"), path_join(out, "shaders"));
            ++files;
            ok = ok && copy_tree(resources_dir(), path_join(out, "resources"));
            remove_tree(path_join(path_join(out, "resources"), "templates")); // Studio-only
            // C# support only when the place has scripts.
            bool scripts = false;
            for (world::Instance* i : session_.model().root().descendants())
                if (i->is_a("BaseScript") || i->is_a("ModuleScript")) scripts = true;
            if (scripts && is_dir(path_join(bin, "managed")))
                ok = ok && copy_tree(path_join(bin, "managed"), path_join(out, "managed"));
            // Referenced assets (relative to the project folder) keep their relative paths.
            const std::string project = session_.path().empty() ? std::string() : path_parent(session_.path());
            std::set<std::string> assets;
            if (!project.empty())
                for (world::Instance* i : session_.model().root().descendants())
                    for (u32 k = 0; k < i->cls().prop_count(); ++k) {
                        const world::Value& v = i->get(k);
                        if (v.type != world::PropType::String) continue;
                        std::string a = v.s;
                        if (const usize hash = a.find('#'); hash != std::string::npos) a.resize(hash);
                        if (is_relative_asset(a) && file_exists(path_join(project, a).c_str())) assets.insert(a);
                    }
            for (const std::string& a : assets) {
                const std::string src = path_join(project, a), dst = path_join(out, a);
                make_dirs(path_parent(dst).c_str());
                // A .gltf brings its buffers and textures: copy its whole folder.
                if (path_extension(a) == ".gltf")
                    ok = copy_tree(path_parent(src), path_parent(dst)) && ok;
                else
                    ok = copy_file(src, dst) && ok;
                ++files;
            }
            ok = ok && session_.model().save(path_join(out, "game.tefra").c_str());
            JsonValue cfg = JsonValue::object();
            cfg.set("title", JsonValue(std::string(g_export.title)));
            cfg.set("place", JsonValue(std::string("game.tefra")));
            cfg.set("width", JsonValue(static_cast<f64>(g_export.width)));
            cfg.set("height", JsonValue(static_cast<f64>(g_export.height)));
            cfg.set("fullscreen", JsonValue(g_export.fullscreen));
            cfg.set("quality", JsonValue(std::string("auto"))); // low / medium / high / ultra, or auto-detect
            const std::string text = cfg.dump(true);
            ok = ok && file_write_all(path_join(out, "game.json").c_str(), text.data(), text.size());
            std::string readme = std::string(g_export.title) +
                                 "\n\nMade with Tefra (https://github.com/AnonymoDGH/Tefra).\nRun " + title +
                                 kExeSuffix + " to play. Requires a GPU with Vulkan 1.2.\n";
            if (scripts)
                readme += "This game uses C# scripts: install the .NET 8 runtime (https://dotnet.microsoft.com).\n";
            readme += "\nControls: WASD move, Shift sprint, Space jump (hold to keep hopping), right mouse orbits the camera, Left Ctrl toggles shift lock, wheel zooms (all the way in = first person), Esc menu.\n";
            file_write_all(path_join(out, "README.txt").c_str(), readme.data(), readme.size());
            char buf[256];
            std::snprintf(buf, sizeof(buf), "Exported to %s\n%u asset file(s), %.1f MB in total%s", out.c_str(),
                          static_cast<unsigned>(assets.size()), f64(tree_size(out)) / (1024.0 * 1024.0),
                          scripts ? "\nUses C# scripts: players need the .NET 8 runtime." : "");
            g_export.result = ok ? buf : "Some files could not be copied to " + out;
            g_export.ok = ok;
            g_export.out_dir = out;
            (void)files;
            TEFRA_LOG_INFO("Export Game: %s", g_export.result.c_str());
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Close", {130, 0}) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    if (!g_export.result.empty()) {
        ImGui::Dummy({0, 4});
        ImGui::TextWrapped("%s", g_export.result.c_str());
        if (g_export.ok && ImGui::Button("Show Folder")) reveal_in_file_manager(g_export.out_dir);
    }
    ImGui::EndPopup();
}

} // namespace tefra::studio
