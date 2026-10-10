// Import a Roblox place (.rbxl / .rbxlx) or model (.rbxm / .rbxmx) the user owns (ADR-017).
#include "tefra/studio/app.h"

#include "file_dialog.h"

#include "tefra/core/fs.h"
#include "tefra/core/log.h"

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
    error_text_ = "This build of Tefra Studio has no Roblox importer yet.\n" + path;
    open_error_ = true;
    return false;
}

} // namespace tefra::studio
