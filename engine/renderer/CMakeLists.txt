tefra_add_library(tefra_renderer
    SOURCES src/testpattern.cpp src/camera.cpp
    PUBLIC_DEPS tefra_rhi)
target_precompile_headers(tefra_renderer PRIVATE <volk.h>)
