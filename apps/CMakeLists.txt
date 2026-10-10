add_library(tefra_app STATIC common/runner.cpp common/options.cpp common/scenes.cpp)
target_include_directories(tefra_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/common)
target_link_libraries(tefra_app PUBLIC tefra_renderer PRIVATE tefra_compile_options)
target_compile_definitions(tefra_app PUBLIC
    TEFRA_VERSION="${PROJECT_VERSION}" TEFRA_GIT_SHA="${TEFRA_GIT_SHA}" TEFRA_BUILD_TYPE="$<CONFIG>")
if(TEFRA_WITH_WINDOW)
    target_link_libraries(tefra_app PUBLIC tefra_platform)
    target_compile_definitions(tefra_app PUBLIC TEFRA_WITH_WINDOW=1)
endif()

# MCP (ADR-015): headless demo server, and the stdio <-> HTTP bridge for clients that only speak stdio.
tefra_add_executable(tefra_mcp_demo SOURCES mcp_demo/main.cpp DEPS tefra_mcp)
tefra_add_executable(tefra_mcp_bridge SOURCES mcp_bridge/main.cpp DEPS tefra_mcp)
set_target_properties(tefra_mcp_bridge PROPERTIES OUTPUT_NAME tefra_mcp)

tefra_add_executable(tefra_info SOURCES info/main.cpp DEPS tefra_app)
tefra_add_executable(tefra_bench SOURCES bench/main.cpp DEPS tefra_app)
tefra_add_executable(tefra_avatar_demo SOURCES avatar_demo/main.cpp DEPS tefra_app tefra_anim tefra_world)
tefra_add_executable(tefra_physics_demo SOURCES physics_demo/main.cpp DEPS tefra_app tefra_physics)
if(TEFRA_WITH_WINDOW)
    tefra_add_executable(tefra_viewer SOURCES viewer/main.cpp DEPS tefra_app)
endif()
# Tefra Studio: editing core (session, undo history; no window) and the editor application.
add_library(tefra_studio_core STATIC studio/src/session.cpp studio/src/history.cpp)
target_include_directories(tefra_studio_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/studio/include)
target_link_libraries(tefra_studio_core PUBLIC tefra_world PRIVATE tefra_compile_options)
if(TEFRA_WITH_WINDOW)
    tefra_add_executable(tefra_studio
        SOURCES studio/main.cpp studio/src/app.cpp studio/src/ui.cpp
        DEPS tefra_studio_core tefra_platform tefra_imgui tefra_imguizmo)
endif()
tefra_add_executable(tefra_run SOURCES run/main.cpp DEPS tefra_scripting tefra_world)
if(TARGET tefra_managed)
    add_dependencies(tefra_run tefra_managed)
endif()
