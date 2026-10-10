# Unit tests (no GPU).
tefra_add_executable(tefra_unit_tests
    SOURCES unit/main.cpp unit/test_math.cpp unit/test_memory.cpp unit/test_jobs.cpp unit/test_json.cpp
            unit/test_stats.cpp unit/test_image.cpp unit/test_environment.cpp unit/test_foliage.cpp unit/test_palm.cpp unit/test_world.cpp unit/test_studio.cpp unit/test_projects.cpp unit/test_anim.cpp
            unit/test_mcp.cpp unit/test_physics.cpp unit/test_scripting.cpp unit/test_landscape.cpp
    DEPS tefra_renderer tefra_world tefra_studio_core tefra_runtime tefra_anim tefra_mcp tefra_physics tefra_scripting tefra_doctest)
add_test(NAME unit COMMAND tefra_unit_tests)
set_tests_properties(unit PROPERTIES LABELS "unit")
target_compile_definitions(tefra_unit_tests PRIVATE TEFRA_SCRIPTING_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/scripting/fixtures")
if(TARGET tefra_managed)
    add_dependencies(tefra_unit_tests tefra_managed)
endif()

# Headless run of a place with scripts (tefra_run). Skipped (77) when the .NET runtime is missing.
add_test(NAME scripting_run COMMAND tefra_run
    --place ${CMAKE_CURRENT_SOURCE_DIR}/scripting/fixtures/spin.tefra --frames 60 --require-scripts
    --out ${CMAKE_BINARY_DIR}/tests/scripting_run_out.tefra)
set_tests_properties(scripting_run PROPERTIES LABELS "unit" SKIP_RETURN_CODE 77)
if(TEFRA_ASAN)
    # The .NET runtime keeps its heap for the life of the process; suppress only its modules (see lsan.supp).
    set(_tefra_lsan "LSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/lsan.supp")
    set_tests_properties(unit scripting_run PROPERTIES ENVIRONMENT "${_tefra_lsan}")
endif()

# MCP end-to-end (ADR-015): starts tefra_mcp_demo, speaks Streamable HTTP to it, then drives the stdio bridge.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME mcp_demo
        COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/mcp/test_mcp_demo.py
                $<TARGET_FILE:tefra_mcp_demo> $<TARGET_FILE:tefra_mcp_bridge>)
    set_tests_properties(mcp_demo PROPERTIES LABELS "mcp")
endif()

# GPU integration tests (lavapipe in CI). Golden images live in tests/golden.
tefra_add_executable(tefra_gpu_tests
    SOURCES gpu/main.cpp gpu/test_testpattern.cpp
    DEPS tefra_app tefra_doctest)
target_compile_definitions(tefra_gpu_tests PRIVATE TEFRA_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")
add_test(NAME gpu COMMAND tefra_gpu_tests)
set_tests_properties(gpu PROPERTIES LABELS "gpu")
if(TEFRA_ASAN)
    # Mesa/LLVM keep allocations in threads of unloaded modules; leak checks stay on for unit tests.
    set_tests_properties(gpu PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0")
endif()
# doctest::Approx works in double; float comparisons in tests are intentional. Source-level options are
# placed after the interface warning flags, so this one wins.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    get_target_property(_unit_srcs tefra_unit_tests SOURCES)
    get_target_property(_gpu_srcs tefra_gpu_tests SOURCES)
    set_source_files_properties(${_unit_srcs} ${_gpu_srcs} PROPERTIES COMPILE_OPTIONS -Wno-double-promotion)
endif()
