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

} // namespace tefra
