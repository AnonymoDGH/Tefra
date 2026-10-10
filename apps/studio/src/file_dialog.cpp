#include "file_dialog.h"

#include "tefra/core/fs.h"

#include <cstdio>
#include <cstdlib>

#if defined(_WIN32)
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <windows.h>
#else
#include <spawn.h>
#include <sys/wait.h>
extern char** environ;
#endif

namespace tefra::studio {

#if defined(_WIN32)
namespace {
std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}
std::string narrow(const wchar_t* w) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string s(static_cast<size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    for (char& c : s)
        if (c == '\\') c = '/';
    return s;
}
std::wstring native(const std::string& path) {
    std::wstring w = widen(path);
    for (wchar_t& c : w)
        if (c == L'/') c = L'\\';
    return w;
}
// "Name\0*.a;*.b\0...\0\0"
std::wstring filter_string(const std::vector<FileFilter>& filters) {
    std::wstring f;
    for (const FileFilter& x : filters) {
        f += widen(x.name);
        f += L'\0';
        f += widen(x.pattern);
        f += L'\0';
    }
    f += L"All files";
    f += L'\0';
    f += L"*.*";
    f += L'\0';
    f += L'\0';
    return f;
}
DialogStatus file_dialog(bool save, const char* title, const std::vector<FileFilter>& filters,
                         const std::string& start_dir, const std::string& default_name, std::string& out) {
    wchar_t buf[4096] = {};
    const std::wstring def = widen(default_name);
    if (def.size() < 4000) std::wcsncpy(buf, def.c_str(), 4000);
    const std::wstring filter = filter_string(filters);
    const std::wstring wtitle = widen(title);
    const std::wstring dir = native(start_dir);
    std::wstring ext;
    if (!filters.empty()) {
        std::string p = filters[0].pattern;
        const size_t dot = p.find('.');
        const size_t end = p.find(';');
        if (dot != std::string::npos) ext = widen(p.substr(dot + 1, end == std::string::npos ? end : end - dot - 1));
    }
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetActiveWindow();
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = buf;
    ofn.nMaxFile = 4096;
    ofn.lpstrTitle = wtitle.c_str();
    ofn.lpstrInitialDir = dir.empty() ? nullptr : dir.c_str();
    ofn.lpstrDefExt = ext.empty() ? nullptr : ext.c_str();
    ofn.Flags = OFN_NOCHANGEDIR | OFN_EXPLORER |
                (save ? OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST : OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST);
    const BOOL ok = save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
    if (!ok) return CommDlgExtendedError() == 0 ? DialogStatus::Cancelled : DialogStatus::Unavailable;
    out = narrow(buf);
    return DialogStatus::Ok;
}
} // namespace

DialogStatus dialog_open_file(const char* title, const std::vector<FileFilter>& filters, const std::string& start_dir,
                              std::string& out) {
    return file_dialog(false, title, filters, start_dir, {}, out);
}

DialogStatus dialog_save_file(const char* title, const std::vector<FileFilter>& filters, const std::string& start_dir,
                              const std::string& default_name, std::string& out) {
    return file_dialog(true, title, filters, start_dir, default_name, out);
}

DialogStatus dialog_pick_folder(const char* title, const std::string& start_dir, std::string& out) {
    (void)start_dir;
    const std::wstring wtitle = widen(title);
    BROWSEINFOW bi{};
    bi.hwndOwner = GetActiveWindow();
    bi.lpszTitle = wtitle.c_str();
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return DialogStatus::Cancelled;
    wchar_t buf[MAX_PATH] = {};
    const BOOL ok = SHGetPathFromIDListW(pidl, buf);
    CoTaskMemFree(pidl);
    if (!ok) return DialogStatus::Cancelled;
    out = narrow(buf);
    return DialogStatus::Ok;
}

void reveal_in_file_manager(const std::string& path) {
    std::wstring args = L"/select,\"" + native(path) + L"\"";
    if (is_dir(path)) args = L"\"" + native(path) + L"\"";
    ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
}

#else // Linux / other POSIX: zenity, then kdialog.

namespace {
std::string quote(const std::string& s) {
    std::string q = "'";
    for (char c : s) {
        if (c == '\'')
            q += "'\\''";
        else
            q += c;
    }
    return q + "'";
}

bool have(const char* tool) {
    const std::string cmd = std::string("command -v ") + tool + " >/dev/null 2>&1";
    return std::system(cmd.c_str()) == 0;
}

// Runs the command and returns its first output line; Cancelled on a non-zero exit.
DialogStatus run(const std::string& cmd, std::string& out) {
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return DialogStatus::Unavailable;
    char buf[4096];
    std::string text;
    while (std::fgets(buf, sizeof(buf), p)) text += buf;
    const int status = pclose(p);
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
    if (status != 0 || text.empty()) return DialogStatus::Cancelled;
    out = text;
    return DialogStatus::Ok;
}

std::string patterns(const char* p, char sep) {
    std::string s = p;
    for (char& c : s)
        if (c == ';') c = sep;
    return s;
}

DialogStatus posix_dialog(int mode, const char* title, const std::vector<FileFilter>& filters,
                          const std::string& start_dir, const std::string& default_name, std::string& out) {
    std::string start = start_dir.empty() ? std::string(".") : start_dir;
    if (have("zenity")) {
        std::string cmd = "zenity --file-selection --title=" + quote(title);
        if (mode == 1) cmd += " --save --confirm-overwrite";
        if (mode == 2) cmd += " --directory";
        cmd += " --filename=" + quote(path_join(start, mode == 1 ? default_name : std::string()));
        for (const FileFilter& f : filters)
            cmd += " --file-filter=" + quote(std::string(f.name) + " | " + patterns(f.pattern, ' '));
        return run(cmd + " 2>/dev/null", out);
    }
    if (have("kdialog")) {
        std::string filter;
        for (const FileFilter& f : filters) {
            if (!filter.empty()) filter += "|";
            filter += patterns(f.pattern, ' ') + "|" + f.name;
        }
        std::string cmd = "kdialog --title " + quote(title);
        if (mode == 0) cmd += " --getopenfilename " + quote(start) + " " + quote(filter);
        if (mode == 1) cmd += " --getsavefilename " + quote(path_join(start, default_name)) + " " + quote(filter);
        if (mode == 2) cmd += " --getexistingdirectory " + quote(start);
        return run(cmd + " 2>/dev/null", out);
    }
    return DialogStatus::Unavailable;
}
} // namespace

DialogStatus dialog_open_file(const char* title, const std::vector<FileFilter>& filters, const std::string& start_dir,
                              std::string& out) {
    return posix_dialog(0, title, filters, start_dir, {}, out);
}

DialogStatus dialog_save_file(const char* title, const std::vector<FileFilter>& filters, const std::string& start_dir,
                              const std::string& default_name, std::string& out) {
    return posix_dialog(1, title, filters, start_dir, default_name, out);
}

DialogStatus dialog_pick_folder(const char* title, const std::string& start_dir, std::string& out) {
    return posix_dialog(2, title, {}, start_dir, {}, out);
}

void reveal_in_file_manager(const std::string& path) {
    const std::string dir = is_dir(path) ? path : path_parent(path);
    char arg0[] = "xdg-open";
    std::string d = dir;
    char* argv[] = {arg0, d.data(), nullptr};
    pid_t pid = 0;
    if (posix_spawnp(&pid, "xdg-open", nullptr, nullptr, argv, environ) == 0) waitpid(pid, nullptr, WNOHANG);
}

#endif

} // namespace tefra::studio
