#pragma once

// Native <-> managed interop table for the C# scripting layer (spec M3, ADR-013).
//
// The C# side mirrors these structs in managed/Tefra.Engine/src/Native.cs. Keep both in sync and bump
// TEFRA_SCRIPT_ABI_VERSION on any change. Only plain C types cross the boundary: no exceptions, no C++ objects.
//
// Ownership: strings returned by native functions point into the DataModel and stay valid until the next call
// that mutates the model. The managed side copies them immediately.

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TEFRA_SCRIPT_ABI_VERSION 1u

// Log levels: same numbering as tefra::LogLevel.
enum TefraLogLevel {
    TEFRA_LOG_TRACE = 0,
    TEFRA_LOG_DEBUG = 1,
    TEFRA_LOG_INFO = 2,
    TEFRA_LOG_WARN = 3,
    TEFRA_LOG_ERROR = 4
};

// Change notifications queued by the native side while a script run is active.
enum TefraEventKind { TEFRA_EVENT_CHANGED = 1, TEFRA_EVENT_REMOVING = 2 };

// A property value in flat form. `type` is a tefra::world::PropType. Bool/Int/Float/Enum use n[0]; Vector3 and
// Color3 use n[0..2]; String uses str/str_len (UTF-8, not NUL-terminated semantics); Ref uses ref (0 = nil).
typedef struct TefraValue {
    int32_t type;
    uint32_t str_len;
    double n[3];
    const char* str;
    uint64_t ref;
} TefraValue;

typedef struct TefraEvent {
    uint32_t kind; // TefraEventKind
    uint32_t prop; // property index (Changed only)
    uint64_t id;   // instance id
} TefraEvent;

// Functions implemented by the host and handed to the managed side in Bridge.Init. Instance ids are u64; 0 is nil.
// Negative/zero results mean failure unless noted. All `const char*` arguments are NUL-terminated UTF-8.
typedef struct TefraNativeApi {
    uint32_t abi_version;
    int32_t (*get_prop)(uint64_t id, uint32_t index, TefraValue* out);
    int32_t (*set_prop)(uint64_t id, uint32_t index, const TefraValue* value); // 1 = ok, 0 = rejected
    int32_t (*prop_index)(const char* class_name, const char* prop_name);      // -1 if unknown
    int32_t (*prop_type)(const char* class_name, const char* prop_name);       // PropType, -1 if unknown
    const char* (*prop_name)(uint64_t id, uint32_t index);                     // nullptr if unknown
    const char* (*class_name)(uint64_t id);                                    // nullptr if the id is gone
    int32_t (*is_a)(uint64_t id, const char* class_name);
    uint64_t (*create)(const char* class_name, uint64_t parent, const char* name);
    int32_t (*destroy)(uint64_t id);
    int32_t (*set_parent)(uint64_t id, uint64_t parent); // parent 0 = detach
    uint64_t (*parent)(uint64_t id);
    int32_t (*child_count)(uint64_t id);
    uint64_t (*child_at)(uint64_t id, int32_t index);
    uint64_t (*find_child)(uint64_t id, const char* name);
    uint64_t (*service)(const char* name);
    uint64_t (*clone)(uint64_t id, uint64_t parent);
    uint64_t (*find_path)(const char* path);
    void (*log)(int32_t level, const char* message);
} TefraNativeApi;

// Functions exported by the managed side. The host fills nothing here; the managed side fills it in Init.
// compile: compiles every enabled Script without running it. Returns the number that compiled, -1 on fatal error.
// start:   compiles if needed, then calls Start() on every compiled script. Returns the number started, -1 on error.
// step:    advances Heartbeat, tasks and Update(dt). Returns 0.
// dispatch: delivers the change events queued since the last step.
// stop:    drops every script and unloads its assembly context.
typedef struct TefraManagedApi {
    uint32_t abi_version;
    int32_t (*compile)(void);
    int32_t (*start)(void);
    int32_t (*step)(float dt);
    void (*dispatch)(const TefraEvent* events, uint32_t count);
    void (*stop)(void);
} TefraManagedApi;

#ifdef __cplusplus
}
#endif
