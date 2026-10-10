#include "tefra/core/sysinfo.h"

#include <cstdio>
#include <cstring>
#include <thread>

#if defined(__x86_64__) || defined(__i386__)
#include <cpuid.h>
#endif

#if defined(_WIN32)
#include <windows.h>
// psapi.h needs windows.h first; keep it in its own include block.
#include <psapi.h>
#elif defined(__linux__)
#include <sys/utsname.h>
#include <unistd.h>
#endif

namespace tefra {

static std::string cpu_brand() {
#if defined(__x86_64__) || defined(__i386__)
    unsigned int regs[12] = {};
    unsigned int max_ext = __get_cpuid_max(0x80000000u, nullptr);
    if (max_ext >= 0x80000004u) {
        for (unsigned int i = 0; i < 3; ++i)
            __get_cpuid(0x80000002u + i, &regs[i * 4 + 0], &regs[i * 4 + 1], &regs[i * 4 + 2], &regs[i * 4 + 3]);
        char brand[49];
        std::memcpy(brand, regs, 48);
        brand[48] = '\0';
        const char* p = brand;
        while (*p == ' ') ++p;
        return p;
    }
#endif
    return "unknown";
}

#if defined(__linux__)
static std::string read_os_release() {
    FILE* f = std::fopen("/etc/os-release", "r");
    if (!f) return "";
    char line[256];
    std::string result;
    while (std::fgets(line, sizeof(line), f)) {
        if (std::strncmp(line, "PRETTY_NAME=", 12) == 0) {
            result = line + 12;
            while (!result.empty() && (result.back() == '\n' || result.back() == '"')) result.pop_back();
            if (!result.empty() && result.front() == '"') result.erase(0, 1);
            break;
        }
    }
    std::fclose(f);
    return result;
}
#endif

SystemInfo system_info() {
    SystemInfo info;
    info.cpu = cpu_brand();
    info.threads = std::thread::hardware_concurrency();
#if defined(_WIN32)
    MEMORYSTATUSEX mem{};
    mem.dwLength = sizeof(mem);
    if (GlobalMemoryStatusEx(&mem)) info.ram_mb = mem.ullTotalPhys / (1024ull * 1024ull);
    // RtlGetVersion reports the real version (GetVersionEx lies without a manifest).
    using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    info.os = "Windows";
    if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll")) {
        auto fn = reinterpret_cast<RtlGetVersionFn>(reinterpret_cast<void*>(GetProcAddress(ntdll, "RtlGetVersion")));
        RTL_OSVERSIONINFOW v{};
        v.dwOSVersionInfoSize = sizeof(v);
        if (fn && fn(&v) == 0) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "Windows %lu.%lu build %lu", v.dwMajorVersion, v.dwMinorVersion,
                          v.dwBuildNumber);
            info.os = buf;
        }
    }
#elif defined(__linux__)
    const long pages = sysconf(_SC_PHYS_PAGES);
    const long page_size = sysconf(_SC_PAGE_SIZE);
    if (pages > 0 && page_size > 0) info.ram_mb = u64(pages) * u64(page_size) / (1024ull * 1024ull);
    info.os = read_os_release();
    utsname u{};
    if (uname(&u) == 0) info.os += std::string(info.os.empty() ? "" : " / ") + u.sysname + " " + u.release;
#endif
    return info;
}

f64 process_rss_mb() {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS pmc{};
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
        return static_cast<f64>(pmc.WorkingSetSize) / (1024.0 * 1024.0);
    return 0.0;
#elif defined(__linux__)
    FILE* f = std::fopen("/proc/self/statm", "r");
    if (!f) return 0.0;
    long size = 0, resident = 0;
    const int n = std::fscanf(f, "%ld %ld", &size, &resident);
    std::fclose(f);
    if (n != 2) return 0.0;
    return static_cast<f64>(resident) * static_cast<f64>(sysconf(_SC_PAGE_SIZE)) / (1024.0 * 1024.0);
#else
    return 0.0;
#endif
}

} // namespace tefra
