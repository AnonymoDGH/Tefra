#pragma once

// Fills a tefra::InputState from Dear ImGui's IO for a game view rectangle (Studio viewport, tefra_player window).

#include "tefra/core/input.h"

namespace tefra::ui {

// `view_min` / `view_size`: the game view in ImGui screen coordinates. `hovered`: the mouse is over the view.
// `keyboard`: the game owns the keyboard (false while a text field has focus).
void input_from_imgui(InputState& out, vec2 view_min, vec2 view_size, bool hovered, bool keyboard);

} // namespace tefra::ui
