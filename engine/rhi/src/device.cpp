#include "tefra/rhi/device.h"

#include "tefra/core/log.h"
#include "tefra/core/math.h"

#include <atomic>
#include <cstdio>
#include <cstring>

namespace tefra::rhi {
namespace {

std::atomic<u32> g_validation_errors{0};
std::atomic<u32> g_validation_warnings{0};

VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                              VkDebugUtilsMessageTypeFlagsEXT type,
                                              const VkDebugUtilsMessengerCallbackDataEXT* data, void*) {
    const char* msg = data && data->pMessage ? data->pMessage : "(null)";
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        g_validation_errors.fetch_add(1, std::memory_order_relaxed);
        TEFRA_LOG_ERROR("[vulkan] %s", msg);
    } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        g_validation_warnings.fetch_add(1, std::memory_order_relaxed);
        TEFRA_LOG_WARN("[vulkan%s] %s", (type & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) ? "/perf" : "", msg);
    } else {
        TEFRA_LOG_DEBUG("[vulkan] %s", msg);
    }
    return VK_FALSE;
}

bool has_extension(const std::vector<VkExtensionProperties>& exts, const char* name) {
    for (const auto& e : exts)
        if (std::strcmp(e.extensionName, name) == 0) return true;
    return false;
}

bool has_layer(const char* name) {
    u32 count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> layers(count);
    vkEnumerateInstanceLayerProperties(&count, layers.data());
    for (const auto& l : layers)
        if (std::strcmp(l.layerName, name) == 0) return true;
    return false;
}

std::vector<VkExtensionProperties> instance_extensions(const char* layer = nullptr) {
    u32 count = 0;
    vkEnumerateInstanceExtensionProperties(layer, &count, nullptr);
    std::vector<VkExtensionProperties> exts(count);
    vkEnumerateInstanceExtensionProperties(layer, &count, exts.data());
    return exts;
}

std::vector<VkExtensionProperties> device_extensions(VkPhysicalDevice pd) {
    u32 count = 0;
    vkEnumerateDeviceExtensionProperties(pd, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> exts(count);
    vkEnumerateDeviceExtensionProperties(pd, nullptr, &count, exts.data());
    return exts;
}

std::string version_string(u32 v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%u.%u.%u", VK_API_VERSION_MAJOR(v), VK_API_VERSION_MINOR(v),
                  VK_API_VERSION_PATCH(v));
    return buf;
}

const char* type_string(VkPhysicalDeviceType t) {
    switch (t) {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return "discrete";
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "integrated";
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return "virtual";
    case VK_PHYSICAL_DEVICE_TYPE_CPU: return "cpu";
    default: return "other";
    }
}

int type_score(VkPhysicalDeviceType t) {
    switch (t) {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return 4;
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return 3;
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return 2;
    case VK_PHYSICAL_DEVICE_TYPE_CPU: return 1;
    default: return 0;
    }
}

// All feature structs we query, chained together.
struct FeatureChain {
    VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    VkPhysicalDeviceVulkan11Features v11{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
    VkPhysicalDeviceVulkan12Features v12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceSynchronization2FeaturesKHR sync2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR};

    void link(bool with_sync2) {
        f2.pNext = &v11;
        v11.pNext = &v12;
        v12.pNext = with_sync2 ? &sync2 : nullptr;
        sync2.pNext = nullptr;
    }
};

struct AdapterQuery {
    VkPhysicalDeviceProperties props{};
    VkPhysicalDeviceDriverProperties driver{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
    FeatureChain feats;
    std::vector<VkExtensionProperties> exts;
    VkPhysicalDeviceMemoryProperties mem{};
};

AdapterQuery query_adapter(VkPhysicalDevice pd) {
    AdapterQuery q;
    VkPhysicalDeviceProperties2 p2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    q.exts = device_extensions(pd);
    vkGetPhysicalDeviceProperties(pd, &q.props);
    if (q.props.apiVersion >= VK_API_VERSION_1_2) {
        p2.pNext = &q.driver;
        vkGetPhysicalDeviceProperties2(pd, &p2);
        q.feats.link(has_extension(q.exts, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME));
        vkGetPhysicalDeviceFeatures2(pd, &q.feats.f2);
    }
    vkGetPhysicalDeviceMemoryProperties(pd, &q.mem);
    return q;
}

// Spec ADR-003 baseline: everything both reference GPUs (HD 520, R5 M330) support and we rely on.
void check_baseline(const AdapterQuery& q, bool need_swapchain, std::vector<std::string>& missing) {
    if (q.props.apiVersion < VK_API_VERSION_1_2) {
        missing.push_back("Vulkan 1.2");
        return;
    }
    auto req = [&](bool ok, const char* name) {
        if (!ok) missing.push_back(name);
    };
    const auto& f = q.feats.f2.features;
    const auto& v12 = q.feats.v12;
    req(has_extension(q.exts, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME) && q.feats.sync2.synchronization2,
        "VK_KHR_synchronization2");
    if (need_swapchain) req(has_extension(q.exts, VK_KHR_SWAPCHAIN_EXTENSION_NAME), "VK_KHR_swapchain");
    req(f.samplerAnisotropy, "samplerAnisotropy");
    req(f.textureCompressionBC, "textureCompressionBC");
    req(f.multiDrawIndirect, "multiDrawIndirect");
    req(f.depthClamp, "depthClamp");
    req(f.imageCubeArray, "imageCubeArray");
    req(v12.descriptorIndexing, "descriptorIndexing");
    req(v12.runtimeDescriptorArray, "runtimeDescriptorArray");
    req(v12.descriptorBindingPartiallyBound, "descriptorBindingPartiallyBound");
    req(v12.descriptorBindingVariableDescriptorCount, "descriptorBindingVariableDescriptorCount");
    req(v12.descriptorBindingSampledImageUpdateAfterBind, "descriptorBindingSampledImageUpdateAfterBind");
    req(v12.shaderSampledImageArrayNonUniformIndexing, "shaderSampledImageArrayNonUniformIndexing");
    req(v12.bufferDeviceAddress, "bufferDeviceAddress");
    req(v12.timelineSemaphore, "timelineSemaphore");
    req(v12.drawIndirectCount, "drawIndirectCount");
    req(v12.imagelessFramebuffer, "imagelessFramebuffer");
    req(q.props.limits.maxPushConstantsSize >= 128, "maxPushConstantsSize >= 128");
    req(q.props.limits.maxBoundDescriptorSets >= 4, "maxBoundDescriptorSets >= 4");
}

AdapterInfo make_adapter_info(u32 index, const AdapterQuery& q, bool need_swapchain) {
    AdapterInfo a;
    a.index = index;
    a.name = q.props.deviceName;
    a.vendor_id = q.props.vendorID;
    a.device_id = q.props.deviceID;
    a.type = type_string(q.props.deviceType);
    a.api_version = version_string(q.props.apiVersion);
    a.driver_name = q.driver.driverName;
    a.driver_info = q.driver.driverInfo;
    a.software = q.props.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU;
    for (u32 i = 0; i < q.mem.memoryHeapCount; ++i)
        if (q.mem.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
            a.device_local_mb += q.mem.memoryHeaps[i].size / (1024ull * 1024ull);
    check_baseline(q, need_swapchain, a.missing);
    a.meets_baseline = a.missing.empty();
    return a;
}

std::vector<VkPhysicalDevice> physical_devices(VkInstance instance) {
    u32 count = 0;
    vkEnumeratePhysicalDevices(instance, &count, nullptr);
    std::vector<VkPhysicalDevice> pds(count);
    vkEnumeratePhysicalDevices(instance, &count, pds.data());
    return pds;
}

VkInstance create_bare_instance() {
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "tefra_enumerate";
    app.apiVersion = VK_API_VERSION_1_2;
    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ci.pApplicationInfo = &app;
    VkInstance instance = VK_NULL_HANDLE;
    if (vkCreateInstance(&ci, nullptr, &instance) != VK_SUCCESS) return VK_NULL_HANDLE;
    volkLoadInstanceOnly(instance);
    return instance;
}

} // namespace

const char* vk_result_str(VkResult r) {
    switch (r) {
#define TEFRA_CASE(x)                                                                                                  \
    case x: return #x;
        TEFRA_CASE(VK_SUCCESS)
        TEFRA_CASE(VK_NOT_READY)
        TEFRA_CASE(VK_TIMEOUT)
        TEFRA_CASE(VK_INCOMPLETE)
        TEFRA_CASE(VK_SUBOPTIMAL_KHR)
        TEFRA_CASE(VK_ERROR_OUT_OF_HOST_MEMORY)
        TEFRA_CASE(VK_ERROR_OUT_OF_DEVICE_MEMORY)
        TEFRA_CASE(VK_ERROR_INITIALIZATION_FAILED)
        TEFRA_CASE(VK_ERROR_DEVICE_LOST)
        TEFRA_CASE(VK_ERROR_MEMORY_MAP_FAILED)
        TEFRA_CASE(VK_ERROR_LAYER_NOT_PRESENT)
        TEFRA_CASE(VK_ERROR_EXTENSION_NOT_PRESENT)
        TEFRA_CASE(VK_ERROR_FEATURE_NOT_PRESENT)
        TEFRA_CASE(VK_ERROR_INCOMPATIBLE_DRIVER)
        TEFRA_CASE(VK_ERROR_TOO_MANY_OBJECTS)
        TEFRA_CASE(VK_ERROR_FORMAT_NOT_SUPPORTED)
        TEFRA_CASE(VK_ERROR_SURFACE_LOST_KHR)
        TEFRA_CASE(VK_ERROR_OUT_OF_DATE_KHR)
        TEFRA_CASE(VK_ERROR_NATIVE_WINDOW_IN_USE_KHR)
#undef TEFRA_CASE
    default: return "VK_ERROR_<other>";
    }
}

bool loader_init() {
    static bool initialized = false;
    static bool ok = false;
    if (!initialized) {
        initialized = true;
        ok = volkInitialize() == VK_SUCCESS;
        if (!ok) TEFRA_LOG_ERROR("Vulkan loader not found (is a Vulkan driver installed?)");
    }
    return ok;
}

std::vector<AdapterInfo> enumerate_adapters() {
    std::vector<AdapterInfo> result;
    if (!loader_init()) return result;
    VkInstance instance = create_bare_instance();
    if (!instance) return result;
    const auto pds = physical_devices(instance);
    for (u32 i = 0; i < pds.size(); ++i) result.push_back(make_adapter_info(i, query_adapter(pds[i]), false));
    vkDestroyInstance(instance, nullptr);
    return result;
}

std::unique_ptr<Device> Device::create(const DeviceDesc& desc) {
    std::unique_ptr<Device> dev(new Device());
    if (!dev->init(desc)) return nullptr;
    return dev;
}

bool Device::init(const DeviceDesc& desc) {
    if (!loader_init()) return false;
    const bool need_swapchain = !desc.instance_extensions.empty();

    // ---- Instance ---------------------------------------------------------------------------------
    const auto inst_exts = instance_extensions();
    std::vector<const char*> exts = desc.instance_extensions;
    std::vector<const char*> layers;
    caps_.debug_utils = has_extension(inst_exts, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    if (caps_.debug_utils) exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    bool use_validation_features = false;
    if (desc.validation) {
        if (has_layer("VK_LAYER_KHRONOS_validation")) {
            layers.push_back("VK_LAYER_KHRONOS_validation");
            validation_ = true;
            const auto layer_exts = instance_extensions("VK_LAYER_KHRONOS_validation");
            if (desc.sync_validation && has_extension(layer_exts, VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME)) {
                exts.push_back(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
                use_validation_features = true;
            }
        } else {
            TEFRA_LOG_WARN("Validation requested but VK_LAYER_KHRONOS_validation is not installed");
        }
    }

    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = desc.app_name;
    app.pEngineName = "Tefra";
    app.engineVersion = VK_MAKE_API_VERSION(0, 0, 0, 1);
    app.apiVersion = VK_API_VERSION_1_2;

    VkDebugUtilsMessengerCreateInfoEXT messenger_ci{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    messenger_ci.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    messenger_ci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    messenger_ci.pfnUserCallback = debug_callback;

    const VkValidationFeatureEnableEXT enables[] = {VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT};
    VkValidationFeaturesEXT vfeatures{VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT};
    vfeatures.enabledValidationFeatureCount = 1;
    vfeatures.pEnabledValidationFeatures = enables;

    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    ici.enabledExtensionCount = static_cast<u32>(exts.size());
    ici.ppEnabledExtensionNames = exts.data();
    ici.enabledLayerCount = static_cast<u32>(layers.size());
    ici.ppEnabledLayerNames = layers.data();
    const void* chain = nullptr;
    if (validation_ && caps_.debug_utils) {
        messenger_ci.pNext = chain;
        chain = &messenger_ci;
    }
    if (use_validation_features) {
        vfeatures.pNext = chain;
        chain = &vfeatures;
    }
    ici.pNext = chain;

    VkResult r = vkCreateInstance(&ici, nullptr, &instance_);
    if (r != VK_SUCCESS) {
        TEFRA_LOG_ERROR("vkCreateInstance failed: %s", vk_result_str(r));
        return false;
    }
    volkLoadInstanceOnly(instance_);
    if (validation_ && caps_.debug_utils) {
        messenger_ci.pNext = nullptr;
        TEFRA_VK_CHECK(vkCreateDebugUtilsMessengerEXT(instance_, &messenger_ci, nullptr, &messenger_));
    }

    // ---- Physical device --------------------------------------------------------------------------
    const auto pds = physical_devices(instance_);
    if (pds.empty()) {
        TEFRA_LOG_ERROR("No Vulkan devices found");
        return false;
    }
    int best = -1;
    int best_score = -1;
    for (u32 i = 0; i < pds.size(); ++i) {
        const AdapterQuery q = query_adapter(pds[i]);
        const AdapterInfo info = make_adapter_info(i, q, need_swapchain);
        if (!info.meets_baseline) {
            std::string list;
            for (const auto& m : info.missing) list += (list.empty() ? "" : ", ") + m;
            TEFRA_LOG_WARN("GPU %u '%s' does not meet the baseline: %s", i, info.name.c_str(), list.c_str());
        }
        if (desc.gpu_index >= 0) {
            if (static_cast<u32>(desc.gpu_index) == i) best = static_cast<int>(i);
            continue;
        }
        if (!info.meets_baseline) continue;
        const int score = type_score(q.props.deviceType);
        if (score > best_score) {
            best_score = score;
            best = static_cast<int>(i);
        }
    }
    if (best < 0) {
        TEFRA_LOG_ERROR("No suitable GPU (requested index %d)", desc.gpu_index);
        return false;
    }
    physical_ = pds[static_cast<usize>(best)];
    const AdapterQuery q = query_adapter(physical_);
    adapter_ = make_adapter_info(static_cast<u32>(best), q, need_swapchain);
    if (!adapter_.meets_baseline) {
        TEFRA_LOG_ERROR("Selected GPU '%s' does not meet the Vulkan 1.2 baseline", adapter_.name.c_str());
        return false;
    }
    TEFRA_LOG_INFO("GPU %u: %s (%s, Vulkan %s, %s %s)", adapter_.index, adapter_.name.c_str(), adapter_.type.c_str(),
                   adapter_.api_version.c_str(), adapter_.driver_name.c_str(), adapter_.driver_info.c_str());

    // ---- Queue ------------------------------------------------------------------------------------
    u32 qf_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_, &qf_count, nullptr);
    std::vector<VkQueueFamilyProperties> qfs(qf_count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_, &qf_count, qfs.data());
    bool found = false;
    for (u32 i = 0; i < qf_count; ++i) {
        const VkQueueFlags need = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
        if ((qfs[i].queueFlags & need) == need) {
            queue_family_ = i;
            found = true;
            break;
        }
    }
    if (!found) {
        TEFRA_LOG_ERROR("No graphics+compute queue family");
        return false;
    }

    // ---- Features ---------------------------------------------------------------------------------
    const bool strict = desc.strict_baseline;
    const auto& av = q.feats;
    FeatureChain en;
    en.link(true);
    auto& f = en.f2.features;
    f.samplerAnisotropy = VK_TRUE;
    f.textureCompressionBC = VK_TRUE;
    f.multiDrawIndirect = VK_TRUE;
    f.depthClamp = VK_TRUE;
    f.imageCubeArray = VK_TRUE;
    f.fillModeNonSolid = !strict && av.f2.features.fillModeNonSolid;
    f.drawIndirectFirstInstance = !strict && av.f2.features.drawIndirectFirstInstance;
    f.independentBlend = av.f2.features.independentBlend;
    f.shaderStorageImageWriteWithoutFormat = av.f2.features.shaderStorageImageWriteWithoutFormat;
    f.fragmentStoresAndAtomics = av.f2.features.fragmentStoresAndAtomics;
    f.vertexPipelineStoresAndAtomics = av.f2.features.vertexPipelineStoresAndAtomics;
    f.shaderClipDistance = av.f2.features.shaderClipDistance;
    caps_.fill_mode_non_solid = f.fillModeNonSolid;
    caps_.draw_indirect_first_instance = f.drawIndirectFirstInstance;

    en.v11.shaderDrawParameters = !strict && av.v11.shaderDrawParameters;
    caps_.shader_draw_parameters = en.v11.shaderDrawParameters;

    auto& v12 = en.v12;
    v12.descriptorIndexing = VK_TRUE;
    v12.runtimeDescriptorArray = VK_TRUE;
    v12.descriptorBindingPartiallyBound = VK_TRUE;
    v12.descriptorBindingVariableDescriptorCount = VK_TRUE;
    v12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
    v12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    v12.descriptorBindingStorageBufferUpdateAfterBind = av.v12.descriptorBindingStorageBufferUpdateAfterBind;
    v12.descriptorBindingStorageImageUpdateAfterBind = av.v12.descriptorBindingStorageImageUpdateAfterBind;
    v12.descriptorBindingUpdateUnusedWhilePending = av.v12.descriptorBindingUpdateUnusedWhilePending;
    v12.bufferDeviceAddress = VK_TRUE;
    v12.timelineSemaphore = VK_TRUE;
    v12.drawIndirectCount = VK_TRUE;
    v12.imagelessFramebuffer = VK_TRUE;
    v12.separateDepthStencilLayouts = av.v12.separateDepthStencilLayouts;
    v12.uniformBufferStandardLayout = av.v12.uniformBufferStandardLayout;
    v12.samplerFilterMinmax = !strict && av.v12.samplerFilterMinmax;
    v12.hostQueryReset = !strict && av.v12.hostQueryReset;
    caps_.sampler_filter_minmax = v12.samplerFilterMinmax;
    caps_.host_query_reset = v12.hostQueryReset;
    en.sync2.synchronization2 = VK_TRUE;

    const auto dev_exts = q.exts;
    std::vector<const char*> enabled_exts = {VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME};
    if (need_swapchain) enabled_exts.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    if (has_extension(dev_exts, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME)) {
        enabled_exts.push_back(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
        caps_.memory_budget = true;
    }

    const f32 priority = 1.0f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = queue_family_;
    qci.queueCount = 1;
    qci.pQueuePriorities = &priority;

    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.pNext = &en.f2;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = static_cast<u32>(enabled_exts.size());
    dci.ppEnabledExtensionNames = enabled_exts.data();
    r = vkCreateDevice(physical_, &dci, nullptr, &device_);
    if (r != VK_SUCCESS) {
        TEFRA_LOG_ERROR("vkCreateDevice failed: %s", vk_result_str(r));
        return false;
    }
    volkLoadDevice(device_);
    vkGetDeviceQueue(device_, queue_family_, 0, &queue_);

    // ---- Capabilities -----------------------------------------------------------------------------
    const auto& limits = q.props.limits;
    caps_.timestamps = limits.timestampComputeAndGraphics || qfs[queue_family_].timestampValidBits > 0;
    caps_.timestamp_period_ns = limits.timestampPeriod;
    caps_.max_push_constants = limits.maxPushConstantsSize;
    caps_.max_bound_sets = limits.maxBoundDescriptorSets;
    caps_.max_image_dim_2d = limits.maxImageDimension2D;
    caps_.max_sampler_anisotropy = limits.maxSamplerAnisotropy;
    caps_.max_compute_shared_memory = limits.maxComputeSharedMemorySize;
    caps_.device_local_bytes = adapter_.device_local_mb * 1024ull * 1024ull;
    if (strict) {
        // Clamp to the reference hardware (spec ADR-003) so lavapipe cannot hide overuse.
        caps_.max_push_constants = min<u32>(caps_.max_push_constants, 128u);
        caps_.max_bound_sets = min<u32>(caps_.max_bound_sets, 8u);
        caps_.max_compute_shared_memory = min<u32>(caps_.max_compute_shared_memory, 32768u);
    }

    // ---- VMA --------------------------------------------------------------------------------------
    VmaVulkanFunctions vk_funcs{};
    vk_funcs.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    vk_funcs.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
    VmaAllocatorCreateInfo aci{};
    aci.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    if (caps_.memory_budget) aci.flags |= VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;
    aci.physicalDevice = physical_;
    aci.device = device_;
    aci.instance = instance_;
    aci.vulkanApiVersion = VK_API_VERSION_1_2;
    aci.pVulkanFunctions = &vk_funcs;
    TEFRA_VK_CHECK(vmaCreateAllocator(&aci, &allocator_));
    return true;
}

Device::~Device() {
    if (device_) vkDeviceWaitIdle(device_);
    if (allocator_) vmaDestroyAllocator(allocator_);
    if (device_) vkDestroyDevice(device_, nullptr);
    if (messenger_) vkDestroyDebugUtilsMessengerEXT(instance_, messenger_, nullptr);
    if (instance_) vkDestroyInstance(instance_, nullptr);
}

u32 validation_error_count() { return g_validation_errors.load(std::memory_order_relaxed); }
u32 validation_warning_count() { return g_validation_warnings.load(std::memory_order_relaxed); }
u32 Device::validation_errors() const { return validation_error_count(); }
u32 Device::validation_warnings() const { return validation_warning_count(); }

bool Device::supports_present(VkSurfaceKHR surface) const {
    VkBool32 ok = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(physical_, queue_family_, surface, &ok);
    return ok == VK_TRUE;
}

void Device::wait_idle() const { vkDeviceWaitIdle(device_); }

void Device::set_name(VkObjectType type, u64 handle, const char* name) const {
    if (!caps_.debug_utils || !vkSetDebugUtilsObjectNameEXT) return;
    VkDebugUtilsObjectNameInfoEXT info{VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT};
    info.objectType = type;
    info.objectHandle = handle;
    info.pObjectName = name;
    vkSetDebugUtilsObjectNameEXT(device_, &info);
}

void Device::memory_usage_mb(f64& used, f64& budget) const {
    used = 0.0;
    budget = 0.0;
    VmaBudget budgets[VK_MAX_MEMORY_HEAPS];
    vmaGetHeapBudgets(allocator_, budgets);
    const VkPhysicalDeviceMemoryProperties* mp = nullptr;
    vmaGetMemoryProperties(allocator_, &mp);
    for (u32 i = 0; i < mp->memoryHeapCount; ++i) {
        if (!(mp->memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)) continue;
        // Bytes actually allocated (VMA block slack excluded) - what the content really costs.
        used += static_cast<f64>(budgets[i].statistics.allocationBytes) / (1024.0 * 1024.0);
        budget += static_cast<f64>(budgets[i].budget) / (1024.0 * 1024.0);
    }
}

} // namespace tefra::rhi
