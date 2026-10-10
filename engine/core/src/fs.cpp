#include "tefra/core/fs.h"

#include <cstdio>
#include <filesystem>
#include <system_error>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

namespace tefra {

bool file_read_all(const char* path, std::vector<u8>& out) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (size < 0) {
        std::fclose(f);
        return false;
    }
    out.resize(static_cast<usize>(size));
    const usize read = size > 0 ? std::fread(out.data(), 1, out.size(), f) : 0;
    std::fclose(f);
    return read == out.size();
}

bool file_write_all(const char* path, const void* data, usize size) {
    FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    const usize written = size > 0 ? std::fwrite(data, 1, size, f) : 0;
    const bool ok = std::fclose(f) == 0 && written == size;
    return ok;
}

bool file_exists(const char* path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

bool make_dirs(const char* path) {
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    return std::filesystem::is_directory(path, ec);
}

std::string exe_dir() {
#if defined(_WIN32)
    wchar_t buf[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n == MAX_PATH) return ".";
    return std::filesystem::path(buf).parent_path().generic_string();
#elif defined(__linux__)
    char buf[4096];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return ".";
    buf[n] = '\0';
    return std::filesystem::path(buf).parent_path().generic_string();
#else
    return ".";
#endif
}

std::string path_join(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (a.back() == '/' || a.back() == '\\') return a + b;
    return a + "/" + b;
}

std::string path_parent(const std::string& path) { return std::filesystem::path(path).parent_path().generic_string(); }

} // namespace tefra
