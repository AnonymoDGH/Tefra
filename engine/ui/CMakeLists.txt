tefra_add_library(tefra_ui
    SOURCES src/imgui_input.cpp src/game_gui.cpp
    PUBLIC_DEPS tefra_world
    PRIVATE_DEPS tefra_imgui)
