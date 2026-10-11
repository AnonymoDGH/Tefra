#include "tefra/studio/projects.h"

#include "tefra/core/fs.h"
#include "tefra/core/json_value.h"

#include <algorithm>
#include <cstdio>
#include <ctime>

namespace tefra::studio {

namespace {
constexpr usize kMaxRecent = 64;

u64 fnv1a(const std::string& s) {
    u64 h = 1469598103934665603ull;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

bool same_path(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (usize i = 0; i < a.size(); ++i) {
        char x = a[i] == '\\' ? '/' : a[i], y = b[i] == '\\' ? '/' : b[i];
#if defined(_WIN32)
        if (x >= 'A' && x <= 'Z') x = static_cast<char>(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = static_cast<char>(y - 'A' + 'a');
#endif
        if (x != y) return false;
    }
    return true;
}

bool starts_with_path(const std::string& path, const std::string& dir) {
    return path.size() > dir.size() && same_path(path.substr(0, dir.size()), dir) &&
           (path[dir.size()] == '/' || path[dir.size()] == '\\');
}
} // namespace

ProjectManager::ProjectManager(std::string root, std::string config)
    : root_(root.empty() ? path_join(documents_dir(), "Tefra") : std::move(root)),
      config_(config.empty() ? config_dir() : std::move(config)) {}

std::string ProjectManager::projects_dir() const { return path_join(root_, "Projects"); }
std::string ProjectManager::archive_dir() const { return path_join(root_, "Archive"); }

std::string ProjectManager::sanitize_name(const std::string& name) {
    std::string out;
    for (char c : name) {
        if (c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\' || c == '|' || c == '?' || c == '*' ||
            (static_cast<unsigned char>(c) < 32))
            continue;
        out += c;
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
    while (!out.empty() && out.front() == ' ') out.erase(out.begin());
    if (out.empty()) out = "Untitled";
    if (out.size() > 80) out.resize(80);
    return out;
}

std::string ProjectManager::thumbnail_for(const std::string& tefra_path) const {
    const std::string folder = path_parent(tefra_path);
    if (path_stem(tefra_path) == path_filename(folder)) return path_join(folder, "thumbnail.png");
    char name[32];
    std::snprintf(name, sizeof(name), "%016llx.png", static_cast<unsigned long long>(fnv1a(tefra_path)));
    return path_join(path_join(config_, "thumbs"), name);
}

ProjectInfo ProjectManager::info_for(const std::string& tefra_path) const {
    ProjectInfo p;
    p.path = tefra_path;
    p.name = path_stem(tefra_path);
    const std::string folder = path_parent(tefra_path);
    if (path_stem(tefra_path) == path_filename(folder)) p.folder = folder;
    p.thumbnail = thumbnail_for(tefra_path);
    p.modified = file_mtime(tefra_path);
    p.archived = starts_with_path(tefra_path, archive_dir());
    return p;
}

std::vector<ProjectManager::RecentEntry> ProjectManager::load_recent() const {
    std::vector<RecentEntry> out;
    std::vector<u8> data;
    if (!file_read_all(path_join(config_, "recent.json").c_str(), data)) return out;
    JsonValue doc;
    if (!JsonValue::parse(std::string_view(reinterpret_cast<const char*>(data.data()), data.size()), doc)) return out;
    for (const JsonValue& e : doc["recent"].items()) {
        RecentEntry r;
        r.path = e["path"].as_string();
        r.opened = e["opened"].as_int();
        if (!r.path.empty()) out.push_back(std::move(r));
    }
    return out;
}

void ProjectManager::save_recent(const std::vector<RecentEntry>& r) const {
    make_dirs(config_.c_str());
    JsonValue doc = JsonValue::object();
    JsonValue arr = JsonValue::array();
    for (const RecentEntry& e : r) {
        JsonValue o = JsonValue::object();
        o.set("path", JsonValue(e.path));
        o.set("opened", JsonValue(static_cast<f64>(e.opened)));
        arr.push(std::move(o));
    }
    doc.set("recent", std::move(arr));
    const std::string text = doc.dump(true);
    file_write_all(path_join(config_, "recent.json").c_str(), text.data(), text.size());
}

void ProjectManager::touch_recent(const std::string& tefra_path) {
    std::vector<RecentEntry> r = load_recent();
    r.erase(std::remove_if(r.begin(), r.end(), [&](const RecentEntry& e) { return same_path(e.path, tefra_path); }),
            r.end());
    r.insert(r.begin(), RecentEntry{tefra_path, unix_time_now()});
    if (r.size() > kMaxRecent) r.resize(kMaxRecent);
    save_recent(r);
}

void ProjectManager::forget(const std::string& tefra_path) {
    std::vector<RecentEntry> r = load_recent();
    r.erase(std::remove_if(r.begin(), r.end(), [&](const RecentEntry& e) { return same_path(e.path, tefra_path); }),
            r.end());
    save_recent(r);
}

std::vector<ProjectInfo> ProjectManager::recent(usize max) const {
    std::vector<ProjectInfo> out;
    for (const RecentEntry& e : load_recent()) {
        if (out.size() >= max) break;
        if (!file_exists(e.path.c_str())) continue;
        ProjectInfo p = info_for(e.path);
        if (p.archived) continue;
        p.opened = e.opened;
        out.push_back(std::move(p));
    }
    return out;
}

std::vector<ProjectInfo> ProjectManager::scan(const std::string& dir, bool archived) const {
    std::vector<ProjectInfo> out;
    std::vector<DirEntry> entries;
    if (!list_dir(dir, entries)) return out;
    const std::vector<RecentEntry> rec = load_recent();
    for (const DirEntry& d : entries) {
        if (!d.is_dir || d.name.empty() || d.name[0] == '.') continue;
        const std::string folder = path_join(dir, d.name);
        const std::string file = path_join(folder, d.name + ".tefra");
        if (!file_exists(file.c_str())) continue;
        ProjectInfo p = info_for(file);
        p.archived = archived;
        for (const RecentEntry& e : rec)
            if (same_path(e.path, file)) p.opened = e.opened;
        out.push_back(std::move(p));
    }
    std::sort(out.begin(), out.end(), [](const ProjectInfo& a, const ProjectInfo& b) {
        return std::max(a.modified, a.opened) > std::max(b.modified, b.opened);
    });
    return out;
}

std::vector<ProjectInfo> ProjectManager::projects() const { return scan(projects_dir(), false); }
std::vector<ProjectInfo> ProjectManager::archived() const { return scan(archive_dir(), true); }

std::string ProjectManager::unique_folder(const std::string& dir, const std::string& base) const {
    const std::string clean = sanitize_name(base);
    std::string name = clean;
    for (int n = 2; file_exists(path_join(dir, name).c_str()); ++n) name = clean + " (" + std::to_string(n) + ")";
    return name;
}

std::string ProjectManager::create_project(const std::string& base_name) const {
    const std::string dir = projects_dir();
    make_dirs(dir.c_str());
    const std::string name = unique_folder(dir, base_name);
    const std::string folder = path_join(dir, name);
    make_dirs(path_join(folder, "assets").c_str());
    return path_join(folder, name + ".tefra");
}

bool ProjectManager::rename(const ProjectInfo& p, const std::string& new_name, std::string* new_path,
                            std::string* error) {
    const std::string clean = sanitize_name(new_name);
    std::string dst;
    if (!p.folder.empty()) {
        const std::string parent = path_parent(p.folder);
        if (clean != path_filename(p.folder) && file_exists(path_join(parent, clean).c_str())) {
            if (error) *error = "a project called '" + clean + "' already exists";
            return false;
        }
        const std::string folder = path_join(parent, clean);
        if (clean != path_filename(p.folder) && !rename_path(p.folder, folder)) {
            if (error) *error = "cannot rename the project folder";
            return false;
        }
        const std::string old_file = path_join(folder, path_filename(p.path));
        dst = path_join(folder, clean + ".tefra");
        if (old_file != dst && !rename_path(old_file, dst)) {
            if (error) *error = "cannot rename the place file";
            return false;
        }
    } else {
        dst = path_join(path_parent(p.path), clean + ".tefra");
        if (dst != p.path && file_exists(dst.c_str())) {
            if (error) *error = "a file called '" + clean + ".tefra' already exists";
            return false;
        }
        if (dst != p.path && !rename_path(p.path, dst)) {
            if (error) *error = "cannot rename the file";
            return false;
        }
        const std::string old_thumb = thumbnail_for(p.path);
        if (file_exists(old_thumb.c_str())) rename_path(old_thumb, thumbnail_for(dst));
    }
    // Keep the recent entry (and its date) under the new path.
    std::vector<RecentEntry> r = load_recent();
    for (RecentEntry& e : r)
        if (same_path(e.path, p.path)) e.path = dst;
    save_recent(r);
    if (new_path) *new_path = dst;
    return true;
}

bool ProjectManager::duplicate(const ProjectInfo& p, std::string* new_path, std::string* error) {
    std::string dst;
    if (!p.folder.empty()) {
        const std::string parent = path_parent(p.folder);
        const std::string name = unique_folder(parent, p.name + " copy");
        const std::string folder = path_join(parent, name);
        if (!copy_tree(p.folder, folder)) {
            if (error) *error = "cannot copy the project folder";
            return false;
        }
        dst = path_join(folder, name + ".tefra");
        if (!rename_path(path_join(folder, path_filename(p.path)), dst)) {
            if (error) *error = "cannot rename the copied place";
            return false;
        }
    } else {
        const std::string dir = path_parent(p.path);
        std::string name = p.name + " copy";
        for (int n = 2; file_exists(path_join(dir, name + ".tefra").c_str()); ++n)
            name = p.name + " copy (" + std::to_string(n) + ")";
        dst = path_join(dir, name + ".tefra");
        if (!copy_file(p.path, dst)) {
            if (error) *error = "cannot copy the file";
            return false;
        }
        const std::string thumb = thumbnail_for(p.path);
        if (file_exists(thumb.c_str())) {
            make_dirs(path_parent(thumbnail_for(dst)).c_str());
            copy_file(thumb, thumbnail_for(dst));
        }
    }
    touch_recent(dst);
    if (new_path) *new_path = dst;
    return true;
}

bool ProjectManager::set_archived(const ProjectInfo& p, bool archived, std::string* error) {
    if (p.folder.empty()) {
        if (archived) forget(p.path); // loose files: archiving only hides them from the recent list
        return true;
    }
    const std::string dir = archived ? archive_dir() : projects_dir();
    make_dirs(dir.c_str());
    const std::string name = unique_folder(dir, path_filename(p.folder));
    const std::string folder = path_join(dir, name);
    if (!rename_path(p.folder, folder)) {
        if (error) *error = "cannot move the project folder";
        return false;
    }
    std::string dst = path_join(folder, path_filename(p.path));
    if (name != path_filename(p.folder)) {
        const std::string renamed = path_join(folder, name + ".tefra");
        if (rename_path(dst, renamed)) dst = renamed;
    }
    std::vector<RecentEntry> r = load_recent();
    for (RecentEntry& e : r)
        if (same_path(e.path, p.path)) e.path = dst;
    save_recent(r);
    return true;
}

bool ProjectManager::remove(const ProjectInfo& p, std::string* error) {
    const std::string target = p.folder.empty() ? p.path : p.folder;
    if (!remove_tree(target)) {
        if (error) *error = "cannot delete " + target;
        return false;
    }
    if (p.folder.empty()) remove_tree(thumbnail_for(p.path));
    forget(p.path);
    return true;
}

std::string format_age(i64 then, i64 now) {
    if (then <= 0) return "never";
    const i64 d = now - then;
    char buf[64];
    if (d < 60) return "just now";
    if (d < 3600) {
        std::snprintf(buf, sizeof(buf), "%lld minute%s ago", static_cast<long long>(d / 60), d / 60 == 1 ? "" : "s");
        return buf;
    }
    if (d < 86400) {
        std::snprintf(buf, sizeof(buf), "%lld hour%s ago", static_cast<long long>(d / 3600), d / 3600 == 1 ? "" : "s");
        return buf;
    }
    if (d < 2 * 86400) return "yesterday";
    if (d < 7 * 86400) {
        std::snprintf(buf, sizeof(buf), "%lld days ago", static_cast<long long>(d / 86400));
        return buf;
    }
    const std::time_t t = static_cast<std::time_t>(then);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    static const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    std::snprintf(buf, sizeof(buf), "%d %s %d", tm.tm_mday, months[tm.tm_mon % 12], tm.tm_year + 1900);
    return buf;
}

} // namespace tefra::studio
