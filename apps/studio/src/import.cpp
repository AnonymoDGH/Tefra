// Import a Roblox place (.rbxl / .rbxlx) or model (.rbxm / .rbxmx) the user owns (ADR-017).
#include "tefra/studio/app.h"

#include "file_dialog.h"

#include "tefra/core/fs.h"
#include "tefra/core/log.h"
#include "tefra/rbx/importer.h"

namespace tefra::studio {

void App::import_roblox_dialog() {
    std::string path;
    const DialogStatus st = dialog_open_file("Import Roblox place",
                                             {{"Roblox places", "*.rbxl;*.rbxlx"}, {"Roblox models", "*.rbxm;*.rbxmx"}},
                                             documents_dir(), path);
    if (st == DialogStatus::Ok)
        import_roblox_file(path);
    else if (st == DialogStatus::Unavailable)
        show_import_dialog_ = true;
}

bool App::import_roblox_file(const std::string& path) {
    const std::string ext = path_extension(path);
    const bool model = ext == ".rbxm" || ext == ".rbxmx";
    rbx::ImportOptions opt;
    auto show_report = [this, path](const rbx::ImportReport& rep) {
        std::string text = "Imported " + path_filename(path) + "\n" + rbx::report_summary(rep);
        usize n = 0;
        for (const std::string& w : rep.warnings) {
            TEFRA_LOG_WARN("Roblox import: %s", w.c_str());
            if (n++ < 12) text += "\n- " + w;
        }
        if (rep.warnings.size() > 12) text += "\n... (see the Output panel)";
        if (rep.scripts)
            text += "\n\nScripts were imported disabled: their Luau code is kept in LuauSource; rewrite them in C#.";
        error_text_ = text;
        open_error_ = true;
    };
    if (model) {
        // Models go into the open place, under the selection (or the Workspace), as one undoable insertion.
        world::Instance* parent = session_.primary();
        if (!parent) parent = session_.model().workspace();
        rbx::ImportReport rep;
        std::string err;
        std::vector<world::Instance*> roots;
        if (!rbx::import_model(path.c_str(), parent, opt, &rep, &err, &roots)) {
            error_text_ = "Cannot import " + path + "\n" + err;
            open_error_ = true;
            return false;
        }
        std::vector<u64> ids;
        for (world::Instance* r : roots) ids.push_back(r->id());
        session_.select_only(ids);
        show_report(rep);
        return true;
    }
    bool ok = false;
    confirm_discard([this, path, opt, show_report, &ok] {
        auto dm = std::make_unique<world::DataModel>();
        rbx::ImportReport rep;
        std::string err;
        if (!rbx::import_place(path.c_str(), *dm, opt, &rep, &err)) {
            error_text_ = "Cannot import " + path + "\n" + err;
            open_error_ = true;
            return;
        }
        // The import becomes a new project next to the others.
        const std::string place = projects_.create_project(path_stem(path));
        session_.adopt(std::move(dm), place);
        if (session_.save()) projects_.touch_recent(place);
        screen_ = Screen::Editor;
        thumbnail_countdown_ = 45;
        projects_dirty_ = true;
        TEFRA_LOG_INFO("Roblox import: %s -> %s", path.c_str(), place.c_str());
        show_report(rep);
        ok = true;
    });
    return ok;
}

} // namespace tefra::studio
