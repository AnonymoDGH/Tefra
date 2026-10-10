#pragma once

// Native file dialogs for Studio: comdlg32 / shell32 on Windows, zenity or kdialog on Linux. When neither is
// available the caller falls back to its own ImGui path prompt (status Unavailable).

#include <string>
#include <vector>

namespace tefra::studio {

struct FileFilter {
    const char* name;    // "Tefra places"
    const char* pattern; // "*.tefra" (several separated by ';': "*.rbxl;*.rbxlx")
};

enum class DialogStatus { Ok, Cancelled, Unavailable };

DialogStatus dialog_open_file(const char* title, const std::vector<FileFilter>& filters, const std::string& start_dir,
                              std::string& out);
DialogStatus dialog_save_file(const char* title, const std::vector<FileFilter>& filters, const std::string& start_dir,
                              const std::string& default_name, std::string& out);
DialogStatus dialog_pick_folder(const char* title, const std::string& start_dir, std::string& out);

// Opens the system file manager on the folder (and selects the file on Windows).
void reveal_in_file_manager(const std::string& path);

} // namespace tefra::studio
