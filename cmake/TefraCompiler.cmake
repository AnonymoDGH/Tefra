# Compiler flags shared by all Tefra engine targets (not applied to third_party).

add_library(tefra_compile_options INTERFACE)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(tefra_compile_options INTERFACE
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion
        -Wno-missing-field-initializers -Wnon-virtual-dtor -Wold-style-cast -Wcast-align
        -Wdouble-promotion -Wformat=2 -Wimplicit-fallthrough
        $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti>
    )
    if(TEFRA_WERROR)
        target_compile_options(tefra_compile_options INTERFACE -Werror)
    endif()
    # Baseline ISA: x86-64-v2 (SSE4.2/POPCNT). Never -march=native: the cloud has AVX-512, the reference PC does not.
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
        target_compile_options(tefra_compile_options INTERFACE -march=x86-64-v2)
    endif()
endif()

if(WIN32)
    target_compile_definitions(tefra_compile_options INTERFACE
        WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS __USE_MINGW_ANSI_STDIO=1)
endif()

if(TEFRA_ASAN)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
    add_link_options(-fsanitize=address,undefined)
endif()

if(MINGW)
    # Static runtime: the .exe runs on the reference PC without shipping MinGW DLLs.
    add_link_options(-static -static-libgcc -static-libstdc++)
endif()

# Helper to declare an engine library with common settings.
function(tefra_add_library name)
    cmake_parse_arguments(ARG "" "" "SOURCES;PUBLIC_DEPS;PRIVATE_DEPS" ${ARGN})
    add_library(${name} STATIC ${ARG_SOURCES})
    target_include_directories(${name} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
                                       PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(${name} PUBLIC ${ARG_PUBLIC_DEPS} PRIVATE tefra_compile_options ${ARG_PRIVATE_DEPS})
    if(TEFRA_UNITY)
        set_target_properties(${name} PROPERTIES UNITY_BUILD ON)
    endif()
endfunction()

function(tefra_add_executable name)
    cmake_parse_arguments(ARG "" "" "SOURCES;DEPS" ${ARGN})
    add_executable(${name} ${ARG_SOURCES})
    target_link_libraries(${name} PRIVATE tefra_compile_options ${ARG_DEPS})
    add_dependencies(${name} tefra_shaders)
endfunction()
