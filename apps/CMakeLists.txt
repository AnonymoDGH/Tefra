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
add_library(tefra_studio_core STATIC studio/src/session.cpp studio/src/history.cpp studio/src/projects.cpp
    studio/src/templates.cpp)
target_include_directories(tefra_studio_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/studio/include)
target_link_libraries(tefra_studio_core PUBLIC tefra_world PRIVATE tefra_compile_options)
if(TEFRA_WITH_WINDOW)
    # The editor shell is a library shared by tefra_studio and tefra_player (player mode: the game fills the window).
    add_library(tefra_studio_app STATIC
        studio/src/app.cpp studio/src/ui.cpp studio/src/play.cpp studio/src/home.cpp studio/src/export.cpp
        studio/src/import.cpp studio/src/file_dialog.cpp)
    target_link_libraries(tefra_studio_app
        PUBLIC tefra_studio_core tefra_platform tefra_imgui tefra_imguizmo tefra_runtime tefra_audio tefra_ui tefra_mcp
        PRIVATE tefra_compile_options)
    target_compile_definitions(tefra_studio_app PRIVATE TEFRA_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
                                                        TEFRA_VERSION="${PROJECT_VERSION}")
    if(WIN32)
        target_link_libraries(tefra_studio_app PRIVATE comdlg32 shell32 ole32)
    endif()
    tefra_add_executable(tefra_studio SOURCES studio/main.cpp DEPS tefra_studio_app)
    tefra_add_executable(tefra_player SOURCES player/main.cpp DEPS tefra_studio_app)
    # Fonts and templates next to the executables (exports copy them from there).
    add_custom_target(tefra_studio_resources
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/studio/resources
                $<TARGET_FILE_DIR:tefra_studio>/resources)
    add_dependencies(tefra_studio tefra_studio_resources)
    add_dependencies(tefra_player tefra_studio_resources)
    if(TARGET tefra_managed)
        add_dependencies(tefra_studio tefra_managed)
        add_dependencies(tefra_player tefra_managed)
    endif()
endif()
tefra_add_executable(tefra_run SOURCES run/main.cpp DEPS tefra_scripting tefra_world tefra_runtime)
if(TARGET tefra_managed)
    add_dependencies(tefra_run tefra_managed)
endif()
