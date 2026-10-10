#pragma once

// Run-time loader for the .NET 8 runtime (spec M3, ADR-013). No nethost and no link-time dependency: libhostfxr is
// located and opened with dlopen / LoadLibrary, which works the same for Linux and MinGW on Windows.
//
// The declarations below are the public hosting API (hostfxr.h, coreclr_delegates.h of the dotnet/runtime repo,
// MIT licensed), checked against the 8.0.31 headers. Only what Tefra uses is declared.

#include "tefra/scripting/abi.h"

#include <cstdint>
#include <filesystem>
#include <string>

#if defined(_WIN32)
#define TEFRA_HOST_CALL __cdecl
#else
#define TEFRA_HOST_CALL
#endif

namespace tefra::scripting::dotnet {

#if defined(_WIN32)
using hchar = wchar_t;
#else
using hchar = char;
#endif

using hostfxr_handle = void*;

struct hostfxr_initialize_parameters {
    std::size_t size;
    const hchar* host_path;
    const hchar* dotnet_root;
};

using hostfxr_initialize_for_runtime_config_fn = std::int32_t(TEFRA_HOST_CALL*)(
    const hchar* runtime_config_path, const hostfxr_initialize_parameters* parameters, hostfxr_handle* context);
using hostfxr_get_runtime_delegate_fn = std::int32_t(TEFRA_HOST_CALL*)(hostfxr_handle context,
                                                                       std::int32_t delegate_type, void** delegate);
using hostfxr_close_fn = std::int32_t(TEFRA_HOST_CALL*)(hostfxr_handle context);

// hostfxr_delegate_type value hdt_load_assembly_and_get_function_pointer.
inline constexpr std::int32_t kDelegateLoadAssemblyAndGetFunctionPointer = 5;

using load_assembly_and_get_function_pointer_fn = std::int32_t(TEFRA_HOST_CALL*)(const hchar* assembly_path,
                                                                                 const hchar* type_name,
                                                                                 const hchar* method_name,
                                                                                 const hchar* delegate_type_name,
                                                                                 void* reserved, void** delegate);

// Finds the newest libhostfxr under <dotnet root>/host/fxr/<version>/. The root comes from DOTNET_ROOT, then the usual
// install locations. Returns false with a message in *err when no runtime is installed.
bool find_hostfxr(std::filesystem::path* library, std::string* err);

// Starts the runtime for <dir>/Tefra.Engine.runtimeconfig.json and calls Tefra.Bridge.Init from <dir>/Tefra.Engine.dll.
// On success, *managed holds the managed entry points.
bool load_managed(const std::filesystem::path& dir, const TefraNativeApi* native, TefraManagedApi* managed,
                  std::string* err);

} // namespace tefra::scripting::dotnet
