# Unit tests (no GPU).
tefra_add_executable(tefra_unit_tests
    SOURCES unit/main.cpp unit/test_math.cpp unit/test_memory.cpp unit/test_jobs.cpp unit/test_json.cpp
            unit/test_stats.cpp unit/test_image.cpp unit/test_environment.cpp unit/test_foliage.cpp unit/test_palm.cpp unit/test_world.cpp unit/test_studio.cpp unit/test_projects.cpp unit/test_anim.cpp
            unit/test_mcp.cpp unit/test_physics.cpp unit/test_scripting.cpp unit/test_rbx.cpp unit/test_gameplay.cpp unit/test_landscape.cpp unit/test_audio.cpp unit/test_skin.cpp unit/test_character.cpp unit/test_particles.cpp
    DEPS tefra_renderer tefra_world tefra_studio_core tefra_runtime tefra_anim tefra_mcp tefra_physics tefra_scripting tefra_rbx tefra_doctest tefra_audio)
# The in-game UI needs Dear ImGui, which is only built with windowing support.
if(TEFRA_WITH_WINDOW)
    target_sources(tefra_unit_tests PRIVATE unit/test_ui.cpp)
    target_link_libraries(tefra_unit_tests PRIVATE tefra_ui tefra_imgui)
endif()
add_dependencies(tefra_unit_tests tefra_sfx_resources)
add_test(NAME unit COMMAND tefra_unit_tests)
set_tests_properties(unit PROPERTIES LABELS "unit")
target_compile_definitions(tefra_unit_tests PRIVATE TEFRA_SCRIPTING_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/scripting/fixtures"
    TEFRA_ASSET_ROOT="${CMAKE_SOURCE_DIR}/assets/cache")
if(TARGET tefra_managed)
    add_dependencies(tefra_unit_tests tefra_managed)
endif()

# Headless run of a place with scripts (tefra_run). Skipped (77) when the .NET runtime is missing.
add_test(NAME scripting_run COMMAND tefra_run
    --place ${CMAKE_CURRENT_SOURCE_DIR}/scripting/fixtures/spin.tefra --frames 60 --require-scripts
    --out ${CMAKE_BINARY_DIR}/tests/scripting_run_out.tefra)
set_tests_properties(scripting_run PROPERTIES LABELS "unit" SKIP_RETURN_CODE 77)

# Game runtime end to end (M3b): a place with a sensor, a kill brick that kills the dropped character, a respawn, a
# ModuleScript used by a Script, a LocalScript in PlayerGui, a script created during Play, an InputBegan from a synthetic
# key press, and a StarterCharacterScripts clone. --expect-file: every line of the file must appear in the log.
add_test(NAME scripting_game COMMAND tefra_run
    --place ${CMAKE_CURRENT_SOURCE_DIR}/scripting/fixtures/game.tefra --frames 600 --press E:120:3 --require-scripts
    --expect-file ${CMAKE_CURRENT_SOURCE_DIR}/scripting/fixtures/game.expect)
set_tests_properties(scripting_game PROPERTIES LABELS "scripting" SKIP_RETURN_CODE 77)

# Gameplay regressions (M3b): Workspace.Raycast from a TefraScript (also in Start), a respawned HUD that receives
# CharacterAdded, a kill brick and a sensor that kill the character, a destroyed script's handlers no longer running
# (the "E began" from the HUD is printed once per press), and StarterCharacterScripts. Output lines must appear.
add_test(NAME scripting_gameplay COMMAND tefra_run
    --place ${CMAKE_CURRENT_SOURCE_DIR}/scripting/fixtures/gameplay.tefra --frames 1800 --press E:300:3 --require-scripts
    --expect-file ${CMAKE_CURRENT_SOURCE_DIR}/scripting/fixtures/gameplay.expect)
set_tests_properties(scripting_gameplay PROPERTIES LABELS "scripting" SKIP_RETURN_CODE 77)

# Value types (CFrame, PivotTo on a Model, UDim2, sequences, Color3 HSV/hex, TefraRandom) evaluated in a script.
add_test(NAME scripting_mathtypes COMMAND tefra_run
    --place ${CMAKE_CURRENT_SOURCE_DIR}/scripting/fixtures/mathtypes.tefra --frames 3 --require-scripts
    --expect-file ${CMAKE_CURRENT_SOURCE_DIR}/scripting/fixtures/mathtypes.expect)
set_tests_properties(scripting_mathtypes PROPERTIES LABELS "scripting" SKIP_RETURN_CODE 77)
# Messaging and services (M3c): BindableEvent / BindableFunction, RemoteEvent / RemoteFunction loopback, Debris,
# DataStore and HttpService. The datastores go to the build tree, not to the user's application data.
add_test(NAME scripting_services COMMAND tefra_run
    --place ${CMAKE_CURRENT_SOURCE_DIR}/scripting/fixtures/services.tefra --frames 60 --require-scripts
    --expect-file ${CMAKE_CURRENT_SOURCE_DIR}/scripting/fixtures/services.expect)
set_tests_properties(scripting_services PROPERTIES LABELS "scripting" SKIP_RETURN_CODE 77
    ENVIRONMENT "TEFRA_DATASTORE_DIR=${CMAKE_BINARY_DIR}/tests/datastores")
# Attributes and tags (ABI 3): SetAttribute / GetAttribute / AttributeChanged / GetAttributeChangedSignal, AddTag /
# RemoveTag / GetTags, CollectionService.GetTagged and the InstanceAdded / InstanceRemoved signals of one tag.
add_test(NAME scripting_attributes COMMAND tefra_run
    --place ${CMAKE_CURRENT_SOURCE_DIR}/scripting/fixtures/attributes.tefra --frames 10 --require-scripts
    --expect-file ${CMAKE_CURRENT_SOURCE_DIR}/scripting/fixtures/attributes.expect)
set_tests_properties(scripting_attributes PROPERTIES LABELS "scripting" SKIP_RETURN_CODE 77)
if(TEFRA_ASAN)
    # The .NET runtime keeps its heap for the life of the process; suppress only its modules (see lsan.supp).
    set(_tefra_lsan "LSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/lsan.supp")
    set_tests_properties(unit scripting_run scripting_game scripting_gameplay scripting_mathtypes scripting_attributes
        PROPERTIES ENVIRONMENT "${_tefra_lsan}")
    set_tests_properties(scripting_services PROPERTIES ENVIRONMENT
        "${_tefra_lsan};TEFRA_DATASTORE_DIR=${CMAKE_BINARY_DIR}/tests/datastores")
endif()

# MCP end-to-end (ADR-015): starts tefra_mcp_demo, speaks Streamable HTTP to it, then drives the stdio bridge.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME mcp_demo
        COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/mcp/test_mcp_demo.py
                $<TARGET_FILE:tefra_mcp_demo> $<TARGET_FILE:tefra_mcp_bridge>)
    set_tests_properties(mcp_demo PROPERTIES LABELS "mcp")

    # The built-in sound effects are generated by scripts/gen_sfx.py; the committed WAVs must match it byte for byte.
    add_test(NAME sfx_generator
        COMMAND ${Python3_EXECUTABLE} -I ${PROJECT_SOURCE_DIR}/scripts/gen_sfx.py --check)
    set_tests_properties(sfx_generator PROPERTIES LABELS "unit")
endif()

# GPU integration tests (lavapipe in CI). Golden images live in tests/golden.
tefra_add_executable(tefra_gpu_tests
    SOURCES gpu/main.cpp gpu/test_testpattern.cpp gpu/test_landscape_gpu.cpp gpu/test_skin.cpp
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
