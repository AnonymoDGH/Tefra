#include "tefra/ui/game_gui.h"

#include "tefra/core/fs.h"
#include "tefra/world/datamodel.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace tefra::ui {

using world::Instance;

Rect layout_rect(const Instance& g, Rect parent) {
    const vec3 pos = g.get_vec3("Position"), poff = g.get_vec3("PositionOffset");
    const vec3 size = g.get_vec3("Size"), soff = g.get_vec3("SizeOffset");
    const vec3 anchor = g.get_vec3("AnchorPoint");
    Rect r;
    r.size = {parent.size.x * size.x + soff.x, parent.size.y * size.y + soff.y};
    const vec2 p{parent.min.x + parent.size.x * pos.x + poff.x, parent.min.y + parent.size.y * pos.y + poff.y};
    r.min = {p.x - r.size.x * anchor.x, p.y - r.size.y * anchor.y};
    return r;
}

namespace {
ImU32 color(vec3 c, f32 transparency) {
    const f32 a = clamp(1.0f - transparency, 0.0f, 1.0f);
    auto b = [](f32 v) { return static_cast<int>(clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
    return IM_COL32(b(c.x), b(c.y), b(c.z), b(a));
}

bool inside(Rect r, vec2 p) {
    return p.x >= r.min.x && p.y >= r.min.y && p.x < r.min.x + r.size.x && p.y < r.min.y + r.size.y;
}
} // namespace

struct GameGui::Impl {
    std::string asset_root;
    TextureLoader loader;
    struct Tex {
        void* id = nullptr;
        vec2 size{};
    };
    std::unordered_map<std::string, Tex> textures;
    u64 pressed = 0; // button that received the mouse press
    u64 hovered = 0;
};

GameGui::GameGui() : impl_(std::make_unique<Impl>()) {}
GameGui::~GameGui() = default;

void GameGui::set_asset_root(std::string dir) { impl_->asset_root = std::move(dir); }
void GameGui::set_texture_loader(TextureLoader loader) { impl_->loader = std::move(loader); }

namespace {
struct DrawCtx {
    ImDrawList* dl;
    GameGui::Impl* impl;
    InputState* input;
    bool interactive;
    vec2 mouse;
    u64 top_hit = 0; // topmost interactive object under the cursor (last drawn wins)
};

void draw_object(DrawCtx& c, Instance& g, Rect parent, ImVec2 clip_min, ImVec2 clip_max);

void draw_children(DrawCtx& c, Instance& parent, Rect rect, ImVec2 clip_min, ImVec2 clip_max) {
    std::vector<Instance*> kids;
    for (Instance* k : parent.children())
        if (k->is_a("GuiObject") && k->get_bool("Visible", true)) kids.push_back(k);
    std::stable_sort(kids.begin(), kids.end(), [](const Instance* a, const Instance* b) {
        return a->get_float("ZIndex", 1.0f) < b->get_float("ZIndex", 1.0f);
    });
    for (Instance* k : kids) draw_object(c, *k, rect, clip_min, clip_max);
}

void draw_object(DrawCtx& c, Instance& g, Rect parent, ImVec2 clip_min, ImVec2 clip_max) {
    const Rect r = layout_rect(g, parent);
    const ImVec2 a{r.min.x, r.min.y}, b{r.min.x + r.size.x, r.min.y + r.size.y};
    const f32 radius = g.get_float("CornerRadius", 0.0f);
    const f32 bg_t = g.get_float("BackgroundTransparency", 0.0f);
    c.dl->PushClipRect(clip_min, clip_max, true);
    if (bg_t < 1.0f) {
        vec3 bg = g.get_vec3("BackgroundColor", {1, 1, 1});
        const bool button = g.is_a("TextButton") || g.is_a("ImageButton");
        if (button && g.get_bool("AutoButtonColor", true) && c.interactive && c.impl->hovered == g.id())
            bg = bg * (c.impl->pressed == g.id() ? 0.7f : 0.85f);
        c.dl->AddRectFilled(a, b, color(bg, bg_t), radius);
    }
    const f32 border = g.get_float("BorderSize", 0.0f);
    if (border > 0.0f) c.dl->AddRect(a, b, color(g.get_vec3("BorderColor"), bg_t), radius, 0, border);
    if (g.is_a("ImageLabel") && c.impl->loader) {
        const std::string& img = g.get_string("Image");
        if (!img.empty()) {
            const std::string path =
                c.impl->asset_root.empty() || img[0] == '/' ? img : path_join(c.impl->asset_root, img);
            auto it = c.impl->textures.find(path);
            if (it == c.impl->textures.end()) {
                GameGui::Impl::Tex t;
                t.id = c.impl->loader(path, &t.size);
                it = c.impl->textures.emplace(path, t).first;
            }
            if (it->second.id)
                c.dl->AddImageRounded(reinterpret_cast<ImTextureID>(it->second.id), a, b, {0, 0}, {1, 1},
                                      color(g.get_vec3("ImageColor", {1, 1, 1}), g.get_float("ImageTransparency")),
                                      radius);
        }
    }
    if (g.is_a("TextLabel")) {
        const std::string& text = g.get_string("Text");
        if (!text.empty()) {
            f32 size = g.get_float("TextSize", 20.0f);
            ImFont* font = ImGui::GetFont();
            ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str());
            if (g.get_bool("TextScaled") && ts.y > 0.0f) {
                size = size * std::min(r.size.x * 0.95f / std::max(ts.x, 1.0f), r.size.y * 0.8f / ts.y);
                ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str());
            }
            const world::Value* xa = g.get("TextXAlignment");
            const world::Value* ya = g.get("TextYAlignment");
            const u32 xal = xa ? xa->as_enum() : 1u, yal = ya ? ya->as_enum() : 1u;
            const f32 x = xal == 0 ? a.x + 4 : xal == 2 ? b.x - ts.x - 4 : a.x + (r.size.x - ts.x) * 0.5f;
            const f32 y = yal == 0 ? a.y + 2 : yal == 2 ? b.y - ts.y - 2 : a.y + (r.size.y - ts.y) * 0.5f;
            const f32 tt = g.get_float("TextTransparency", 0.0f);
            const f32 st = g.get_float("TextStrokeTransparency", 1.0f);
            if (st < 1.0f) {
                const ImU32 sc = color({0, 0, 0}, std::max(st, tt));
                for (int dx = -1; dx <= 1; ++dx)
                    for (int dy = -1; dy <= 1; ++dy)
                        if (dx || dy) c.dl->AddText(font, size, {x + f32(dx), y + f32(dy)}, sc, text.c_str());
            }
            c.dl->AddText(font, size, {x, y}, color(g.get_vec3("TextColor", {0, 0, 0}), tt), text.c_str());
        }
    }
    c.dl->PopClipRect();
    const bool button = g.is_a("TextButton") || g.is_a("ImageButton");
    if (c.interactive && inside(r, c.mouse) && (button || bg_t < 0.99f)) {
        c.top_hit = button ? g.id() : 0;
        if (c.input) c.input->mouse_over_ui = true;
    }
    draw_children(c, g, r, clip_min, clip_max);
}
} // namespace

void GameGui::draw(ImDrawList* dl, Rect view, Instance& root, InputState* input, bool interactive) {
    if (!dl) return;
    DrawCtx c{dl, impl_.get(), input, interactive, {}, 0};
    if (input) {
        c.mouse = {view.min.x + input->mouse_pos.x, view.min.y + input->mouse_pos.y};
        input->mouse_over_ui = false;
        if (!input->hovered) c.interactive = false;
    } else {
        c.interactive = false;
    }
    std::vector<Instance*> screens;
    for (Instance* s : root.children())
        if (s->is_a("ScreenGui") && s->get_bool("Enabled", true)) screens.push_back(s);
    std::stable_sort(screens.begin(), screens.end(), [](const Instance* a, const Instance* b) {
        return a->get_float("DisplayOrder", 0.0f) < b->get_float("DisplayOrder", 0.0f);
    });
    const ImVec2 cmin{view.min.x, view.min.y}, cmax{view.min.x + view.size.x, view.min.y + view.size.y};
    for (Instance* s : screens) draw_children(c, *s, view, cmin, cmax);
    if (!c.interactive) return;

    world::DataModel& dm = root.model();
    if (c.top_hit != impl_->hovered) {
        if (Instance* old = dm.find(impl_->hovered)) dm.fire_event(*old, "MouseLeave");
        impl_->hovered = c.top_hit;
        if (Instance* now = dm.find(impl_->hovered)) dm.fire_event(*now, "MouseEnter");
    }
    if (input->mouse_pressed[0]) impl_->pressed = c.top_hit;
    if (input->mouse_released[0]) {
        if (impl_->pressed && impl_->pressed == c.top_hit)
            if (Instance* btn = dm.find(impl_->pressed)) {
                dm.fire_event(*btn, "MouseButton1Click");
                if (Instance* again = dm.find(c.top_hit)) dm.fire_event(*again, "Activated");
            }
        impl_->pressed = 0;
    }
}

} // namespace tefra::ui
