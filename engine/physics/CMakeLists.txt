tefra_add_library(tefra_physics
    SOURCES src/physics_world.cpp src/character.cpp
    PUBLIC_DEPS tefra_world
    PRIVATE_DEPS Jolt)
