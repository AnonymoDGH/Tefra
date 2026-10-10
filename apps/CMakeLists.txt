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
if(TEFRA_WITH_WINDOW)
    tefra_add_executable(tefra_viewer SOURCES viewer/main.cpp DEPS tefra_app)
endif()
