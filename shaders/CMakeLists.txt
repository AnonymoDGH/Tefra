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
        ${CMAKE_CURRENT_SOURCE_DIR}/tonemap.frag
        ${CMAKE_CURRENT_SOURCE_DIR}/skyview.frag
        ${CMAKE_CURRENT_SOURCE_DIR}/terrain.vert
        ${CMAKE_CURRENT_SOURCE_DIR}/terrain_shadow.vert
        ${CMAKE_CURRENT_SOURCE_DIR}/terrain.frag
        ${CMAKE_CURRENT_SOURCE_DIR}/veg_cull.comp
        ${CMAKE_CURRENT_SOURCE_DIR}/veg.vert
        ${CMAKE_CURRENT_SOURCE_DIR}/veg_shadow.vert
        ${CMAKE_CURRENT_SOURCE_DIR}/shadow_alpha.frag
        ${CMAKE_CURRENT_SOURCE_DIR}/water.vert
        ${CMAKE_CURRENT_SOURCE_DIR}/water.frag
        ${CMAKE_CURRENT_SOURCE_DIR}/taa.frag
        ${CMAKE_CURRENT_SOURCE_DIR}/gtao.frag
        ${CMAKE_CURRENT_SOURCE_DIR}/volumetric.frag)
