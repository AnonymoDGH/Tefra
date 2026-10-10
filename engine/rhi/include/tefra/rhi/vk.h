#pragma once

// Vulkan access for engine code: volk (dynamic loader) + VMA configured for the Vulkan 1.2 baseline.

#include "tefra/core/assert.h"

#include <volk.h>

#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#define VMA_VULKAN_VERSION 1002000
#include <vk_mem_alloc.h>

namespace tefra::rhi {
const char* vk_result_str(VkResult r);
} // namespace tefra::rhi

#define TEFRA_VK_CHECK(expr)                                                                                           \
    do {                                                                                                               \
        const VkResult tefra_vk_r_ = (expr);                                                                           \
        if (tefra_vk_r_ != VK_SUCCESS) TEFRA_PANIC("%s -> %s", #expr, ::tefra::rhi::vk_result_str(tefra_vk_r_));       \
    } while (0)
