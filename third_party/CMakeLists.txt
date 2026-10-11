# Vendored third-party libraries. Warnings from these are not our concern: compile them as SYSTEM.

# --- Vulkan headers -------------------------------------------------------------------------------
add_library(tefra_vulkan_headers INTERFACE)
target_include_directories(tefra_vulkan_headers SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/vulkan-headers/include)
target_compile_definitions(tefra_vulkan_headers INTERFACE VK_NO_PROTOTYPES)
if(WIN32)
    target_compile_definitions(tefra_vulkan_headers INTERFACE VK_USE_PLATFORM_WIN32_KHR)
endif()

# --- volk (Vulkan loader) -------------------------------------------------------------------------
add_library(tefra_volk STATIC volk/volk.c)
target_include_directories(tefra_volk SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/volk)
target_link_libraries(tefra_volk PUBLIC tefra_vulkan_headers ${CMAKE_DL_LIBS})

# --- VMA (header-only; implementation compiled inside rhi) ----------------------------------------
add_library(tefra_vma INTERFACE)
target_include_directories(tefra_vma SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/vma/include)

# --- stb -------------------------------------------------------------------------------------------
add_library(tefra_stb STATIC stb/stb_impl.c)
target_include_directories(tefra_stb SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/stb)

# --- cgltf (glTF 2.0 loader) ----------------------------------------------------------------------
add_library(tefra_cgltf STATIC cgltf/cgltf_impl.c)
target_include_directories(tefra_cgltf SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/cgltf)

# --- zstd decompressor (BSD-3, v1.5.7) ------------------------------------------------------------
# Single-file amalgamation of the official decoder (build/single_file_libs/zstddeclib-in.c, combined without legacy
# formats). Used by engine/rbx for zstd-compressed chunks of .rbxl/.rbxm files (ADR-017).
add_library(tefra_zstd STATIC zstd/zstddeclib.c)
target_include_directories(tefra_zstd SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/zstd)

# --- doctest ---------------------------------------------------------------------------------------
add_library(tefra_doctest INTERFACE)
target_include_directories(tefra_doctest SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/doctest)

# --- GLFW ------------------------------------------------------------------------------------------
if(TEFRA_WITH_WINDOW)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    if(UNIX AND NOT APPLE)
        find_package(PkgConfig QUIET)
        set(_tefra_wayland OFF)
        if(PkgConfig_FOUND)
            pkg_check_modules(TEFRA_WL QUIET wayland-client wayland-cursor wayland-egl xkbcommon)
            find_program(TEFRA_WL_SCANNER wayland-scanner)
            if(TEFRA_WL_FOUND AND TEFRA_WL_SCANNER)
                set(_tefra_wayland ON)
            endif()
        endif()
        set(GLFW_BUILD_WAYLAND ${_tefra_wayland} CACHE BOOL "" FORCE)
    endif()
    add_subdirectory(glfw EXCLUDE_FROM_ALL)
    get_target_property(_glfw_inc glfw INTERFACE_INCLUDE_DIRECTORIES)
    set_target_properties(glfw PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_glfw_inc}")

    # --- Dear ImGui (docking branch, MIT) with the GLFW and Vulkan backends, used by Tefra Studio (ADR-011).
    # The Vulkan backend loads its functions through volk (IMGUI_IMPL_VULKAN_USE_VOLK), like the rest of the engine.
    add_library(tefra_imgui STATIC
        imgui/imgui.cpp imgui/imgui_draw.cpp imgui/imgui_tables.cpp imgui/imgui_widgets.cpp
        imgui/backends/imgui_impl_glfw.cpp imgui/backends/imgui_impl_vulkan.cpp)
    target_include_directories(tefra_imgui SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/imgui
                                                         ${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends)
    target_compile_definitions(tefra_imgui PUBLIC IMGUI_IMPL_VULKAN_USE_VOLK GLFW_INCLUDE_NONE)
    target_link_libraries(tefra_imgui PUBLIC tefra_volk glfw)

    # --- ImGuizmo (MIT): transform gizmos for Dear ImGui.
    add_library(tefra_imguizmo STATIC imguizmo/src/ImGuizmo.cpp)
    target_include_directories(tefra_imguizmo SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/imguizmo/src)
    target_link_libraries(tefra_imguizmo PUBLIC tefra_imgui)
endif()

# --- Jolt Physics (MIT, v5.6.0; ADR-014) ----------------------------------------------------------------------
# Only Jolt/ is vendored (not its Build/, samples or tests). Jolt/Jolt.cmake declares the static library from its
# source list; the options below select the features: no Vulkan/DX12/Metal, no profiler or debug renderer, and SIMD
# up to SSE4.2 + POPCNT (x86-64-v2, like the rest of the engine): no AVX, BMI, LZCNT, F16C or FMA. Cross-platform
# determinism stays off. Warnings from Jolt are isolated: no -Werror and SYSTEM include directories.
set(PHYSICS_REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/jolt)
set(OBJECT_LAYER_BITS 16)
set(JPH_USE_VK OFF)
set(JPH_USE_DX12 OFF)
set(JPH_USE_MTL OFF)
set(JPH_USE_CPU_COMPUTE OFF)
set(USE_AVX512 OFF)
set(USE_AVX2 OFF)
set(USE_AVX OFF)
set(USE_SSE4_1 ON)
set(USE_SSE4_2 ON)
set(USE_LZCNT OFF)
set(USE_TZCNT OFF)
set(USE_F16C OFF)
set(USE_FMADD OFF)
set(ENABLE_OBJECT_STREAM OFF)
set(DEBUG_RENDERER_IN_DEBUG_AND_RELEASE OFF)
set(DEBUG_RENDERER_IN_DISTRIBUTION OFF)
set(PROFILER_IN_DEBUG_AND_RELEASE OFF)
set(PROFILER_IN_DISTRIBUTION OFF)
set(CROSS_PLATFORM_DETERMINISTIC OFF)
set(DOUBLE_PRECISION OFF)
set(TRACK_BROADPHASE_STATS OFF)
set(TRACK_NARROWPHASE_STATS OFF)
set(JPH_TRACK_SIMULATION_STATS OFF)
set(DISABLE_CUSTOM_ALLOCATOR OFF)
set(USE_STD_VECTOR OFF)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(USE_ASSERTS ON) # Jolt asserts in debug builds; the physics module installs AssertFailed and Trace.
else()
    set(USE_ASSERTS OFF)
endif()
find_package(Threads REQUIRED)
include(${PHYSICS_REPO_ROOT}/Jolt/Jolt.cmake)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    # The engine forbids exceptions and RTTI; Jolt does not need them either (its own defaults).
    target_compile_options(Jolt PUBLIC -fno-exceptions -fno-rtti)
endif()
target_link_libraries(Jolt PUBLIC Threads::Threads)
set_target_properties(Jolt PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${PHYSICS_REPO_ROOT}")

# --- miniaudio (MIT-0 or public domain) + stb_vorbis (public domain or MIT) ----------
# One translation unit: stb_vorbis first, then miniaudio (WAV, FLAC, MP3, Vorbis decoders; WASAPI / DirectSound /
# ALSA / PulseAudio loaded at run time, so no system libraries are linked on Windows).
add_library(tefra_miniaudio STATIC miniaudio/miniaudio_impl.c)
target_include_directories(tefra_miniaudio SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/miniaudio)
if(UNIX AND NOT APPLE)
    find_library(TEFRA_MATH_LIB m)
    target_link_libraries(tefra_miniaudio PUBLIC ${CMAKE_DL_LIBS} Threads::Threads)
    if(TEFRA_MATH_LIB)
        target_link_libraries(tefra_miniaudio PUBLIC ${TEFRA_MATH_LIB})
    endif()
endif()
