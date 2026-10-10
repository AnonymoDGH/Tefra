#pragma once

#include "tefra/rhi/vk.h"

#include <vector>

namespace tefra::rhi {

class Device;

enum class PresentMode : u8 { Fifo, Mailbox, Immediate };
const char* present_mode_name(PresentMode m);
bool parse_present_mode(const char* s, PresentMode& out);

class Swapchain {
  public:
    static constexpr u32 kMaxFramesInFlight = 3;
    static constexpr VkImageUsageFlags kUsage =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    // Takes ownership of the surface.
    Swapchain(const Device& dev, VkSurfaceKHR surface, u32 width, u32 height, PresentMode mode);
    ~Swapchain();
    Swapchain(const Swapchain&) = delete;
    Swapchain& operator=(const Swapchain&) = delete;

    // Returns false when the swapchain must be recreated (out of date / zero size).
    bool acquire(u32 frame_slot, u32& image_index);
    bool present(u32 image_index);
    void recreate(u32 width, u32 height);

    VkSemaphore acquire_semaphore(u32 frame_slot) const { return acquire_sems_[frame_slot]; }
    VkSemaphore present_semaphore(u32 image_index) const { return present_sems_[image_index]; }
    VkFormat format() const { return format_; }
    VkExtent2D extent() const { return extent_; }
    u32 image_count() const { return static_cast<u32>(images_.size()); }
    VkImage image(u32 i) const { return images_[i]; }
    VkImageView view(u32 i) const { return views_[i]; }
    PresentMode present_mode() const { return mode_; }
    // Actual image usage (kUsage masked by surface support); render passes must use exactly this.
    VkImageUsageFlags usage() const { return usage_; }

  private:
    void create(u32 width, u32 height);
    void destroy_views_and_sems();

    const Device& dev_;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    VkColorSpaceKHR color_space_ = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    VkExtent2D extent_{};
    PresentMode mode_;
    VkImageUsageFlags usage_ = 0;
    std::vector<VkImage> images_;
    std::vector<VkImageView> views_;
    std::vector<VkSemaphore> present_sems_;
    VkSemaphore acquire_sems_[kMaxFramesInFlight] = {};
};

} // namespace tefra::rhi
