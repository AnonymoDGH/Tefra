#pragma once

#include "tefra/core/types.h"

namespace tefra::detail {
[[noreturn]] void panic(const char* file, int line, const char* fmt, ...) TEFRA_PRINTF(3, 4);
} // namespace tefra::detail

// Always-on fatal error with message.
#define TEFRA_PANIC(...) ::tefra::detail::panic(__FILE__, __LINE__, __VA_ARGS__)

// Always-on check (also in Release).
#define TEFRA_VERIFY(cond)                                                                                             \
    do {                                                                                                               \
        if (!(cond)) TEFRA_PANIC("verify failed: %s", #cond);                                                          \
    } while (0)

// Debug-only check.
#if defined(NDEBUG)
#define TEFRA_ASSERT(cond) ((void)sizeof(cond))
#else
#define TEFRA_ASSERT(cond)                                                                                             \
    do {                                                                                                               \
        if (!(cond)) TEFRA_PANIC("assert failed: %s", #cond);                                                          \
    } while (0)
#endif
