#include "tefra/core/fs.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <system_error>

#if defined(_WIN32)
// clang-format off
#include <windows.h>
#include <shlobj.h>
// clang-format on
#elif defined(__linux__)
#include <unistd.h>
#endif

namespace tefra {

namespace fs = std::filesystem;

namespace {
// UTF-8 std::string <-> filesystem path (the narrow path constructor uses the ANSI code page on Windows).
fs::path to_path(const std::string& s) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
}
std::string from_path(const fs::path& p) {
    const std::u8string u = p.generic_u8string();
    return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}
FILE* open_file(const char* path, const char* mode) {
#if defined(_WIN32)
    const fs::path p = to_path(path);
    wchar_t wmode[8] = {};
    for (int i = 0; mode[i] && i < 7; ++i) wmode[i] = static_cast<wchar_t>(mode[i]);
    return _wfopen(p.c_str(), wmode);
#else
    return std::fopen(path, mode);
#endif
}
i64 to_unix(fs::file_time_type t) {
    using namespace std::chrono;
    const auto sys = time_point_cast<seconds>(t - fs::file_time_type::clock::now() + system_clock::now());
    return static_cast<i64>(sys.time_since_epoch().count());
}
} // namespace

bool file_read_all(const char* path, std::vector<u8>& out) {
    FILE* f = open_file(path, "rb");
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
    FILE* f = open_file(path, "wb");
    if (!f) return false;
    const usize written = size > 0 ? std::fwrite(data, 1, size, f) : 0;
    const bool ok = std::fclose(f) == 0 && written == size;
    return ok;
}

bool file_exists(const char* path) {
    std::error_code ec;
    return fs::exists(to_path(path), ec);
}

bool make_dirs(const char* path) {
    std::error_code ec;
    fs::create_directories(to_path(path), ec);
    return fs::is_directory(to_path(path), ec);
}

std::string exe_dir() {
#if defined(_WIN32)
    wchar_t buf[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n == MAX_PATH) return ".";
    return from_path(fs::path(buf).parent_path());
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

std::string path_parent(const std::string& path) { return from_path(to_path(path).parent_path()); }
std::string path_filename(const std::string& path) { return from_path(to_path(path).filename()); }
std::string path_stem(const std::string& path) { return from_path(to_path(path).stem()); }
std::string path_extension(const std::string& path) { return from_path(to_path(path).extension()); }

bool list_dir(const std::string& dir, std::vector<DirEntry>& out) {
    out.clear();
    std::error_code ec;
    fs::directory_iterator it(to_path(dir), ec), end;
    if (ec) return false;
    for (; it != end; it.increment(ec)) {
        if (ec) break;
        DirEntry e;
        e.name = from_path(it->path().filename());
        e.is_dir = it->is_directory(ec);
        if (!e.is_dir) e.size = static_cast<u64>(it->file_size(ec));
        const auto t = it->last_write_time(ec);
        if (!ec) e.mtime = to_unix(t);
        out.push_back(std::move(e));
    }
    std::sort(out.begin(), out.end(), [](const DirEntry& a, const DirEntry& b) { return a.name < b.name; });
    return true;
}

bool is_dir(const std::string& path) {
    std::error_code ec;
    return fs::is_directory(to_path(path), ec);
}

i64 file_mtime(const std::string& path) {
    std::error_code ec;
    const auto t = fs::last_write_time(to_path(path), ec);
    return ec ? 0 : to_unix(t);
}

bool copy_file(const std::string& from, const std::string& to) {
    std::error_code ec;
    fs::copy_file(to_path(from), to_path(to), fs::copy_options::overwrite_existing, ec);
    return !ec;
}

bool copy_tree(const std::string& from, const std::string& to) {
    std::error_code ec;
    fs::copy(to_path(from), to_path(to), fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
    return !ec;
}

bool rename_path(const std::string& from, const std::string& to) {
    std::error_code ec;
    fs::rename(to_path(from), to_path(to), ec);
    return !ec;
}

bool remove_tree(const std::string& path) {
    std::error_code ec;
    fs::remove_all(to_path(path), ec);
    return !ec;
}

i64 unix_time_now() {
    using namespace std::chrono;
    return static_cast<i64>(duration_cast<seconds>(system_clock::now().time_since_epoch()).count());
}

namespace {
std::string env(const char* name) {
#if defined(_WIN32)
    wchar_t wname[64] = {};
    for (int i = 0; name[i] && i < 63; ++i) wname[i] = static_cast<wchar_t>(name[i]);
    const wchar_t* v = _wgetenv(wname);
    return v ? from_path(fs::path(v)) : std::string();
#else
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
#endif
}
} // namespace

std::string documents_dir() {
    if (std::string h = env("TEFRA_HOME"); !h.empty()) return h;
#if defined(_WIN32)
    wchar_t buf[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, SHGFP_TYPE_CURRENT, buf)))
        return from_path(fs::path(buf));
    return env("USERPROFILE");
#else
    const std::string home = env("HOME");
    if (home.empty()) return ".";
    const std::string docs = path_join(home, "Documents");
    return is_dir(docs) ? docs : home;
#endif
}

std::string config_dir() {
    if (std::string h = env("TEFRA_HOME"); !h.empty()) return path_join(h, ".config");
#if defined(_WIN32)
    const std::string appdata = env("APPDATA");
    return path_join(appdata.empty() ? documents_dir() : appdata, "Tefra");
#else
    const std::string xdg = env("XDG_CONFIG_HOME");
    if (!xdg.empty()) return path_join(xdg, "tefra");
    const std::string home = env("HOME");
    return path_join(home.empty() ? std::string(".") : home, ".config/tefra");
#endif
}

} // namespace tefra
