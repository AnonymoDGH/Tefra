#include "tefra/core/crash.h"

#include "tefra/core/log.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>

#if defined(_WIN32)
// clang-format off
#include <windows.h>
// clang-format on
#endif

namespace tefra {

namespace {
void on_signal(int sig) {
    const char* name = sig == SIGSEGV   ? "SIGSEGV (invalid memory access)"
                       : sig == SIGABRT ? "SIGABRT (abort)"
                       : sig == SIGFPE  ? "SIGFPE"
                       : sig == SIGILL  ? "SIGILL (illegal instruction)"
                                        : "signal";
    log_write(LogLevel::Error, "FATAL: %s", name);
    log_set_file(nullptr); // flush and close
    std::fflush(stderr);
    std::_Exit(3);
}

#if defined(_WIN32)
LONG WINAPI on_exception(EXCEPTION_POINTERS* info) {
    const EXCEPTION_RECORD* r = info->ExceptionRecord;
    char module[MAX_PATH] = "?";
    HMODULE mod = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           static_cast<LPCSTR>(r->ExceptionAddress), &mod))
        GetModuleFileNameA(mod, module, MAX_PATH);
    const unsigned long long offset =
        mod ? static_cast<unsigned long long>(static_cast<const char*>(r->ExceptionAddress) -
                                              reinterpret_cast<const char*>(mod))
            : 0ull;
    log_write(LogLevel::Error, "FATAL: exception 0x%08lX at %p (%s + 0x%llX)",
              static_cast<unsigned long>(r->ExceptionCode), r->ExceptionAddress, module, offset);
    if (r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && r->NumberParameters >= 2)
        log_write(LogLevel::Error, "FATAL: %s address %p", r->ExceptionInformation[0] ? "writing" : "reading",
                  reinterpret_cast<void*>(r->ExceptionInformation[1]));
    log_set_file(nullptr);
    std::fflush(stderr);
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif
} // namespace

void install_crash_handler() {
#if defined(_WIN32)
    SetUnhandledExceptionFilter(on_exception);
    std::signal(SIGABRT, on_signal);
#else
    std::signal(SIGSEGV, on_signal);
    std::signal(SIGABRT, on_signal);
    std::signal(SIGFPE, on_signal);
    std::signal(SIGILL, on_signal);
#endif
}

} // namespace tefra
