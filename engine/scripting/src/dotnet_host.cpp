#include "dotnet_host.h"

#include "tefra/core/log.h"

#include <cstdlib>
#include <cstring>
#include <string_view>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace tefra::scripting::dotnet {

namespace fs = std::filesystem;

namespace {

#if defined(_WIN32)
constexpr const char* kHostfxrName = "hostfxr.dll";
#elif defined(__APPLE__)
constexpr const char* kHostfxrName = "libhostfxr.dylib";
#else
constexpr const char* kHostfxrName = "libhostfxr.so";
#endif

// Assembly-qualified names are ASCII in practice; widen them for the Windows (wchar_t) API when needed.
std::basic_string<hchar> to_hchar(std::string_view s) {
    std::basic_string<hchar> out;
    out.reserve(s.size());
    for (char c : s) out.push_back(static_cast<hchar>(static_cast<unsigned char>(c)));
    return out;
}

// Parses "8.0.31" (or "9.0.0-preview.7", whose suffix is ignored) into numeric components.
std::vector<long> version_key(const std::string& name) {
    std::vector<long> key;
    long current = 0;
    bool any = false;
    for (char c : name) {
        if (c >= '0' && c <= '9') {
            current = current * 10 + (c - '0');
            any = true;
        } else if (c == '.') {
            key.push_back(current);
            current = 0;
            any = false;
        } else {
            break;
        }
    }
    if (any) key.push_back(current);
    return key;
}

std::vector<fs::path> dotnet_roots() {
    std::vector<fs::path> roots;
    if (const char* env = std::getenv("DOTNET_ROOT"); env && *env) roots.emplace_back(env);
    roots.emplace_back("/usr/lib/dotnet");
    roots.emplace_back("/usr/share/dotnet");
    roots.emplace_back("/usr/local/share/dotnet");
    roots.emplace_back("C:/Program Files/dotnet");
    return roots;
}

// Opens a shared library and resolves one symbol. Keeps the library loaded for the life of the process.
void* open_library(const fs::path& lib) {
#if defined(_WIN32)
    return reinterpret_cast<void*>(LoadLibraryW(lib.c_str()));
#else
    return dlopen(lib.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
}

void* resolve(void* lib, const char* name) {
    if (!lib) return nullptr;
#if defined(_WIN32)
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(lib), name));
#else
    return dlsym(lib, name);
#endif
}

// Symbols come back as void*; the function-pointer type is recovered with memcpy, which stays warning-free under
// -Wpedantic (a reinterpret_cast between object and function pointers does not).
template <typename Fn> bool bind(void* symbol, Fn& out) {
    static_assert(sizeof(Fn) == sizeof(void*), "function pointer size");
    if (!symbol) return false;
    std::memcpy(&out, &symbol, sizeof(Fn));
    return true;
}

} // namespace

bool find_hostfxr(fs::path* library, std::string* err) {
    std::error_code ec;
    for (const fs::path& root : dotnet_roots()) {
        const fs::path fxr_dir = root / "host" / "fxr";
        if (!fs::is_directory(fxr_dir, ec)) continue;

        fs::path best;
        std::vector<long> best_key;
        for (auto it = fs::directory_iterator(fxr_dir, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
            if (!it->is_directory(ec)) continue;
            const std::string name = it->path().filename().string();
            std::vector<long> key = version_key(name);
            if (key.empty()) continue;
            const fs::path candidate = it->path() / kHostfxrName;
            if (!fs::is_regular_file(candidate, ec)) continue;
            if (best.empty() || key > best_key) {
                best = candidate;
                best_key = key;
            }
        }
        if (!best.empty()) {
            *library = best;
            return true;
        }
    }
    if (err) *err = "the .NET runtime was not found (install .NET 8 or set DOTNET_ROOT)";
    return false;
}

bool load_managed(const fs::path& dir, const TefraNativeApi* native, TefraManagedApi* managed, std::string* err) {
    std::error_code ec;
    const fs::path config = dir / "Tefra.Engine.runtimeconfig.json";
    const fs::path assembly = dir / "Tefra.Engine.dll";
    if (!fs::is_regular_file(assembly, ec)) {
        if (err) *err = "managed assembly not found: " + assembly.string() + " (build the managed target with dotnet)";
        return false;
    }
    if (!fs::is_regular_file(config, ec)) {
        if (err) *err = "runtime config not found: " + config.string();
        return false;
    }

    fs::path hostfxr_path;
    if (!find_hostfxr(&hostfxr_path, err)) return false;
    void* lib = open_library(hostfxr_path);
    if (!lib) {
        if (err) *err = "cannot load " + hostfxr_path.string();
        return false;
    }

    hostfxr_initialize_for_runtime_config_fn init_config = nullptr;
    hostfxr_get_runtime_delegate_fn get_delegate = nullptr;
    hostfxr_close_fn close_context = nullptr;
    if (!bind(resolve(lib, "hostfxr_initialize_for_runtime_config"), init_config) ||
        !bind(resolve(lib, "hostfxr_get_runtime_delegate"), get_delegate) ||
        !bind(resolve(lib, "hostfxr_close"), close_context)) {
        if (err) *err = "unexpected hostfxr library: missing hosting exports";
        return false;
    }

    // 0 = success; 1 and 2 mean the runtime was already started with compatible properties. Negative = failure.
    hostfxr_handle context = nullptr;
    const std::int32_t rc = init_config(config.native().c_str(), nullptr, &context);
    if (rc < 0 || context == nullptr) {
        if (err) *err = "hostfxr could not start the runtime (code " + std::to_string(rc) + ")";
        return false;
    }

    void* delegate_ptr = nullptr;
    load_assembly_and_get_function_pointer_fn load_fn = nullptr;
    const std::int32_t rc_delegate = get_delegate(context, kDelegateLoadAssemblyAndGetFunctionPointer, &delegate_ptr);
    const bool have_loader = rc_delegate >= 0 && bind(delegate_ptr, load_fn);
    if (!have_loader) {
        close_context(context);
        if (err) *err = "hostfxr did not return the assembly loader";
        return false;
    }

    // UNMANAGEDCALLERSONLY_METHOD: the method is an [UnmanagedCallersOnly] export, called without a delegate.
    const hchar* unmanaged_only = reinterpret_cast<const hchar*>(static_cast<std::intptr_t>(-1));
    const std::basic_string<hchar> assembly_path = assembly.native();
    const std::basic_string<hchar> type_name = to_hchar("Tefra.Bridge, Tefra.Engine");
    const std::basic_string<hchar> method_name = to_hchar("Init");
    void* init_ptr = nullptr;
    const std::int32_t rc_load =
        load_fn(assembly_path.c_str(), type_name.c_str(), method_name.c_str(), unmanaged_only, nullptr, &init_ptr);
    close_context(context);
    if (rc_load < 0 || init_ptr == nullptr) {
        if (err) *err = "cannot resolve Tefra.Bridge.Init (code " + std::to_string(rc_load) + ")";
        return false;
    }

    using init_fn = int(TEFRA_HOST_CALL*)(const TefraNativeApi*, TefraManagedApi*);
    init_fn init = nullptr;
    if (!bind(init_ptr, init)) {
        if (err) *err = "invalid entry point for Tefra.Bridge.Init";
        return false;
    }
    const int status = init(native, managed);
    if (status != 0 || managed->abi_version != TEFRA_SCRIPT_ABI_VERSION) {
        if (err) *err = "Tefra.Engine initialization failed (status " + std::to_string(status) + ")";
        return false;
    }
    return true;
}

} // namespace tefra::scripting::dotnet
