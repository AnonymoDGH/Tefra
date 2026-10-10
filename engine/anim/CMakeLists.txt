tefra_add_library(tefra_anim
    SOURCES src/clip.cpp src/builtin_clips.cpp src/avatar.cpp src/animator.cpp src/skeleton.cpp
    PUBLIC_DEPS tefra_world tefra_renderer)
