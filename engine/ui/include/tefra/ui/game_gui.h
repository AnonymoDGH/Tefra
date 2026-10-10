#pragma once

// In-game UI (spec M3b §5): draws the ScreenGuis of a running game (LocalPlayer.PlayerGui) over the game view with
// Dear ImGui draw lists, places BillboardGuis and name plates over the 3D world, and turns clicks and typing into
// GameEvents. Layout follows Roblox's UDim2 idea: Position / Size are a scale (0-1 of the parent) plus a pixel offset
// (PositionOffset / SizeOffset), and AnchorPoint picks the pivot.
//
// Colours are authored in sRGB (like the Studio theme) and converted to linear before they reach ImGui's vertex
// colours, because the viewport and swapchain are sRGB targets.
//
// Events fired through DataModel::fire_event: "MouseButton1Click" then "Activated" on TextButton / ImageButton
// (left click released over the button that got the press), "MouseEnter" / "MouseLeave" on buttons, "Focused" /
// "FocusLost" on TextBox (FocusLost's number is 1 when Enter was pressed, 0 otherwise).

#include "tefra/core/input.h"
#include "tefra/core/math.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

struct ImDrawList;
struct ImFont;

namespace tefra::rhi {
class Device;
}

namespace tefra::world {
class Instance;
}

namespace tefra::ui {

// A rectangle in screen pixels (top-left corner + size).
struct Rect {
    vec2 min{}, size{};
};

// The three typefaces of GuiObject.Font, in enum order.
enum class GuiFont : u8 { Default = 0, Bold = 1, Mono = 2 };

// Absolute rectangle of a GuiObject inside its parent's rectangle. Honours AnchorPoint and a UIAspectRatioConstraint
// child (the width drives the height). Pure: needs no ImGui context.
Rect layout_rect(const world::Instance& gui_object, Rect parent);

// The rectangle left inside `parent` after its UIPadding child (if any) is applied.
Rect container_rect(const world::Instance& parent, Rect rect);

struct PlacedChild {
    world::Instance* object = nullptr;
    Rect rect{};
};

// Rectangles of the visible GuiObject children of `parent`, in draw order (ZIndex, stable). A UIListLayout or
// UIGridLayout child places them; otherwise each uses layout_rect. Invisible children take no room in a layout.
std::vector<PlacedChild> layout_children(world::Instance& parent, Rect rect);

// TextScaled: the font size at which `text_size_at_base` (measured at `base_size`) fills `box` (95% wide, 80% high).
// Never below 1 px. Pure.
f32 text_scaled_size(f32 base_size, vec2 box, vec2 text_size_at_base);

// The camera used to place BillboardGuis and name plates. Built from the runtime's yaw / pitch (radians, yaw 0
// looks down -Z) or from explicit axes.
struct GuiCamera {
    vec3 position{0, 0, 0};
    vec3 forward{0, 0, -1};
    vec3 right{1, 0, 0};
    vec3 up{0, 1, 0};
    f32 fov_y = 1.2217305f; // radians (70 degrees)
};
GuiCamera make_gui_camera(vec3 position, f32 yaw, f32 pitch, f32 fov_y);

// Projects a world point to screen pixels inside `view`. False when the point is behind the camera.
bool world_to_screen(const GuiCamera& cam, vec3 world, Rect view, vec2* out);

// Turns an image file (absolute path) into an ImTextureID for ImDrawList::AddImage, or nullptr on failure. Used only
// when no device was given to enable_textures(); `size` receives the pixel size.
using TextureLoader = std::function<void*(const std::string& path, vec2* size)>;

class TextureCache;

class GameGui {
  public:
    GameGui();
    ~GameGui();
    GameGui(const GameGui&) = delete;
    GameGui& operator=(const GameGui&) = delete;

    void set_asset_root(std::string dir); // relative Image paths resolve against it
    void set_texture_loader(TextureLoader loader);
    // Loads images through a TextureCache owned by this object (preferred). The device must outlive this object and
    // the ImGui Vulkan backend must be initialised. Call once; later calls are ignored.
    void enable_textures(const rhi::Device& device);

    // Typefaces for TextLabel.Font (Default / Bold / Mono). Fonts are drawn at the requested TextSize: Dear ImGui
    // 1.93 bakes each size on demand into the dynamic font atlas (the Vulkan backend uploads it), so the text stays
    // crisp at any size and one ImFont per family is enough. With no font set, ImGui's current font is used. Call
    // before the first draw.
    void set_fonts(ImFont* regular, ImFont* bold, ImFont* mono);

    // Draws every enabled ScreenGui under `root` (PlayerGui while playing; StarterGui in edit mode) into the view
    // rectangle (screen coordinates), ordered by DisplayOrder then ZIndex. When `interactive`, hover, clicks and text
    // entry fire events and `input->mouse_over_ui` is set while the cursor is over a visible button, text box or an
    // opaque Frame. When `input` is given and a TextBox has focus, `input->keyboard` is cleared so the game does not
    // walk while typing.
    //
    // With `camera`, BillboardGuis under Workspace and the name plates of Humanoids (DisplayName) are drawn in 3D
    // space too. They are never interactive. A non-interactive draw of a StarterGui (editor preview) is skipped when
    // StarterGui.ShowDevelopmentGui is false.
    void draw(ImDrawList* dl, Rect view, world::Instance& root, InputState* input, bool interactive,
              const GuiCamera* camera = nullptr);

    // The GuiObject under `mouse` (screen coordinates, same space as `view`): the topmost visible one, children first.
    // Used by the editor to select UI. Billboards are not picked.
    world::Instance* pick(Rect view, world::Instance& root, vec2 mouse);

    // True while a TextBox has focus (the game keyboard must be ignored).
    bool wants_keyboard() const;

    struct Impl; // implementation detail (game_gui.cpp)

  private:
    std::unique_ptr<Impl> impl_;
};

} // namespace tefra::ui
