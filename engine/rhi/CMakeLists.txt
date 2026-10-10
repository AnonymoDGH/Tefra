tefra_add_library(tefra_rhi
    SOURCES
        src/device.cpp src/resources.cpp src/commands.cpp src/swapchain.cpp src/frame.cpp
        src/gpu_timer.cpp src/pipeline.cpp src/readback.cpp src/vma_impl.cpp
    PUBLIC_DEPS tefra_core tefra_volk tefra_vma)
# The VMA implementation TU has its own warning pragmas; keep it out of unity batches.
set_source_files_properties(src/vma_impl.cpp PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
target_precompile_headers(tefra_rhi PRIVATE <volk.h>)
