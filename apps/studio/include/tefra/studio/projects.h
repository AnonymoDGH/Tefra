#pragma once

// Projects shown on Studio's start page (spec M3b §1). A project is a folder under <Documents>/Tefra/Projects:
//
//   My Game/
//     My Game.tefra     the place
//     thumbnail.png     captured from the viewport on every save
//     assets/           files the place references by relative path (meshes, sounds, images)
//
// Archived projects live in <Documents>/Tefra/Archive. Loose .tefra files opened from anywhere appear in the recent
// list too; their thumbnails go to <config>/thumbs. The recent list is <config>/recent.json.

#include "tefra/core/types.h"

#include <string>
#include <vector>

namespace tefra::studio {

struct ProjectInfo {
    std::string name;      // display name (the folder / file stem)
    std::string path;      // the .tefra file
    std::string folder;    // the project folder ("" for a loose file)
    std::string thumbnail; // PNG path (may not exist yet)
    i64 modified = 0;      // mtime of the .tefra (Unix seconds)
    i64 opened = 0;        // last opened from Studio (0 = never)
    bool archived = false;
};

class ProjectManager {
  public:
    // `root` defaults to <Documents>/Tefra; `config` to config_dir(). Both are created on demand.
    explicit ProjectManager(std::string root = {}, std::string config = {});

    const std::string& root() const { return root_; }
    std::string projects_dir() const;
    std::string archive_dir() const;

    std::vector<ProjectInfo> recent(usize max = 24) const; // most recently opened first; missing files skipped
    std::vector<ProjectInfo> projects() const;             // every project folder, most recently modified first
    std::vector<ProjectInfo> archived() const;

    // Path of the .tefra for a new project called `base_name` (made unique: "Obby", "Obby (2)"...). Creates the
    // folder and its assets/ directory.
    std::string create_project(const std::string& base_name) const;

    void touch_recent(const std::string& tefra_path); // after open / save
    void forget(const std::string& tefra_path);       // remove from the recent list

    // File operations (false + error on failure). They keep the recent list in sync.
    bool rename(const ProjectInfo& p, const std::string& new_name, std::string* new_path, std::string* error);
    bool duplicate(const ProjectInfo& p, std::string* new_path, std::string* error);
    bool set_archived(const ProjectInfo& p, bool archived, std::string* error);
    bool remove(const ProjectInfo& p, std::string* error);

    std::string thumbnail_for(const std::string& tefra_path) const;
    ProjectInfo info_for(const std::string& tefra_path) const;

    // "Untitled" -> "Untitled"; strips characters that are invalid in file names on Windows.
    static std::string sanitize_name(const std::string& name);

  private:
    struct RecentEntry {
        std::string path;
        i64 opened = 0;
    };
    std::vector<RecentEntry> load_recent() const;
    void save_recent(const std::vector<RecentEntry>& r) const;
    std::vector<ProjectInfo> scan(const std::string& dir, bool archived) const;
    std::string unique_folder(const std::string& dir, const std::string& base) const;

    std::string root_, config_;
};

// "just now", "5 minutes ago", "yesterday", "12 Mar 2026".
std::string format_age(i64 then, i64 now);

} // namespace tefra::studio
