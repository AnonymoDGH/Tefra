#pragma once

// Platform-neutral input snapshot for one game frame (spec M3b). Filled by the host (Studio viewport, tefra_player
// window) and read by the game runtime, scripts (UserInputService) and the in-game UI.

#include "tefra/core/math.h"
#include "tefra/core/types.h"

#include <string_view>

namespace tefra {

enum class Key : u16 {
    Unknown = 0,
    A,
    B,
    C,
    D,
    E,
    F,
    G,
    H,
    I,
    J,
    K,
    L,
    M,
    N,
    O,
    P,
    Q,
    R,
    S,
    T,
    U,
    V,
    W,
    X,
    Y,
    Z,
    Zero,
    One,
    Two,
    Three,
    Four,
    Five,
    Six,
    Seven,
    Eight,
    Nine,
    Space,
    Return,
    Escape,
    Tab,
    Backspace,
    Delete,
    Insert,
    Home,
    End,
    PageUp,
    PageDown,
    Left,
    Right,
    Up,
    Down,
    LeftShift,
    RightShift,
    LeftControl,
    RightControl,
    LeftAlt,
    RightAlt,
    F1,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12,
    Count
};

enum class MouseButton : u8 { Left = 0, Right = 1, Middle = 2, Count = 3 };

struct InputState {
    bool down[static_cast<usize>(Key::Count)] = {};     // held this frame
    bool pressed[static_cast<usize>(Key::Count)] = {};  // went down this frame
    bool released[static_cast<usize>(Key::Count)] = {}; // went up this frame
    bool mouse_down[3] = {}, mouse_pressed[3] = {}, mouse_released[3] = {};
    vec2 mouse_pos{};           // pixels from the top-left of the game view
    vec2 mouse_delta{};         // pixels moved this frame
    f32 wheel = 0.0f;           // notches (+ = away from the user)
    vec2 view_size{};           // game view in pixels
    bool hovered = false;       // the mouse is over the game view
    bool keyboard = true;       // false while another widget has keyboard focus (typing in Studio)
    bool mouse_over_ui = false; // set by the in-game UI when the cursor is over an interactive GuiObject

    bool key(Key k) const { return keyboard && down[static_cast<usize>(k)]; }
    bool key_pressed(Key k) const { return keyboard && pressed[static_cast<usize>(k)]; }
    bool mouse(MouseButton b) const { return hovered && mouse_down[static_cast<usize>(b)]; }
};

// Roblox Enum.KeyCode names ("W", "Space", "LeftShift", "One", "F1"). key_from_name is case-sensitive; Unknown when
// the name is not recognised.
const char* key_name(Key k);
Key key_from_name(std::string_view name);

} // namespace tefra
