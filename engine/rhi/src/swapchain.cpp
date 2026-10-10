#include "tefra/rhi/swapchain.h"
#include "tefra/rhi/device.h"

#include "tefra/core/log.h"
#include "tefra/core/math.h"

#include <cstring>

namespace tefra::rhi {

const char* present_mode_name(PresentMode m) {
    switch (m) {
    case PresentMode::Fifo: return "fifo";
    case PresentMode::Mailbox: return "mailbox";
    case PresentMode::Immediate: return "immediate";
    }
    return "?";
}

bool parse_present_mode(const char* s, PresentMode& out) {
    if (!s) return false;
    if (std::strcmp(s, "fifo") == 0)
        out = PresentMode::Fifo;
    else if (std::strcmp(s, "mailbox") == 0)
        out = PresentMode::Mailbox;
    else if (std::strcmp(s, "immediate") == 0)
        out = PresentMode::Immediate;
    else
        return false;
    return true;
}

Swapchain::Swapchain(const Device& dev, VkSurfaceKHR surface, u32 width, u32 height, PresentMode mode)
    : dev_(dev), surface_(surface), mode_(mode) {
    VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (auto& s : acquire_sems_) TEFRA_VK_CHECK(vkCreateSemaphore(dev_.device(), &sci, nullptr, &s));
    create(width, height);
}

Swapchain::~Swapchain() {
    dev_.wait_idle();
    destroy_views_and_sems();
    for (auto& s : acquire_sems_) vkDestroySemaphore(dev_.device(), s, nullptr);
    if (swapchain_) vkDestroySwapchainKHR(dev_.device(), swapchain_, nullptr);
    if (surface_) vkDestroySurfaceKHR(dev_.instance(), surface_, nullptr);
}

void Swapchain::destroy_views_and_sems() {
    for (auto v : views_) vkDestroyImageView(dev_.device(), v, nullptr);
    for (auto s : present_sems_) vkDestroySemaphore(dev_.device(), s, nullptr);
    views_.clear();
    present_sems_.clear();
}

void Swapchain::create(u32 width, u32 height) {
    VkPhysicalDevice pd = dev_.physical();
    VkSurfaceCapabilitiesKHR caps{};
    TEFRA_VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(pd, surface_, &caps));

    u32 count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(pd, surface_, &count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(pd, surface_, &count, formats.data());
    VkSurfaceFormatKHR chosen =
        formats.empty() ? VkSurfaceFormatKHR{VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR} : formats[0];
    for (const auto& f : formats) {
        if ((f.format == VK_FORMAT_B8G8R8A8_SRGB || f.format == VK_FORMAT_R8G8B8A8_SRGB) &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosen = f;
            break;
        }
    }
    format_ = chosen.format;
    color_space_ = chosen.colorSpace;

    vkGetPhysicalDeviceSurfacePresentModesKHR(pd, surface_, &count, nullptr);
    std::vector<VkPresentModeKHR> modes(count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(pd, surface_, &count, modes.data());
    VkPresentModeKHR want = VK_PRESENT_MODE_FIFO_KHR;
    if (mode_ == PresentMode::Mailbox) want = VK_PRESENT_MODE_MAILBOX_KHR;
    if (mode_ == PresentMode::Immediate) want = VK_PRESENT_MODE_IMMEDIATE_KHR;
    VkPresentModeKHR present = VK_PRESENT_MODE_FIFO_KHR;
    for (auto m : modes)
        if (m == want) present = m;
    if (present != want) {
        TEFRA_LOG_WARN("Present mode %s not supported, using fifo", present_mode_name(mode_));
        mode_ = PresentMode::Fifo;
    }

    if (caps.currentExtent.width != 0xFFFFFFFFu) {
        extent_ = caps.currentExtent;
    } else {
        extent_.width = clamp(width, caps.minImageExtent.width, caps.maxImageExtent.width);
        extent_.height = clamp(height, caps.minImageExtent.height, caps.maxImageExtent.height);
    }
    if (extent_.width == 0 || extent_.height == 0) return; // minimized; caller retries later

    u32 image_count = max(caps.minImageCount + 1, 2u);
    if (caps.maxImageCount > 0) image_count = min(image_count, caps.maxImageCount);

    VkSwapchainCreateInfoKHR sci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    sci.surface = surface_;
    sci.minImageCount = image_count;
    sci.imageFormat = format_;
    sci.imageColorSpace = color_space_;
    sci.imageExtent = extent_;
    sci.imageArrayLayers = 1;
    usage_ = kUsage & caps.supportedUsageFlags;
    sci.imageUsage = usage_;
    sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sci.preTransform = caps.currentTransform;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode = present;
    sci.clipped = VK_TRUE;
    VkSwapchainKHR old = swapchain_;
    sci.oldSwapchain = old;
    TEFRA_VK_CHECK(vkCreateSwapchainKHR(dev_.device(), &sci, nullptr, &swapchain_));
    if (old) vkDestroySwapchainKHR(dev_.device(), old, nullptr);

    vkGetSwapchainImagesKHR(dev_.device(), swapchain_, &count, nullptr);
    images_.resize(count);
    vkGetSwapchainImagesKHR(dev_.device(), swapchain_, &count, images_.data());
    VkSemaphoreCreateInfo semci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (VkImage img : images_) {
        VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vci.image = img;
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = format_;
        vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkImageView view = VK_NULL_HANDLE;
        TEFRA_VK_CHECK(vkCreateImageView(dev_.device(), &vci, nullptr, &view));
        views_.push_back(view);
        VkSemaphore sem = VK_NULL_HANDLE;
        TEFRA_VK_CHECK(vkCreateSemaphore(dev_.device(), &semci, nullptr, &sem));
        present_sems_.push_back(sem);
    }
    TEFRA_LOG_INFO("Swapchain %ux%u, %u images, %s", extent_.width, extent_.height, count, present_mode_name(mode_));
}

void Swapchain::recreate(u32 width, u32 height) {
    dev_.wait_idle();
    destroy_views_and_sems();
    images_.clear();
    create(width, height);
}

bool Swapchain::acquire(u32 frame_slot, u32& image_index) {
    if (!swapchain_ || images_.empty()) return false;
    const VkResult r = vkAcquireNextImageKHR(dev_.device(), swapchain_, UINT64_MAX, acquire_sems_[frame_slot],
                                             VK_NULL_HANDLE, &image_index);
    if (r == VK_ERROR_OUT_OF_DATE_KHR) return false;
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) TEFRA_PANIC("vkAcquireNextImageKHR: %s", vk_result_str(r));
    return true;
}

bool Swapchain::present(u32 image_index) {
    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &present_sems_[image_index];
    pi.swapchainCount = 1;
    pi.pSwapchains = &swapchain_;
    pi.pImageIndices = &image_index;
    const VkResult r = vkQueuePresentKHR(dev_.queue(), &pi);
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) return false;
    if (r != VK_SUCCESS) TEFRA_PANIC("vkQueuePresentKHR: %s", vk_result_str(r));
    return true;
}

} // namespace tefra::rhi
