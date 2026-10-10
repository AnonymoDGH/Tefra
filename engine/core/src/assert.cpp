#include "tefra/core/assert.h"
#include "tefra/core/log.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

namespace tefra::detail {

void panic(const char* file, int line, const char* fmt, ...) {
    char msg[1024];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
    log_write(LogLevel::Error, "PANIC %s:%d: %s", file, line, msg);
    std::fflush(stderr);
    std::abort();
}

} // namespace tefra::detail
