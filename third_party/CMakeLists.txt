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
endif()
