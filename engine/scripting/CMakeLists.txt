# C# scripting host (spec M3, ADR-013): loads the .NET runtime at run time with hostfxr. Works without .NET: init()
# then reports why and the engine runs without scripts.
tefra_add_library(tefra_scripting
    SOURCES src/script_host.cpp src/dotnet_host.cpp src/native_api.cpp
    PUBLIC_DEPS tefra_world
    PRIVATE_DEPS ${CMAKE_DL_LIBS})
