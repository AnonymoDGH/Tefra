tefra_add_library(tefra_platform
    SOURCES src/window.cpp
    PUBLIC_DEPS tefra_core tefra_volk
    PRIVATE_DEPS glfw)
