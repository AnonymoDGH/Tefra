#pragma once

// In-game UI (spec M3b §5): draws the ScreenGuis of a running game (LocalPlayer.PlayerGui) over the game view with
// Dear ImGui draw lists and turns clicks into GameEvents. Layout follows Roblox's UDim2: Position / Size are a scale
// (0-1 of the parent) plus an offset in pixels (PositionOffset / SizeOffset), AnchorPoint picks the pivot.
//
// Events fired through DataModel::fire_event: "MouseButton1Click" then "Activated" on TextButton / ImageButton
// (left click released over the button that got the press), "MouseEnter" / "MouseLeave" on any GuiObject.

#include "tefra/core/input.h"
#include "tefra/core/math.h"

#include <functional>
#include <memory>
#include <string>

struct ImDrawList;

namespace tefra::world {
class Instance;
}

namespace tefra::ui {

struct Rect {
    vec2 min{}, size{};
};

// Absolute rectangle of a GuiObject inside its parent's rectangle (pure; unit-tested).
Rect layout_rect(const world::Instance& gui_object, Rect parent);

// Turns an image file (absolute path) into an ImTextureID for ImDrawList::AddImage, or nullptr on failure. Hosts with a
// Vulkan ImGui backend provide one (see TextureCache); without it ImageLabels draw as their background only.
using TextureLoader = std::function<void*(const std::string& path, vec2* size)>;

class GameGui {
  public:
    GameGui();
    ~GameGui();
    GameGui(const GameGui&) = delete;
    GameGui& operator=(const GameGui&) = delete;

    void set_asset_root(std::string dir);          // relative Image paths resolve against it
    void set_texture_loader(TextureLoader loader); // optional

    // Draws every enabled ScreenGui under `root` (PlayerGui while playing; StarterGui in edit mode) into the view
    // rectangle (screen coordinates), ordered by DisplayOrder then ZIndex. When `interactive`, hover and clicks fire
    // events and `input->mouse_over_ui` is set while the cursor is over a visible button or an opaque Frame.
    void draw(ImDrawList* dl, Rect view, world::Instance& root, InputState* input, bool interactive);

    struct Impl; // implementation detail (game_gui.cpp)

  private:
    std::unique_ptr<Impl> impl_;
};

} // namespace tefra::ui
