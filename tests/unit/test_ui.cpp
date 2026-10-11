#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest.h>

#include "tefra/ui/game_gui.h"
#include "tefra/world/datamodel.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace tefra;
using namespace tefra::ui;
using namespace tefra::world;

namespace {

Instance* make_gui(DataModel& dm, Instance* parent, const char* cls, vec3 pos, vec3 size, vec3 size_off) {
    Instance* g = dm.create(cls, parent, cls);
    REQUIRE(g != nullptr);
    g->set("Position", Value::vector3(pos));
    g->set("Size", Value::vector3(size));
    g->set("SizeOffset", Value::vector3(size_off));
    return g;
}

Instance* make_child(DataModel& dm, Instance* parent, const char* cls) {
    Instance* c = dm.create(cls, parent, cls);
    REQUIRE(c != nullptr);
    return c;
}

Rect rect(f32 x, f32 y, f32 w, f32 h) { return Rect{{x, y}, {w, h}}; }

bool near(f32 a, f32 b) { return std::fabs(a - b) < 1e-3f; }

} // namespace

TEST_CASE("ui layout: anchors and offsets") {
    DataModel dm;
    Instance* screen = dm.create("ScreenGui", dm.find_path("StarterGui"), "Screen");
    REQUIRE(screen != nullptr);
    // Parent 200 x 100 at (10, 20). Child: 50% of the width plus 10 px, 50% of the height, centred on the top edge.
    Instance* g = make_gui(dm, screen, "Frame", {0.5f, 0.0f, 0}, {0.5f, 0.5f, 0}, {10, 0, 0});
    g->set("PositionOffset", Value::vector3({-5, 0, 0}));
    g->set("AnchorPoint", Value::vector3({0.5f, 0.5f, 0}));
    const Rect r = layout_rect(*g, rect(10, 20, 200, 100));
    CHECK(near(r.size.x, 110.0f));
    CHECK(near(r.size.y, 50.0f));
    CHECK(near(r.min.x, 50.0f)); // anchor x = 10 + 100 - 5 - 55
    CHECK(near(r.min.y, -5.0f)); // anchor y = 20 - 25
}

TEST_CASE("ui layout: aspect ratio constraint drives the height") {
    DataModel dm;
    Instance* screen = dm.create("ScreenGui", dm.find_path("StarterGui"), "Screen");
    Instance* g = make_gui(dm, screen, "Frame", {0, 0, 0}, {0, 0, 0}, {100, 10, 0});
    make_child(dm, g, "UIAspectRatioConstraint")->set("AspectRatio", Value::number(2.0));
    const Rect r = layout_rect(*g, rect(0, 0, 800, 600));
    CHECK(near(r.size.x, 100.0f));
    CHECK(near(r.size.y, 50.0f));
}

TEST_CASE("ui layout: UIPadding insets the children") {
    DataModel dm;
    Instance* screen = dm.create("ScreenGui", dm.find_path("StarterGui"), "Screen");
    REQUIRE(screen != nullptr);
    Instance* frame = make_gui(dm, screen, "Frame", {0, 0, 0}, {0, 0, 0}, {300, 200, 0});
    Instance* pad = make_child(dm, frame, "UIPadding");
    pad->set("PaddingLeft", Value::number(10));
    pad->set("PaddingRight", Value::number(5));
    pad->set("PaddingTop", Value::number(3));
    pad->set("PaddingBottom", Value::number(7));
    const Rect box = container_rect(*frame, rect(0, 0, 300, 200));
    CHECK(near(box.min.x, 10.0f));
    CHECK(near(box.min.y, 3.0f));
    CHECK(near(box.size.x, 285.0f));
    CHECK(near(box.size.y, 190.0f));

    Instance* kid = make_gui(dm, frame, "Frame", {0, 0, 0}, {1, 0, 0}, {0, 20, 0});
    const std::vector<PlacedChild> kids = layout_children(*frame, rect(0, 0, 300, 200));
    REQUIRE(kids.size() == 1);
    CHECK(kids[0].object == kid);
    CHECK(near(kids[0].rect.min.x, 10.0f));
    CHECK(near(kids[0].rect.min.y, 3.0f));
    CHECK(near(kids[0].rect.size.x, 285.0f));
}

TEST_CASE("ui layout: UIListLayout stacks children, honours alignment and LayoutOrder") {
    DataModel dm;
    Instance* screen = dm.create("ScreenGui", dm.find_path("StarterGui"), "Screen");
    Instance* list = make_gui(dm, screen, "Frame", {0, 0, 0}, {0, 0, 0}, {200, 300, 0});
    Instance* layout = make_child(dm, list, "UIListLayout");
    layout->set("Padding", Value::number(10));
    Instance* a = make_gui(dm, list, "TextButton", {0, 0, 0}, {0, 0, 0}, {100, 50, 0});
    Instance* b = make_gui(dm, list, "TextButton", {0, 0, 0}, {0, 0, 0}, {100, 50, 0});
    Instance* hidden = make_gui(dm, list, "TextButton", {0, 0, 0}, {0, 0, 0}, {100, 50, 0});
    Instance* c = make_gui(dm, list, "TextButton", {0, 0, 0}, {0, 0, 0}, {100, 50, 0});
    hidden->set("Visible", Value::boolean(false));
    a->set("LayoutOrder", Value::integer(2));
    b->set("LayoutOrder", Value::integer(0));
    c->set("LayoutOrder", Value::integer(1));

    std::vector<PlacedChild> kids = layout_children(*list, rect(0, 0, 200, 300));
    REQUIRE(kids.size() == 3); // the hidden button takes no room
    // Draw order = ZIndex (all equal, so the layout order): b, c, a stacked at y = 0, 60, 120.
    CHECK(kids[0].object == b);
    CHECK(kids[1].object == c);
    CHECK(kids[2].object == a);
    CHECK(near(kids[0].rect.min.y, 0.0f));
    CHECK(near(kids[1].rect.min.y, 60.0f));
    CHECK(near(kids[2].rect.min.y, 120.0f));
    CHECK(near(kids[2].rect.size.y, 50.0f));

    // Centred vertically: the block is 3 * 50 + 2 * 10 = 170 high, so it starts at (300 - 170) / 2 = 65.
    layout->set("VerticalAlignment", Value::enumeration(1));
    kids = layout_children(*list, rect(0, 0, 200, 300));
    CHECK(near(kids[0].rect.min.y, 65.0f));

    // Horizontal row, centred horizontally by the block width 3 * 100 + 2 * 10 = 320 > 200: it overflows evenly.
    layout->set("FillDirection", Value::enumeration(1));
    layout->set("HorizontalAlignment", Value::enumeration(1));
    kids = layout_children(*list, rect(0, 0, 200, 300));
    CHECK(near(kids[0].rect.min.x, (200.0f - 320.0f) * 0.5f));
    CHECK(near(kids[1].rect.min.x, (200.0f - 320.0f) * 0.5f + 110.0f));
}

TEST_CASE("ui layout: UIGridLayout fills rows of equal cells") {
    DataModel dm;
    Instance* screen = dm.create("ScreenGui", dm.find_path("StarterGui"), "Screen");
    Instance* grid = make_gui(dm, screen, "Frame", {0, 0, 0}, {0, 0, 0}, {130, 200, 0});
    Instance* layout = make_child(dm, grid, "UIGridLayout");
    layout->set("CellSize", Value::vector3({50, 40, 0}));
    layout->set("CellPadding", Value::vector3({10, 5, 0}));
    std::vector<Instance*> cells;
    for (int i = 0; i < 5; ++i) cells.push_back(make_gui(dm, grid, "Frame", {0, 0, 0}, {0, 0, 0}, {0, 0, 0}));
    // Width 130: (130 + 10) / (50 + 10) = 2 cells per row.
    const std::vector<PlacedChild> kids = layout_children(*grid, rect(0, 0, 130, 200));
    REQUIRE(kids.size() == 5);
    CHECK(near(kids[0].rect.min.x, 0.0f));
    CHECK(near(kids[0].rect.min.y, 0.0f));
    CHECK(near(kids[1].rect.min.x, 60.0f));
    CHECK(near(kids[2].rect.min.x, 0.0f));
    CHECK(near(kids[2].rect.min.y, 45.0f));
    CHECK(near(kids[4].rect.min.y, 90.0f));
    CHECK(near(kids[4].rect.size.x, 50.0f));
    CHECK(near(kids[4].rect.size.y, 40.0f));
}

TEST_CASE("ui layout: ZIndex orders the draw list") {
    DataModel dm;
    Instance* screen = dm.create("ScreenGui", dm.find_path("StarterGui"), "Screen");
    Instance* top = make_gui(dm, screen, "Frame", {0, 0, 0}, {1, 1, 0}, {0, 0, 0});
    Instance* back = make_gui(dm, screen, "Frame", {0, 0, 0}, {1, 1, 0}, {0, 0, 0});
    top->set("ZIndex", Value::integer(5));
    back->set("ZIndex", Value::integer(1));
    const std::vector<PlacedChild> kids = layout_children(*screen, rect(0, 0, 800, 600));
    REQUIRE(kids.size() == 2);
    CHECK(kids[0].object == back);
    CHECK(kids[1].object == top);
}

TEST_CASE("ui text: TextScaled fills the box, never below 1 px") {
    // 100 px wide and 20 px high at 20 px: the height limit (40 * 0.8 / 20 = 1.6) is tighter than the width
    // (200 * 0.95 / 100 = 1.9), so the text grows to 32 px.
    CHECK(near(text_scaled_size(20.0f, {200, 40}, {100, 20}), 32.0f));
    // A box much wider than the text is limited by the height.
    CHECK(near(text_scaled_size(10.0f, {1000, 10}, {10, 10}), 8.0f));
    // Degenerate input keeps the base size.
    CHECK(near(text_scaled_size(12.0f, {100, 100}, {0, 0}), 12.0f));
    CHECK(text_scaled_size(0.01f, {1, 1}, {1000, 1000}) >= 1.0f);
}

TEST_CASE("ui camera: world points project to the view rectangle") {
    const GuiCamera cam = make_gui_camera({0, 0, 0}, 0.0f, 0.0f, radians(90.0f));
    CHECK(near(cam.forward.z, -1.0f));
    CHECK(near(cam.right.x, 1.0f));
    CHECK(near(cam.up.y, 1.0f));
    const Rect view = rect(0, 0, 200, 100);
    vec2 p{};
    REQUIRE(world_to_screen(cam, {0, 0, -10}, view, &p));
    CHECK(near(p.x, 100.0f));
    CHECK(near(p.y, 50.0f));
    // At depth 1 with a 90 degree fov, x = 1 is half the half-width: 0.75 of the view width.
    REQUIRE(world_to_screen(cam, {1, 0, -1}, view, &p));
    CHECK(near(p.x, 150.0f));
    CHECK(near(p.y, 50.0f));
    // Above the top of the view: y = 1 at depth 1 is the top edge.
    REQUIRE(world_to_screen(cam, {0, 1, -1}, view, &p));
    CHECK(near(p.y, 0.0f));
    // Behind the camera.
    CHECK_FALSE(world_to_screen(cam, {0, 0, 5}, view, &p));
}

TEST_CASE("ui camera: yaw turns the view and the right axis follows") {
    const GuiCamera cam = make_gui_camera({0, 0, 0}, radians(90.0f), 0.0f, radians(70.0f));
    // Yaw 90 looks down -X (forward = (-sin, 0, -cos) = (-1, 0, 0)).
    CHECK(near(cam.forward.x, -1.0f));
    CHECK(near(cam.forward.z, 0.0f));
    CHECK(near(cam.right.z, -1.0f));
    CHECK(near(cam.up.y, 1.0f));
}

TEST_CASE("ui pick: the topmost visible GuiObject under the cursor, children first") {
    DataModel dm;
    Instance* starter = dm.find_path("StarterGui");
    REQUIRE(starter != nullptr);
    Instance* screen = dm.create("ScreenGui", starter, "Screen");
    Instance* a = make_gui(dm, screen, "Frame", {0, 0, 0}, {0, 0, 0}, {100, 100, 0});
    Instance* inner = make_gui(dm, a, "TextLabel", {0.5f, 0.5f, 0}, {0, 0, 0}, {20, 20, 0});
    inner->set("AnchorPoint", Value::vector3({0.5f, 0.5f, 0}));
    GameGui gui;
    const Rect view = rect(0, 0, 800, 600);
    CHECK(gui.pick(view, *starter, {50, 50}) == inner); // the label sits in the middle of the frame
    CHECK(gui.pick(view, *starter, {10, 10}) == a);     // only the frame is under this point
    CHECK(gui.pick(view, *starter, {500, 500}) == nullptr);
    // A hidden object is not picked.
    a->set("Visible", Value::boolean(false));
    CHECK(gui.pick(view, *starter, {10, 10}) == nullptr);
}

TEST_CASE("ui pick: ClipsDescendants limits the children to the parent rectangle") {
    DataModel dm;
    Instance* starter = dm.find_path("StarterGui");
    Instance* screen = dm.create("ScreenGui", starter, "Screen");
    Instance* a = make_gui(dm, screen, "Frame", {0, 0, 0}, {0, 0, 0}, {100, 100, 0});
    Instance* over = make_gui(dm, a, "Frame", {0, 0, 0}, {0, 0, 0}, {50, 50, 0});
    over->set("PositionOffset", Value::vector3({80, 80, 0})); // sticks out of its parent (80..130)
    GameGui gui;
    const Rect view = rect(0, 0, 800, 600);
    CHECK(gui.pick(view, *starter, {120, 120}) == over);
    a->set("ClipsDescendants", Value::boolean(true));
    CHECK(gui.pick(view, *starter, {120, 120}) == nullptr);
}

TEST_CASE("ui gui: wants_keyboard is false until a TextBox has focus") {
    GameGui gui;
    CHECK_FALSE(gui.wants_keyboard());
}

// Drives a TextBox through the real draw path: click to focus, typed characters, Backspace, Enter (FocusLost 1).
// Headless ImGui: a context with the default font atlas, no backend (the draw lists are only built, not rendered).
TEST_CASE("ui text entry: focus, typing, Backspace and Enter") {
    DataModel dm;
    Instance* starter = dm.find_path("StarterGui");
    Instance* screen = dm.create("ScreenGui", starter, "Screen");
    Instance* box = make_gui(dm, screen, "TextBox", {0, 0, 0}, {0, 0, 0}, {200, 40, 0});
    box->set("Text", Value::string(""));
    std::vector<std::string> events;
    std::vector<f64> numbers;
    WorldListener listener;
    listener.event = [&](const GameEvent& e) {
        if (e.source != box) return;
        events.emplace_back(e.name);
        numbers.push_back(e.number);
    };
    dm.add_listener(std::move(listener));

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(800, 600);
    io.DeltaTime = 1.0f / 60.0f;
    io.IniFilename = nullptr;
    io.Fonts->AddFontDefault();
    io.Fonts->Build();

    GameGui gui;
    const Rect view = rect(0, 0, 800, 600);
    InputState in;
    auto frame = [&] {
        ImGui::NewFrame();
        ImGui::Begin("ui_test");
        gui.draw(ImGui::GetWindowDrawList(), view, *starter, &in, true);
        ImGui::End();
        ImGui::Render();
    };

    // Frame 1: a click inside the box focuses it (Focused) and takes the keyboard from the game.
    in.hovered = true;
    in.mouse_pos = {50, 20};
    in.mouse_pressed[0] = true;
    in.mouse_down[0] = true;
    frame();
    CHECK(gui.wants_keyboard());
    CHECK_FALSE(in.keyboard);
    in.mouse_pressed[0] = false;
    in.mouse_down[0] = false;
    in.hovered = false; // typing must work with the cursor outside the view as well

    // Frame 2: two characters.
    in.keyboard = true;
    io.AddInputCharacter('h'); // queued events: they reach io.InputQueueCharacters in the next NewFrame
    io.AddInputCharacter('i');
    ImGui::NewFrame();
    ImGui::Begin("ui_test");
    gui.draw(ImGui::GetWindowDrawList(), view, *starter, &in, true);
    ImGui::End();
    ImGui::Render();
    CHECK(box->get_string("Text") == "hi");
    CHECK_FALSE(in.keyboard);

    // Frame 3: Backspace removes the last character (key down before NewFrame, so it is a press this frame).
    io.AddKeyEvent(ImGuiKey_Backspace, true);
    frame();
    CHECK(box->get_string("Text") == "h");
    io.AddKeyEvent(ImGuiKey_Backspace, false);

    // Frame 4: Enter ends editing and reports FocusLost with number 1.
    io.AddKeyEvent(ImGuiKey_Enter, true);
    frame();
    CHECK_FALSE(gui.wants_keyboard());
    io.AddKeyEvent(ImGuiKey_Enter, false);
    frame();

    REQUIRE(events.size() == 2);
    CHECK(events[0] == "Focused");
    CHECK(events[1] == "FocusLost");
    CHECK(numbers[1] == doctest::Approx(1.0));
    CHECK(box->get_string("Text") == "h");

    ImGui::DestroyContext();
}

// ---- regressions found by the verifier pass -----------------------------------------------------------------------

namespace {

// A headless ImGui context: default font atlas, no backend (draw lists are built, never rendered).
struct HeadlessImGui {
    HeadlessImGui() {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(800, 600);
        io.DeltaTime = 1.0f / 60.0f;
        io.IniFilename = nullptr;
        io.Fonts->AddFontDefault();
        io.Fonts->Build();
    }
    ~HeadlessImGui() { ImGui::DestroyContext(); }
};

// Draws `root` (a StarterGui with ShowDevelopmentGui, so the non-interactive preview path runs) and returns the
// vertices that GameGui::draw appended to the window draw list.
std::vector<ImDrawVert> draw_vertices(GameGui& gui, Instance& root, const GuiCamera* cam = nullptr) {
    ImGui::NewFrame();
    ImGui::Begin("ui_regression");
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const int from = dl->VtxBuffer.Size;
    gui.draw(dl, rect(0, 0, 800, 600), root, nullptr, false, cam);
    std::vector<ImDrawVert> out(dl->VtxBuffer.Data + from, dl->VtxBuffer.Data + dl->VtxBuffer.Size);
    ImGui::End();
    ImGui::Render();
    return out;
}

} // namespace

TEST_CASE("ui colours: sRGB authored colours reach ImGui as linear") {
    HeadlessImGui imgui;
    DataModel dm;
    Instance* starter = dm.find_path("StarterGui");
    starter->set("ShowDevelopmentGui", Value::boolean(true));
    Instance* screen = dm.create("ScreenGui", starter, "Screen");
    Instance* f = make_gui(dm, screen, "Frame", {0, 0, 0}, {0, 0, 0}, {100, 100, 0});
    f->set("BackgroundColor", Value::color3({0.5f, 0.5f, 0.5f}));
    GameGui gui;
    const std::vector<ImDrawVert> verts = draw_vertices(gui, *starter);
    REQUIRE(!verts.empty());
    // Exact sRGB -> linear transfer of 0.5, quantised to 8 bits (what ImGui's vertex colour holds).
    const f32 lin = std::pow((0.5f + 0.055f) / 1.055f, 2.4f);
    const u32 expect = static_cast<u32>(lin * 255.0f + 0.5f);
    CHECK((verts[0].col & 0xFFu) == expect);
    CHECK(((verts[0].col >> 8) & 0xFFu) == expect);
    CHECK(((verts[0].col >> 16) & 0xFFu) == expect);
    CHECK((verts[0].col >> 24) == 255u); // alpha is not gamma-encoded
    CHECK(expect < 128u);                // 0.5 sRGB is much darker than 128 once linearised
}

TEST_CASE("ui rotation: a rotated object turns its vertices around its centre") {
    HeadlessImGui imgui;
    DataModel dm;
    Instance* starter = dm.find_path("StarterGui");
    starter->set("ShowDevelopmentGui", Value::boolean(true));
    Instance* screen = dm.create("ScreenGui", starter, "Screen");
    // 200 x 100 at (100, 100): centre (200, 150). A 90 degree turn swaps the extents: x 150..250, y 50..250.
    Instance* f = make_gui(dm, screen, "Frame", {0, 0, 0}, {0, 0, 0}, {200, 100, 0});
    f->set("PositionOffset", Value::vector3({100, 100, 0}));
    f->set("Rotation", Value::number(90.0));
    GameGui gui;
    const std::vector<ImDrawVert> verts = draw_vertices(gui, *starter);
    REQUIRE(verts.size() >= 4);
    f32 minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f;
    for (const ImDrawVert& v : verts) {
        minx = std::min(minx, v.pos.x);
        maxx = std::max(maxx, v.pos.x);
        miny = std::min(miny, v.pos.y);
        maxy = std::max(maxy, v.pos.y);
    }
    CHECK(near(minx, 150.0f));
    CHECK(near(maxx, 250.0f));
    CHECK(near(miny, 50.0f));
    CHECK(near(maxy, 250.0f));
}

TEST_CASE("ui images: Crop fills the box and Fit keeps the whole image") {
    static int dummy_texture = 0;
    HeadlessImGui imgui;
    DataModel dm;
    Instance* starter = dm.find_path("StarterGui");
    starter->set("ShowDevelopmentGui", Value::boolean(true));
    Instance* screen = dm.create("ScreenGui", starter, "Screen");
    GameGui gui;
    // The image is 200 x 100 (aspect 2); the box is 100 x 100.
    gui.set_texture_loader([](const std::string&, vec2* size) -> void* {
        if (size) *size = vec2{200.0f, 100.0f};
        return &dummy_texture;
    });

    Instance* crop = make_gui(dm, screen, "ImageLabel", {0, 0, 0}, {0, 0, 0}, {100, 100, 0});
    crop->set("Image", Value::string("ui_test.png"));
    crop->set("BackgroundTransparency", Value::number(1.0));
    crop->set("ScaleType", Value::enumeration(2)); // Crop
    std::vector<ImDrawVert> verts = draw_vertices(gui, *starter);
    REQUIRE(verts.size() >= 4);
    f32 umin = 1e9f, umax = -1e9f;
    for (const ImDrawVert& v : verts) {
        umin = std::min(umin, v.uv.x);
        umax = std::max(umax, v.uv.x);
    }
    CHECK(near(umin, 0.25f)); // the middle half of the width is shown
    CHECK(near(umax, 0.75f));

    crop->set("ScaleType", Value::enumeration(1)); // Fit: 100 x 50 centred vertically in the box
    verts = draw_vertices(gui, *starter);
    REQUIRE(verts.size() >= 4);
    f32 miny = 1e9f, maxy = -1e9f, minx = 1e9f, maxx = -1e9f;
    for (const ImDrawVert& v : verts) {
        miny = std::min(miny, v.pos.y);
        maxy = std::max(maxy, v.pos.y);
        minx = std::min(minx, v.pos.x);
        maxx = std::max(maxx, v.pos.x);
    }
    CHECK(near(minx, 0.0f));
    CHECK(near(maxx, 100.0f));
    CHECK(near(miny, 25.0f));
    CHECK(near(maxy, 75.0f));
}

TEST_CASE("ui layout: a tiny grid cell does not overflow the cell count") {
    DataModel dm;
    Instance* starter = dm.find_path("StarterGui");
    Instance* screen = dm.create("ScreenGui", starter, "Screen");
    Instance* box = make_gui(dm, screen, "Frame", {0, 0, 0}, {0, 0, 0}, {100, 100, 0});
    Instance* grid = make_child(dm, box, "UIGridLayout");
    grid->set("CellSize", Value::vector3({1e-30f, 10.0f, 0.0f})); // 100 / 1e-30 does not fit a usize
    grid->set("CellPadding", Value::vector3({0.0f, 0.0f, 0.0f}));
    for (int i = 0; i < 3; ++i) make_child(dm, box, "TextLabel");
    const std::vector<PlacedChild> placed = layout_children(*box, rect(0, 0, 100, 100));
    CHECK(placed.size() == 3);
}

TEST_CASE("ui text: a zero or huge TextSize is clamped, nothing crashes") {
    HeadlessImGui imgui;
    DataModel dm;
    Instance* starter = dm.find_path("StarterGui");
    starter->set("ShowDevelopmentGui", Value::boolean(true));
    Instance* screen = dm.create("ScreenGui", starter, "Screen");
    Instance* a = make_gui(dm, screen, "TextLabel", {0, 0, 0}, {0, 0, 0}, {300, 60, 0});
    a->set("Text", Value::string("zero"));
    a->set("TextSize", Value::number(0.0));
    Instance* b = make_gui(dm, screen, "TextLabel", {0, 0, 0}, {0, 0, 0}, {300, 60, 0});
    b->set("Text", Value::string("huge"));
    b->set("TextSize", Value::number(1.0e9));
    GameGui gui;
    const std::vector<ImDrawVert> verts = draw_vertices(gui, *starter);
    CHECK_FALSE(verts.empty());
}
