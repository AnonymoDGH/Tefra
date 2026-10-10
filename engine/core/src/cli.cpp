#include "tefra/core/cli.h"

#include <cstdlib>
#include <cstring>

namespace tefra {

int CommandLine::find(const char* name) const {
    for (int i = 1; i < argc_; ++i) {
        const char* a = argv_[i];
        if (a[0] == '-' && a[1] == '-' && std::strcmp(a + 2, name) == 0) return i;
    }
    return -1;
}

bool CommandLine::has(const char* name) const { return find(name) >= 0; }

const char* CommandLine::get(const char* name, const char* fallback) const {
    const int i = find(name);
    if (i < 0 || i + 1 >= argc_) return fallback;
    return argv_[i + 1];
}

i64 CommandLine::get_int(const char* name, i64 fallback) const {
    const char* v = get(name);
    if (!v) return fallback;
    char* end = nullptr;
    const long long r = std::strtoll(v, &end, 10);
    return end && *end == '\0' ? static_cast<i64>(r) : fallback;
}

f64 CommandLine::get_float(const char* name, f64 fallback) const {
    const char* v = get(name);
    if (!v) return fallback;
    char* end = nullptr;
    const double r = std::strtod(v, &end);
    return end && *end == '\0' ? r : fallback;
}

} // namespace tefra
