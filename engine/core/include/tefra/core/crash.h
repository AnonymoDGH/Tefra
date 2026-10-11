#pragma once

// Fatal-error reporting: on a crash (access violation, illegal instruction, abort...) the handler logs the cause, the
// faulting address and the module it belongs to (e.g. a GPU driver DLL), flushes the log file and exits. Call once,
// early in main(), after log_set_file().

namespace tefra {

void install_crash_handler();

} // namespace tefra
