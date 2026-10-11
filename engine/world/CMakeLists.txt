tefra_add_library(tefra_world
    SOURCES src/datamodel.cpp src/classes.cpp src/scene_sync.cpp src/primitives.cpp src/environment_sync.cpp
    PUBLIC_DEPS tefra_core tefra_renderer)
