#pragma once

// Session state shared by ScriptHost and the native functions that managed code calls. The native table is static
// (one per process), so the functions reach the active DataModel and log sink through this module.

#include "tefra/core/log.h"
#include "tefra/scripting/abi.h"
#include "tefra/scripting/script_host.h"
#include "tefra/world/datamodel.h"

#include <string_view>

namespace tefra::scripting::detail {

// Points the native functions at the model and log sink of the running session (nullptr when stopped).
void set_session(world::DataModel* model, const LogCallback* log);

// The function table handed to managed code in Init.
const TefraNativeApi* native_table();

// Forwards a message from managed code to the session log, or to the engine log when no session is active.
void session_log(LogLevel level, std::string_view message);

} // namespace tefra::scripting::detail
