#pragma once

#include "tefra/rhi/vk.h"

#include <memory>
#include <string>
#include <vector>

namespace tefra::rhi {

// Optional features: enabled only when present AND not in strict-baseline mode. Code must check these.
struct Capabilities {
    bool memory_budget = false;
    bool debug_utils = false;
    bool shader_draw_parameters = false;
    bool sampler_filter_minmax = false;
    bool host_query_reset = false;
    bool fill_mode_non_solid = false;
    bool draw_indirect_first_instance = false;
    bool timestamps = false; // graphics queue supports timestamps
    f32 timestamp_period_ns = 1.0f;
    u32 max_push_constants = 0;
    u32 max_bound_sets = 0;
    u32 max_image_dim_2d = 0;
    f32 max_sampler_anisotropy = 1.0f;
    u32 max_compute_shared_memory = 0;
    u64 device_local_bytes = 0;
};

struct AdapterInfo {
    u32 index = 0;
    std::string name;
    u32 vendor_id = 0;
    u32 device_id = 0;
    std::string type; // "discrete", "integrated", "virtual", "cpu", "other"
    std::string api_version;
    std::string driver_name;
    std::string driver_info;
    bool software = false;
    u64 device_local_mb = 0;
    bool meets_baseline = false;
    std::vector<std::string> missing; // baseline requirements not met
};

struct DeviceDesc {
    const char* app_name = "tefra";
    bool validation = false;
    bool sync_validation = true; // only when validation is on
    // Strict baseline: enable only features of the reference hardware (spec ADR-003), never optional extras.
    bool strict_baseline = false;
    i32 gpu_index = -1;                           // -1 = auto (discrete > integrated > virtual > cpu)
    std::vector<const char*> instance_extensions; // e.g. from the window system; empty = headless
};

class Device {
  public:
    static std::unique_ptr<Device> create(const DeviceDesc& desc);
    ~Device();
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;

    VkInstance instance() const { return instance_; }
    VkPhysicalDevice physical() const { return physical_; }
    VkDevice device() const { return device_; }
    VkQueue queue() const { return queue_; }
    u32 queue_family() const { return queue_family_; }
    VmaAllocator allocator() const { return allocator_; }
    const Capabilities& caps() const { return caps_; }
    const AdapterInfo& adapter() const { return adapter_; }
    bool validation_enabled() const { return validation_; }

    u32 validation_errors() const;
    u32 validation_warnings() const;

    // Returns false if the queue family cannot present to this surface.
    bool supports_present(VkSurfaceKHR surface) const;
    void wait_idle() const;
    // Debug name for tools (RenderDoc, validation messages). No-op without debug utils.
    void set_name(VkObjectType type, u64 handle, const char* name) const;

    // GPU memory currently used / budget in MiB, summed over device-local heaps.
    void memory_usage_mb(f64& used, f64& budget) const;

  private:
    Device() = default;
    bool init(const DeviceDesc& desc);

    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    u32 queue_family_ = 0;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    Capabilities caps_;
    AdapterInfo adapter_;
    bool validation_ = false;
};

// Process-wide validation message counters (include messages emitted while destroying devices).
u32 validation_error_count();
u32 validation_warning_count();

// Initialises the loader (volk). Safe to call more than once.
bool loader_init();
// Lists every adapter with its baseline check, without creating a device.
// Call it only while no Device exists: it re-points the global volk instance functions.
std::vector<AdapterInfo> enumerate_adapters();

} // namespace tefra::rhi
