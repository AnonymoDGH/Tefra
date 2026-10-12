// tefra_import: converts a Roblox place (.rbxl / .rbxlx) into a Tefra place (.tefra) and prints the conversion report
// (ADR-017: only the user's own places; nothing is downloaded, rbxassetid:// references are kept as text).
//
//   tefra_import in.rbxl|in.rbxlx out.tefra [--report report.json] [--stud-meters 0.28] [--enable-scripts]
//   [--all-properties]

#include "tefra/rbx/importer.h"
#include "tefra/world/datamodel.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

void usage() {
    std::fprintf(stderr,
                 "usage: tefra_import <in.rbxl|in.rbxlx> <out.tefra> [--report report.json] [--stud-meters 0.28]\n"
                 "                    [--enable-scripts] [--all-properties]\n");
}

bool write_text(const char* path, const std::string& text) {
    std::FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    return std::fclose(f) == 0 && ok;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        usage();
        return 2;
    }
    const char* input = argv[1];
    const char* output = argv[2];
    const char* report_path = nullptr;
    tefra::rbx::ImportOptions options;
    for (int i = 3; i < argc; ++i) {
        if (std::strcmp(argv[i], "--report") == 0 && i + 1 < argc) {
            report_path = argv[++i];
        } else if (std::strcmp(argv[i], "--stud-meters") == 0 && i + 1 < argc) {
            options.stud_to_meters = std::strtof(argv[++i], nullptr);
            if (!(options.stud_to_meters > 0.0f)) {
                std::fprintf(stderr, "tefra_import: --stud-meters must be positive\n");
                return 2;
            }
        } else if (std::strcmp(argv[i], "--all-properties") == 0) {
            options.keep_all_properties = true;
        } else if (std::strcmp(argv[i], "--enable-scripts") == 0) {
            options.convert_scripts_disabled = false;
        } else {
            usage();
            return 2;
        }
    }

    tefra::world::DataModel dm;
    tefra::rbx::ImportReport report;
    std::string error;
    if (!tefra::rbx::import_place(input, dm, options, &report, &error)) {
        std::fprintf(stderr, "tefra_import: %s\n", error.c_str());
        return 1;
    }
    if (!dm.save(output)) {
        std::fprintf(stderr, "tefra_import: cannot write %s\n", output);
        return 1;
    }

    std::printf("%s\n", tefra::rbx::report_summary(report).c_str());
    for (const std::string& w : report.warnings) std::printf("warning: %s\n", w.c_str());
    for (const auto& [name, count] : report.unsupported_classes)
        std::printf("placeholder (Folder + RobloxClass): %s x%u\n", name.c_str(), count);
    if (report_path) {
        if (!write_text(report_path, tefra::rbx::report_to_json(report).dump(true) + "\n")) {
            std::fprintf(stderr, "tefra_import: cannot write %s\n", report_path);
            return 1;
        }
        std::printf("report: %s\n", report_path);
    }
    std::printf("wrote %s\n", output);
    return 0;
}
