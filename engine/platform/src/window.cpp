#include "tefra/platform/window.h"

#include "tefra/core/log.h"

// volk.h (via window.h) defines VK_VERSION_1_0, which makes GLFW declare its Vulkan helpers.
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <string>

namespace tefra::platform {
namespace {

int g_glfw_refs = 0;

void error_callback(int code, const char* desc) { TEFRA_LOG_ERROR("GLFW %d: %s", code, desc); }

bool glfw_acquire() {
    if (g_glfw_refs++ > 0) return true;
    glfwSetErrorCallback(error_callback);
    // Share volk's loader with GLFW so both use the same vulkan-1 / libvulkan.
    if (vkGetInstanceProcAddr) glfwInitVulkanLoader(vkGetInstanceProcAddr);
    if (!glfwInit()) {
        g_glfw_refs = 0;
        return false;
    }
    return true;
}

void glfw_release() {
    if (--g_glfw_refs == 0) glfwTerminate();
}

int glfw_key(Key k) {
    switch (k) {
    case Key::Escape: return GLFW_KEY_ESCAPE;
    case Key::F1: return GLFW_KEY_F1;
    case Key::F2: return GLFW_KEY_F2;
    case Key::F3: return GLFW_KEY_F3;
    case Key::F12: return GLFW_KEY_F12;
    case Key::W: return GLFW_KEY_W;
    case Key::A: return GLFW_KEY_A;
    case Key::S: return GLFW_KEY_S;
    case Key::D: return GLFW_KEY_D;
    case Key::Q: return GLFW_KEY_Q;
    case Key::E: return GLFW_KEY_E;
    case Key::Space: return GLFW_KEY_SPACE;
    case Key::LeftShift: return GLFW_KEY_LEFT_SHIFT;
    case Key::Count: break;
    }
    return GLFW_KEY_UNKNOWN;
}

GLFWwindow* win(void* h) { return static_cast<GLFWwindow*>(h); }

} // namespace

Window* Window::create(const WindowDesc& desc) {
    if (!glfw_acquire()) return nullptr;
    if (!glfwVulkanSupported()) {
        TEFRA_LOG_ERROR("GLFW: Vulkan not supported on this display");
        glfw_release();
        return nullptr;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, desc.resizable ? GLFW_TRUE : GLFW_FALSE);
    GLFWwindow* w =
        glfwCreateWindow(static_cast<int>(desc.width), static_cast<int>(desc.height), desc.title, nullptr, nullptr);
    if (!w) {
        glfw_release();
        return nullptr;
    }
    Window* window = new Window();
    window->handle_ = w;
    return window;
}

Window::~Window() {
    if (handle_) glfwDestroyWindow(win(handle_));
    glfw_release();
}

bool Window::should_close() const { return glfwWindowShouldClose(win(handle_)) != 0; }
void Window::request_close() { glfwSetWindowShouldClose(win(handle_), GLFW_TRUE); }
void Window::poll_events() { glfwPollEvents(); }
void Window::set_title(const char* title) { glfwSetWindowTitle(win(handle_), title); }

void Window::framebuffer_size(u32& width, u32& height) const {
    int w = 0, h = 0;
    glfwGetFramebufferSize(win(handle_), &w, &h);
    width = static_cast<u32>(w > 0 ? w : 0);
    height = static_cast<u32>(h > 0 ? h : 0);
}

bool Window::key_down(Key k) const { return glfwGetKey(win(handle_), glfw_key(k)) == GLFW_PRESS; }

bool Window::key_pressed(Key k) {
    const bool down = key_down(k);
    bool& prev = prev_keys_[static_cast<usize>(k)];
    const bool pressed = down && !prev;
    prev = down;
    return pressed;
}

void Window::cursor_delta(f64& dx, f64& dy) {
    f64 x = 0, y = 0;
    glfwGetCursorPos(win(handle_), &x, &y);
    dx = have_cursor_ ? x - last_x_ : 0.0;
    dy = have_cursor_ ? y - last_y_ : 0.0;
    last_x_ = x;
    last_y_ = y;
    have_cursor_ = true;
}

bool Window::mouse_button_down(int button) const { return glfwGetMouseButton(win(handle_), button) == GLFW_PRESS; }

std::vector<const char*> Window::required_instance_extensions() {
    // Copied into static storage: GLFW's array dies with glfwTerminate().
    static std::vector<std::string> storage;
    std::vector<const char*> result;
    if (!glfw_acquire()) return result;
    u32 count = 0;
    const char** exts = glfwGetRequiredInstanceExtensions(&count);
    storage.assign(exts, exts + count);
    glfw_release();
    for (const auto& e : storage) result.push_back(e.c_str());
    return result;
}

VkSurfaceKHR Window::create_surface(VkInstance instance) const {
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    const VkResult r = glfwCreateWindowSurface(instance, win(handle_), nullptr, &surface);
    if (r != VK_SUCCESS) TEFRA_LOG_ERROR("glfwCreateWindowSurface failed (%d)", static_cast<int>(r));
    return surface;
}

} // namespace tefra::platform
