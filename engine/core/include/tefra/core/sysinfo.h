#pragma once

#include "tefra/core/types.h"

#include <string>

namespace tefra {

struct SystemInfo {
    std::string os;
    std::string cpu;
    u32 threads = 0;
    u64 ram_mb = 0;
};

SystemInfo system_info();
// Resident set size of this process, in MiB.
f64 process_rss_mb();

} // namespace tefra
