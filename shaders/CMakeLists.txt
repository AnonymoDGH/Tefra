tefra_compile_shaders(
    TARGET tefra_shaders
    INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}
    SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/fullscreen.vert
        ${CMAKE_CURRENT_SOURCE_DIR}/testpattern.frag)
