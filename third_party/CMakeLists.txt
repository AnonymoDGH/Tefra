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
