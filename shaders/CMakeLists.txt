tefra_compile_shaders(
    TARGET tefra_shaders
    INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}
    SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/fullscreen.vert
        ${CMAKE_CURRENT_SOURCE_DIR}/testpattern.frag
        ${CMAKE_CURRENT_SOURCE_DIR}/ibl_equirect_to_cube.comp
        ${CMAKE_CURRENT_SOURCE_DIR}/ibl_prefilter.comp
        ${CMAKE_CURRENT_SOURCE_DIR}/mesh.vert
        ${CMAKE_CURRENT_SOURCE_DIR}/gbuffer.frag
        ${CMAKE_CURRENT_SOURCE_DIR}/shadow.vert
        ${CMAKE_CURRENT_SOURCE_DIR}/lighting.frag
        ${CMAKE_CURRENT_SOURCE_DIR}/tonemap.frag)
