tefra_add_library(tefra_rendergraph
    SOURCES src/graph.cpp
    PUBLIC_DEPS tefra_rhi)
target_precompile_headers(tefra_rendergraph PRIVATE <volk.h>)
