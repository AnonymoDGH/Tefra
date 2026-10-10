cmake_minimum_required(VERSION 3.25)

project(Tefra VERSION 0.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

option(TEFRA_WITH_WINDOW "Build windowing support (GLFW) and the viewer" ON)
option(TEFRA_BUILD_TESTS "Build tests" ON)
option(TEFRA_WERROR "Treat warnings as errors in engine code" OFF)
option(TEFRA_ASAN "Enable AddressSanitizer + UndefinedBehaviorSanitizer" OFF)
option(TEFRA_UNITY "Unity build for engine libraries" OFF)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(TefraCompiler)
include(TefraShaders)

# ccache if available (big win on the 2-core reference PC).
find_program(TEFRA_CCACHE ccache)
if(TEFRA_CCACHE)
    set(CMAKE_C_COMPILER_LAUNCHER ${TEFRA_CCACHE})
    set(CMAKE_CXX_COMPILER_LAUNCHER ${TEFRA_CCACHE})
endif()

# Git revision for bench reports.
find_package(Git QUIET)
set(TEFRA_GIT_SHA "unknown")
if(GIT_FOUND AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/.git)
    execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short=12 HEAD
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE TEFRA_GIT_SHA OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
endif()

# C# runtime for scripts (managed/Tefra.Engine, .NET 8). Optional: without dotnet the engine builds and runs without
# scripting (ScriptHost::init reports it). The output lands in bin/managed next to the executables.
option(TEFRA_MANAGED "Build the C# scripting assembly with dotnet (needs the .NET 8 SDK)" ON)
if(TEFRA_MANAGED)
    find_program(TEFRA_DOTNET dotnet)
    if(TEFRA_DOTNET)
        add_custom_target(tefra_managed ALL
            COMMAND ${CMAKE_COMMAND} -E env DOTNET_CLI_TELEMETRY_OPTOUT=1 DOTNET_NOLOGO=1
                    ${TEFRA_DOTNET} build "${CMAKE_CURRENT_SOURCE_DIR}/managed/Tefra.Engine/Tefra.Engine.csproj"
                    -c Release -o "${CMAKE_BINARY_DIR}/bin/managed" -nologo -v:minimal
            BYPRODUCTS "${CMAKE_BINARY_DIR}/bin/managed/Tefra.Engine.dll"
            COMMENT "Building managed Tefra.Engine (.NET 8)"
            VERBATIM)
    else()
        message(STATUS "dotnet not found: C# scripting assembly not built (scripts disabled at run time)")
    endif()
endif()

add_subdirectory(third_party)
add_subdirectory(engine)
add_subdirectory(shaders)
add_subdirectory(apps)

if(TEFRA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
