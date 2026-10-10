#include "tefra/ui/imgui_input.h"

#include <imgui.h>

namespace tefra::ui {

namespace {
ImGuiKey to_imgui(Key k) {
    const u16 i = static_cast<u16>(k);
    if (k >= Key::A && k <= Key::Z) return static_cast<ImGuiKey>(ImGuiKey_A + (i - u16(Key::A)));
    if (k >= Key::Zero && k <= Key::Nine) return static_cast<ImGuiKey>(ImGuiKey_0 + (i - u16(Key::Zero)));
    if (k >= Key::F1 && k <= Key::F12) return static_cast<ImGuiKey>(ImGuiKey_F1 + (i - u16(Key::F1)));
    switch (k) {
    case Key::Space: return ImGuiKey_Space;
    case Key::Return: return ImGuiKey_Enter;
    case Key::Escape: return ImGuiKey_Escape;
    case Key::Tab: return ImGuiKey_Tab;
    case Key::Backspace: return ImGuiKey_Backspace;
    case Key::Delete: return ImGuiKey_Delete;
    case Key::Insert: return ImGuiKey_Insert;
    case Key::Home: return ImGuiKey_Home;
    case Key::End: return ImGuiKey_End;
    case Key::PageUp: return ImGuiKey_PageUp;
    case Key::PageDown: return ImGuiKey_PageDown;
    case Key::Left: return ImGuiKey_LeftArrow;
    case Key::Right: return ImGuiKey_RightArrow;
    case Key::Up: return ImGuiKey_UpArrow;
    case Key::Down: return ImGuiKey_DownArrow;
    case Key::LeftShift: return ImGuiKey_LeftShift;
    case Key::RightShift: return ImGuiKey_RightShift;
    case Key::LeftControl: return ImGuiKey_LeftCtrl;
    case Key::RightControl: return ImGuiKey_RightCtrl;
    case Key::LeftAlt: return ImGuiKey_LeftAlt;
    case Key::RightAlt: return ImGuiKey_RightAlt;
    default: return ImGuiKey_None;
    }
}
} // namespace

void input_from_imgui(InputState& out, vec2 view_min, vec2 view_size, bool hovered, bool keyboard) {
    const ImGuiIO& io = ImGui::GetIO();
    InputState in;
    in.view_size = view_size;
    in.hovered = hovered;
    in.keyboard = keyboard;
    in.mouse_pos = {io.MousePos.x - view_min.x, io.MousePos.y - view_min.y};
    in.mouse_delta = {io.MouseDelta.x, io.MouseDelta.y};
    in.wheel = io.MouseWheel;
    for (usize i = 1; i < static_cast<usize>(Key::Count); ++i) {
        const ImGuiKey k = to_imgui(static_cast<Key>(i));
        if (k == ImGuiKey_None) continue;
        in.down[i] = ImGui::IsKeyDown(k);
        in.pressed[i] = ImGui::IsKeyPressed(k, false);
        in.released[i] = ImGui::IsKeyReleased(k);
    }
    for (int b = 0; b < 3; ++b) {
        in.mouse_down[b] = io.MouseDown[b];
        in.mouse_pressed[b] = ImGui::IsMouseClicked(b);
        in.mouse_released[b] = ImGui::IsMouseReleased(b);
    }
    out = in;
}

} // namespace tefra::ui
