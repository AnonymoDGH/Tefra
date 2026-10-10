#pragma once

#include "tefra/core/types.h"

namespace tefra {

enum class LogLevel : u8 { Trace, Debug, Info, Warn, Error };

void log_set_level(LogLevel level);
LogLevel log_level();
// Mirror log output to a file (in addition to stderr). nullptr closes it.
bool log_set_file(const char* path);
void log_write(LogLevel level, const char* fmt, ...) TEFRA_PRINTF(2, 3);

// Receives every message at or above the level filter, after formatting (no timestamp prefix). One sink at a
// time; nullptr removes it. Called under the logger's lock from any thread: it must not log.
using LogSinkFn = void (*)(void* user, LogLevel level, f64 seconds, const char* text);
void log_set_sink(LogSinkFn fn, void* user);

} // namespace tefra

#define TEFRA_LOG_TRACE(...) ::tefra::log_write(::tefra::LogLevel::Trace, __VA_ARGS__)
#define TEFRA_LOG_DEBUG(...) ::tefra::log_write(::tefra::LogLevel::Debug, __VA_ARGS__)
#define TEFRA_LOG_INFO(...) ::tefra::log_write(::tefra::LogLevel::Info, __VA_ARGS__)
#define TEFRA_LOG_WARN(...) ::tefra::log_write(::tefra::LogLevel::Warn, __VA_ARGS__)
#define TEFRA_LOG_ERROR(...) ::tefra::log_write(::tefra::LogLevel::Error, __VA_ARGS__)
