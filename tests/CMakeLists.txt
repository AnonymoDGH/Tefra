# Unit tests (no GPU).
tefra_add_executable(tefra_unit_tests
    SOURCES unit/main.cpp unit/test_math.cpp unit/test_memory.cpp unit/test_jobs.cpp unit/test_json.cpp
            unit/test_stats.cpp unit/test_image.cpp unit/test_environment.cpp unit/test_foliage.cpp unit/test_palm.cpp unit/test_world.cpp unit/test_studio.cpp
    DEPS tefra_renderer tefra_world tefra_studio_core tefra_doctest)
add_test(NAME unit COMMAND tefra_unit_tests)
set_tests_properties(unit PROPERTIES LABELS "unit")

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
