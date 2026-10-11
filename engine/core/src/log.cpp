#include "tefra/core/log.h"
#include "tefra/core/time.h"
#include <cstdlib>

#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace tefra {
namespace {

std::mutex g_mutex;
LogLevel g_level = LogLevel::Info;
FILE* g_file = nullptr;
LogSinkFn g_sink = nullptr;
void* g_sink_user = nullptr;
const u64 g_start_ns = time_now_ns();

const char* level_tag(LogLevel level) {
    switch (level) {
    case LogLevel::Trace: return "trace";
    case LogLevel::Debug: return "debug";
    case LogLevel::Info: return "info ";
    case LogLevel::Warn: return "WARN ";
    case LogLevel::Error: return "ERROR";
    }
    return "?";
}

} // namespace

void log_set_level(LogLevel level) { g_level = level; }
LogLevel log_level() { return g_level; }

bool log_set_file(const char* path) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file) {
        std::fclose(g_file);
        g_file = nullptr;
    }
    if (!path) return true;
    g_file = std::fopen(path, "w");
    return g_file != nullptr;
}

void log_set_sink(LogSinkFn fn, void* user) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_sink = fn;
    g_sink_user = user;
}

void log_write(LogLevel level, const char* fmt, ...) {
    if (level < g_level) return;
    char msg[2048];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
    const f64 t = static_cast<f64>(time_now_ns() - g_start_ns) * 1e-9;
    std::lock_guard<std::mutex> lock(g_mutex);
    std::fprintf(stderr, "[%9.3f %s] %s\n", t, level_tag(level), msg);
    if (g_file) {
        std::fprintf(g_file, "[%9.3f %s] %s\n", t, level_tag(level), msg);
        std::fflush(g_file);
    }
    if (g_sink) g_sink(g_sink_user, level, t, msg);
}

bool trace_enabled() {
    static const bool on = [] {
        const char* e = std::getenv("TEFRA_TRACE_PASSES");
        return e && e[0] == '1';
    }();
    return on;
}

} // namespace tefra
