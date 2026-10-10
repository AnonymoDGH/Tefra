#pragma once

#include "tefra/core/types.h"

#include <volk.h>

#include <vector>

namespace tefra::platform {

struct WindowDesc {
    const char* title = "Tefra";
    u32 width = 1280;
    u32 height = 720;
    bool resizable = true;
};

enum class Key : u16 { Escape, F1, F2, F3, F12, W, A, S, D, Q, E, Space, LeftShift, Count };

class Window {
  public:
    // Initialises GLFW on first use. Returns nullptr on failure (e.g. no display).
    static Window* create(const WindowDesc& desc);
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool should_close() const;
    void request_close();
    void poll_events();
    void set_title(const char* title);
    void framebuffer_size(u32& width, u32& height) const;
    bool key_down(Key k) const;
    bool key_pressed(Key k); // edge-triggered since last call
    void cursor_delta(f64& dx, f64& dy);
    bool mouse_button_down(int button) const;

    // Vulkan integration.
    static std::vector<const char*> required_instance_extensions();
    VkSurfaceKHR create_surface(VkInstance instance) const;

  private:
    Window() = default;
    void* handle_ = nullptr; // GLFWwindow*
    bool prev_keys_[static_cast<usize>(Key::Count)] = {};
    f64 last_x_ = 0, last_y_ = 0;
    bool have_cursor_ = false;
};

} // namespace tefra::platform
