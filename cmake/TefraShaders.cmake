# GLSL -> SPIR-V at build time with glslangValidator.
# Output goes to ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders/<name>.spv next to the executables.

find_program(TEFRA_GLSLANG glslangValidator
    HINTS "$ENV{VULKAN_SDK}/bin" "$ENV{VULKAN_SDK}/Bin")
if(NOT TEFRA_GLSLANG)
    message(FATAL_ERROR "glslangValidator not found. Install the Vulkan SDK (Windows) or glslang-tools (Linux).")
endif()

# tefra_compile_shaders(TARGET <target> SOURCES <files...> INCLUDE_DIR <dir>)
function(tefra_compile_shaders)
    cmake_parse_arguments(ARG "" "TARGET;INCLUDE_DIR" "SOURCES" ${ARGN})
    set(out_dir ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders)
    file(GLOB_RECURSE common_deps CONFIGURE_DEPENDS ${ARG_INCLUDE_DIR}/*.glsl)
    set(outputs "")
    foreach(src ${ARG_SOURCES})
        get_filename_component(name ${src} NAME)
        set(out ${out_dir}/${name}.spv)
        add_custom_command(
            OUTPUT ${out}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${out_dir}
            COMMAND ${TEFRA_GLSLANG} -V --target-env vulkan1.2 -I${ARG_INCLUDE_DIR}
                    $<$<CONFIG:Debug>:-g> -o ${out} ${src}
            DEPENDS ${src} ${common_deps}
            COMMENT "GLSL ${name}"
            VERBATIM COMMAND_EXPAND_LISTS)
        list(APPEND outputs ${out})
    endforeach()
    add_custom_target(${ARG_TARGET} ALL DEPENDS ${outputs})
endfunction()
