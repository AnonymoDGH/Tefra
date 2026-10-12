// Import a Roblox place (.rbxl / .rbxlx) or model (.rbxm / .rbxmx) the user owns (ADR-017).
#include "tefra/studio/app.h"

#include "file_dialog.h"

#include <cctype>

#include "tefra/core/fs.h"
#include "tefra/core/log.h"
#include "tefra/rbx/importer.h"
#include "tefra/renderer/model_import.h"
#include "tefra/world/scene_sync.h"

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

// ---- 3D models: FBX, OBJ, STL and PLY become glTF (.glb) in the project's assets; .glb and .gltf are copied
// ----------

namespace {
constexpr const char* kModelFilter = "*.fbx;*.obj;*.stl;*.ply;*.glb;*.gltf";

std::string lower_ext(const std::string& path) {
    std::string e = path_extension(path);
    for (char& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return e;
}
} // namespace

void App::import_model_dialog() {
    std::string path;
    const DialogStatus st = dialog_open_file("Import 3D model", {{"3D models", kModelFilter}}, documents_dir(), path);
    if (st == DialogStatus::Ok) {
        std::string err;
        if (!import_model_at_camera(path, err)) {
            error_text_ = "Cannot import " + path + "\n" + err;
            open_error_ = true;
        }
    } else if (st == DialogStatus::Unavailable) {
        error_text_ = "No file dialog is available on this system. The import_model tool takes the path instead.";
        open_error_ = true;
    }
}

bool App::import_model_asset(const std::string& src, const render::ModelImportOptions& opt, std::string& mesh_id,
                             std::string& error, render::ModelImportReport* report) {
    if (!file_exists(src.c_str())) {
        error = "file not found: " + src;
        return false;
    }
    const std::string ext = lower_ext(src);
    const bool gltf = ext == ".glb" || ext == ".gltf";
    if (!gltf && render::model_import_format(src).empty()) {
        error = "expected .glb, .gltf, .fbx, .obj, .stl or .ply";
        return false;
    }
    const std::string stem = path_stem(src);
    // Saved place: <project>/assets/<stem>/. Unsaved: a cache folder, and the MeshId is its absolute path.
    const bool saved = !session_.path().empty();
    const std::string base =
        saved ? path_join(path_parent(session_.path()), "assets") : path_join(config_dir(), "model_cache");
    const std::string dir = path_join(base, stem);
    if (!make_dirs(dir.c_str())) {
        error = "cannot create " + dir;
        return false;
    }
    std::string file;
    if (gltf) {
        // A .gltf brings its buffers and textures: copy its whole folder.
        file = path_join(dir, path_filename(src));
        const bool ok = ext == ".gltf" ? copy_tree(path_parent(src), dir) : copy_file(src, file);
        if (!ok) {
            error = "cannot copy the model into " + dir;
            return false;
        }
    } else {
        file = path_join(dir, stem + ".glb");
        if (!render::convert_to_glb(src.c_str(), file.c_str(), opt, &error, report)) return false;
        if (report)
            for (const std::string& w : report->warnings) TEFRA_LOG_WARN("Model import: %s", w.c_str());
    }
    mesh_id = saved ? "assets/" + stem + "/" + path_filename(file) : file;
    return true;
}

bool App::import_model_at_camera(const std::string& path, std::string& error) {
    const render::ModelImportOptions opt;
    std::string mesh_id;
    render::ModelImportReport rep;
    if (!import_model_asset(path, opt, mesh_id, error, &rep)) return false;
    world::DataModel& dm = session_.model();
    world::Instance* mp = session_.create("MeshPart", dm.workspace());
    if (!mp) {
        error = "cannot create the MeshPart";
        return false;
    }
    session_.rename(mp, path_stem(path));
    auto put = [&](const char* prop, const world::Value& v) {
        if (const world::PropInfo* p = mp->cls().find_prop(prop))
            session_.set_property(mp, p->index, v, "Import model");
    };
    put("MeshId", world::Value::string(mesh_id));
    put("Size", world::Value::vector3({1.0f, 1.0f, 1.0f})); // the model's own size (glTF metres)
    // Where the camera looks (or 12 m ahead when nothing is hit within 60 m), on the surface it hits.
    const vec3 cam{f32(camera_.position.x), f32(camera_.position.y), f32(camera_.position.z)};
    const vec3 f = camera_.forward();
    const world::RayHit hit = sync_ ? sync_->raycast(cam, f, 60.0f, mp) : world::RayHit{};
    put("Position", world::Value::vector3(hit.instance ? hit.position : cam + f * 12.0f));
    session_.select_only({mp->id()});
    TEFRA_LOG_INFO("Model import: %s -> %s (%u meshes, %u textures)", path.c_str(), mesh_id.c_str(), rep.meshes,
                   rep.textures);
    return true;
}

} // namespace tefra::studio
