#pragma once

#include "tefra/core/types.h"

#include <string>
#include <vector>

namespace tefra {

bool file_read_all(const char* path, std::vector<u8>& out);
bool file_write_all(const char* path, const void* data, usize size);
bool file_exists(const char* path);
// Creates the directory and all parents. Returns true if it exists afterwards.
bool make_dirs(const char* path);
// Directory containing the running executable (no trailing separator).
std::string exe_dir();
// Joins two path fragments with '/'.
std::string path_join(const std::string& a, const std::string& b);
// Parent directory of a path (empty if none).
std::string path_parent(const std::string& path);
// Last component ("dir/My Game.tefra" -> "My Game.tefra"), without extension ("My Game"), extension (".tefra").
std::string path_filename(const std::string& path);
std::string path_stem(const std::string& path);
std::string path_extension(const std::string& path);

// All paths are UTF-8 (converted to UTF-16 on Windows), so user names with accents work.
struct DirEntry {
    std::string name; // file name only
    bool is_dir = false;
    u64 size = 0;
    i64 mtime = 0; // seconds since the Unix epoch
};
bool list_dir(const std::string& dir, std::vector<DirEntry>& out); // sorted by name; false if not a directory
bool is_dir(const std::string& path);
i64 file_mtime(const std::string& path);                        // seconds since the Unix epoch, 0 if missing
u64 file_size(const std::string& path);                         // bytes, 0 if missing
bool copy_file(const std::string& from, const std::string& to); // overwrites
bool copy_tree(const std::string& from, const std::string& to); // recursive, overwrites files
bool rename_path(const std::string& from, const std::string& to);
bool remove_tree(const std::string& path); // file or directory (recursive)
// Deletes the oldest files of dir whose name ends with suffix until at most max_files remain (caches).
void keep_newest_files(const std::string& dir, const std::string& suffix, u32 max_files);
i64 unix_time_now();

// The user's Documents folder (Windows: the shell's Documents, OneDrive-aware; Linux: ~/Documents or ~), and the
// per-user configuration folder (%APPDATA%/Tefra, $XDG_CONFIG_HOME/tefra or ~/.config/tefra). TEFRA_HOME overrides
// both (tests, portable installs).
std::string documents_dir();
std::string config_dir();

} // namespace tefra
