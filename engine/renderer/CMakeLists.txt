tefra_add_library(tefra_renderer
    SOURCES src/testpattern.cpp src/camera.cpp src/mesh.cpp src/scene.cpp src/gltf.cpp src/environment.cpp src/renderer.cpp src/atmosphere.cpp src/terrain.cpp src/vegetation.cpp src/water.cpp
    PUBLIC_DEPS tefra_rhi tefra_rendergraph
    PRIVATE_DEPS tefra_cgltf)
target_precompile_headers(tefra_renderer PRIVATE <volk.h>)
