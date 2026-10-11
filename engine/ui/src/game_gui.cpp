#include "tefra/ui/game_gui.h"

#include "tefra/core/fs.h"
#include "tefra/ui/texture_cache.h"
#include "tefra/world/datamodel.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace tefra::ui {

using world::DataModel;
using world::Instance;
using world::Value;

namespace {

// ---- small helpers -------------------------------------------------------------------------------------------------

const Instance* child_of(const Instance& g, std::string_view cls) {
    for (const Instance* k : g.children())
        if (k->is_a(cls)) return k;
    return nullptr;
}

u32 enum_at(const Instance& g, std::string_view prop, u32 def = 0) {
    const Value* v = g.get(prop);
    return v && v->type == world::PropType::Enum ? v->as_enum() : def;
}

// Offset of a box of `used` size inside `avail`: 0 = start, 1 = centre, 2 = end. Overflow centres both ways.
f32 align_offset(u32 mode, f32 avail, f32 used) {
    if (mode == 1) return (avail - used) * 0.5f;
    if (mode == 2) return avail - used;
    return 0.0f;
}

bool inside(Rect r, vec2 p) {
    return p.x >= r.min.x && p.y >= r.min.y && p.x < r.min.x + r.size.x && p.y < r.min.y + r.size.y;
}

Rect intersect(Rect a, Rect b) {
    const vec2 lo{std::max(a.min.x, b.min.x), std::max(a.min.y, b.min.y)};
    const vec2 hi{std::min(a.min.x + a.size.x, b.min.x + b.size.x), std::min(a.min.y + a.size.y, b.min.y + b.size.y)};
    return {lo, {std::max(0.0f, hi.x - lo.x), std::max(0.0f, hi.y - lo.y)}};
}

// Colours are authored in sRGB; ImGui's vertex colours go to an sRGB target, so convert to linear first.
f32 srgb_to_linear(f32 c) {
    c = clamp(c, 0.0f, 1.0f);
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}
int to_byte(f32 v) { return static_cast<int>(clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); }

constexpr f32 kMinTextPx = 1.0f;
constexpr f32 kMaxTextPx = 512.0f; // TextScaled on a short word can ask for a huge size: the glyph atlas would explode
ImU32 srgb_color(vec3 c, f32 transparency) {
    const f32 a = clamp(1.0f - transparency, 0.0f, 1.0f);
    return IM_COL32(to_byte(srgb_to_linear(c.x)), to_byte(srgb_to_linear(c.y)), to_byte(srgb_to_linear(c.z)),
                    to_byte(a));
}

// Rotates the vertices appended since `from` around `centre` (degrees, clockwise on screen). Children are included,
// so they turn with their parent.
void rotate_vertices(ImDrawList* dl, int from, ImVec2 centre, f32 degrees) {
    const f32 rad = radians(degrees);
    const f32 cs = std::cos(rad), sn = std::sin(rad);
    for (int i = from; i < dl->VtxBuffer.Size; ++i) {
        ImDrawVert& v = dl->VtxBuffer[i];
        const f32 dx = v.pos.x - centre.x, dy = v.pos.y - centre.y;
        v.pos = {centre.x + dx * cs - dy * sn, centre.y + dx * sn + dy * cs};
    }
}

void sort_by_order(std::vector<Instance*>& kids, u32 sort_order) {
    if (sort_order == 1) { // Name
        std::stable_sort(kids.begin(), kids.end(),
                         [](const Instance* a, const Instance* b) { return a->name() < b->name(); });
    } else { // LayoutOrder
        std::stable_sort(kids.begin(), kids.end(), [](const Instance* a, const Instance* b) {
            return a->get_float("LayoutOrder") < b->get_float("LayoutOrder");
        });
    }
}

// UIListLayout: a column (Vertical) or a row (Horizontal). The main axis starts by the alignment of the whole block;
// the cross axis aligns each child.
void place_list(const Instance& lay, Rect box, const std::vector<Instance*>& kids, std::vector<PlacedChild>& out) {
    if (kids.empty()) return;
    const bool vertical = enum_at(lay, "FillDirection") == 0;
    const f32 gap = lay.get_float("Padding");
    const u32 ha = enum_at(lay, "HorizontalAlignment"), va = enum_at(lay, "VerticalAlignment");
    std::vector<vec2> sizes;
    sizes.reserve(kids.size());
    f32 main_total = 0.0f;
    for (Instance* k : kids) {
        sizes.push_back(layout_rect(*k, box).size);
        main_total += vertical ? sizes.back().y : sizes.back().x;
    }
    main_total += gap * static_cast<f32>(kids.size() - 1);
    f32 pos = vertical ? box.min.y + align_offset(va, box.size.y, main_total)
                       : box.min.x + align_offset(ha, box.size.x, main_total);
    for (usize i = 0; i < kids.size(); ++i) {
        const vec2 s = sizes[i];
        Rect r;
        if (vertical) {
            r.min = {box.min.x + align_offset(ha, box.size.x, s.x), pos};
            pos += s.y + gap;
        } else {
            r.min = {pos, box.min.y + align_offset(va, box.size.y, s.y)};
            pos += s.x + gap;
        }
        r.size = s;
        out.push_back({kids[i], r});
    }
}

// Cells that fit in `span`. The float count is clamped to `n` before the integer conversion: a tiny CellSize would
// otherwise convert an out-of-range float to usize (undefined behaviour).
usize cells_that_fit(f32 span, f32 step, usize n) {
    const f32 fit = std::max(1.0f, std::floor(span / step));
    if (!(fit < static_cast<f32>(n))) return n; // also catches NaN and infinity
    return static_cast<usize>(fit);
}

// UIGridLayout: equal cells, filled across (Horizontal: rows) or down (Vertical: columns) as many as fit.
void place_grid(const Instance& lay, Rect box, const std::vector<Instance*>& kids, std::vector<PlacedChild>& out) {
    if (kids.empty()) return;
    const vec3 cell = lay.get_vec3("CellSize", {100, 100, 0});
    const vec3 pad = lay.get_vec3("CellPadding", {5, 5, 0});
    const bool horizontal = enum_at(lay, "FillDirection", 1) == 1;
    const usize n = kids.size();
    const f32 step_x = cell.x + pad.x, step_y = cell.y + pad.y;
    usize across = n; // cells per row (Horizontal) or per column (Vertical)
    if (horizontal && step_x > 0.0f)
        across = cells_that_fit(box.size.x + pad.x, step_x, n);
    else if (!horizontal && step_y > 0.0f)
        across = cells_that_fit(box.size.y + pad.y, step_y, n);
    across = std::max<usize>(1, std::min(across, n));
    const usize lines = (n + across - 1) / across;
    const f32 cols = static_cast<f32>(horizontal ? across : lines);
    const f32 rows = static_cast<f32>(horizontal ? lines : across);
    const f32 block_w = cols * cell.x + (cols - 1.0f) * pad.x;
    const f32 block_h = rows * cell.y + (rows - 1.0f) * pad.y;
    const f32 ox = box.min.x + align_offset(enum_at(lay, "HorizontalAlignment"), box.size.x, block_w);
    const f32 oy = box.min.y + align_offset(enum_at(lay, "VerticalAlignment"), box.size.y, block_h);
    for (usize i = 0; i < n; ++i) {
        const usize line = i / across, idx = i % across;
        const f32 col = static_cast<f32>(horizontal ? idx : line);
        const f32 row = static_cast<f32>(horizontal ? line : idx);
        out.push_back({kids[i], Rect{{ox + col * step_x, oy + row * step_y}, {cell.x, cell.y}}});
    }
}

// Screens visible under `root`, ordered by DisplayOrder (the last one draws on top).
std::vector<Instance*> visible_screens(Instance& root) {
    std::vector<Instance*> screens;
    for (Instance* s : root.children())
        if (s->is_a("ScreenGui") && s->get_bool("Enabled", true)) screens.push_back(s);
    std::stable_sort(screens.begin(), screens.end(), [](const Instance* a, const Instance* b) {
        return a->get_float("DisplayOrder", 0.0f) < b->get_float("DisplayOrder", 0.0f);
    });
    return screens;
}

// The part a billboard or name plate sits on: a BasePart itself, or the character of a Model.
Instance* anchor_of(Instance* p, DataModel& dm) {
    if (!p) return nullptr;
    if (p->is_a("BasePart")) return p;
    if (!p->is_a("Model")) return nullptr;
    if (const Value* v = p->get("PrimaryPart"))
        if (v->ref)
            if (Instance* pp = dm.find(v->ref)) return pp;
    if (Instance* head = p->find_first_child("Head")) return head;
    return p->find_first_child("HumanoidRootPart");
}

const Instance* local_character(DataModel& dm) {
    Instance* players = dm.find_path("Players");
    if (!players) return nullptr;
    Instance* player = players->find_first_child("Player");
    if (!player) return nullptr;
    const Value* v = player->get("Character");
    return v && v->ref ? dm.find(v->ref) : nullptr;
}

void append_utf8(std::string& s, u32 cp) {
    auto put = [&s](u32 b) { s.push_back(static_cast<char>(static_cast<u8>(b))); };
    if (cp < 0x80) {
        put(cp);
    } else if (cp < 0x800) {
        put(0xC0 | (cp >> 6));
        put(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        put(0xE0 | (cp >> 12));
        put(0x80 | ((cp >> 6) & 0x3F));
        put(0x80 | (cp & 0x3F));
    } else {
        put(0xF0 | (cp >> 18));
        put(0x80 | ((cp >> 12) & 0x3F));
        put(0x80 | ((cp >> 6) & 0x3F));
        put(0x80 | (cp & 0x3F));
    }
}

void pop_utf8(std::string& s) {
    while (!s.empty() && (static_cast<u8>(s.back()) & 0xC0) == 0x80) s.pop_back();
    if (!s.empty()) s.pop_back();
}

} // namespace

// ---- pure layout ---------------------------------------------------------------------------------------------------

Rect layout_rect(const Instance& g, Rect parent) {
    const vec3 pos = g.get_vec3("Position"), poff = g.get_vec3("PositionOffset");
    const vec3 size = g.get_vec3("Size"), soff = g.get_vec3("SizeOffset");
    const vec3 anchor = g.get_vec3("AnchorPoint");
    Rect r;
    r.size = {parent.size.x * size.x + soff.x, parent.size.y * size.y + soff.y};
    if (const Instance* ar = child_of(g, "UIAspectRatioConstraint")) {
        const f32 ratio = ar->get_float("AspectRatio", 1.0f);
        if (ratio > 0.0f && r.size.x > 0.0f) r.size.y = r.size.x / ratio; // the width drives the height
    }
    const vec2 p{parent.min.x + parent.size.x * pos.x + poff.x, parent.min.y + parent.size.y * pos.y + poff.y};
    r.min = {p.x - r.size.x * anchor.x, p.y - r.size.y * anchor.y};
    return r;
}

Rect container_rect(const Instance& parent, Rect rect) {
    const Instance* pad = child_of(parent, "UIPadding");
    if (!pad) return rect;
    const f32 l = pad->get_float("PaddingLeft"), r = pad->get_float("PaddingRight");
    const f32 t = pad->get_float("PaddingTop"), b = pad->get_float("PaddingBottom");
    rect.min = {rect.min.x + l, rect.min.y + t};
    rect.size = {std::max(0.0f, rect.size.x - l - r), std::max(0.0f, rect.size.y - t - b)};
    return rect;
}

std::vector<PlacedChild> layout_children(Instance& parent, Rect rect) {
    const Rect box = container_rect(parent, rect);
    std::vector<Instance*> kids;
    for (Instance* k : parent.children())
        if (k->is_a("GuiObject") && k->get_bool("Visible", true)) kids.push_back(k);
    std::vector<PlacedChild> out;
    const Instance* list = child_of(parent, "UIListLayout");
    const Instance* grid = list ? nullptr : child_of(parent, "UIGridLayout");
    if (list || grid) {
        const Instance& lay = list ? *list : *grid;
        sort_by_order(kids, enum_at(lay, "SortOrder"));
        if (list)
            place_list(lay, box, kids, out);
        else
            place_grid(lay, box, kids, out);
    } else {
        for (Instance* k : kids) out.push_back({k, layout_rect(*k, box)});
    }
    std::stable_sort(out.begin(), out.end(), [](const PlacedChild& a, const PlacedChild& b) {
        return a.object->get_float("ZIndex", 1.0f) < b.object->get_float("ZIndex", 1.0f);
    });
    return out;
}

f32 text_scaled_size(f32 base_size, vec2 box, vec2 text_size_at_base) {
    if (base_size <= 0.0f || text_size_at_base.x <= 0.0f || text_size_at_base.y <= 0.0f) return base_size;
    const f32 k = std::min(box.x * 0.95f / text_size_at_base.x, box.y * 0.8f / text_size_at_base.y);
    return std::max(1.0f, base_size * k);
}

GuiCamera make_gui_camera(vec3 position, f32 yaw, f32 pitch, f32 fov_y) {
    GuiCamera c;
    c.position = position;
    c.fov_y = fov_y;
    c.forward = normalize({-std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch)});
    // right = forward x world up (0, 1, 0); up = right x forward.
    c.right = normalize({-c.forward.z, 0.0f, c.forward.x});
    c.up = {c.right.y * c.forward.z - c.right.z * c.forward.y, c.right.z * c.forward.x - c.right.x * c.forward.z,
            c.right.x * c.forward.y - c.right.y * c.forward.x};
    return c;
}

bool world_to_screen(const GuiCamera& cam, vec3 world, Rect view, vec2* out) {
    const vec3 d = world - cam.position;
    const f32 z = dot(d, cam.forward);
    if (z <= 0.01f) return false;
    const f32 tan_half = std::tan(cam.fov_y * 0.5f);
    const f32 aspect = view.size.y > 0.0f ? view.size.x / view.size.y : 1.0f;
    const f32 nx = dot(d, cam.right) / (z * tan_half * aspect);
    const f32 ny = dot(d, cam.up) / (z * tan_half);
    out->x = view.min.x + (nx * 0.5f + 0.5f) * view.size.x;
    out->y = view.min.y + (0.5f - ny * 0.5f) * view.size.y;
    return true;
}

// ---- GameGui implementation -----------------------------------------------------------------------------------------

struct GameGui::Impl {
    struct Tex {
        void* id = nullptr;
        vec2 size{};
    };
    std::string asset_root;
    TextureLoader loader;
    std::unique_ptr<TextureCache> textures;
    std::unordered_map<std::string, Tex> loaded; // results of `loader`
    ImFont* fonts[3] = {nullptr, nullptr, nullptr};
    u64 pressed = 0; // button that received the mouse press
    u64 hovered = 0; // button under the cursor
    u64 focus = 0;   // TextBox with keyboard focus

    void* resolve_texture(const std::string& img, vec2* size) {
        const std::string path = asset_root.empty() || img[0] == '/' ? img : path_join(asset_root, img);
        if (textures) return textures->get(path, size);
        if (!loader) return nullptr;
        auto it = loaded.find(path);
        if (it == loaded.end()) {
            Tex t;
            t.id = loader(path, &t.size);
            it = loaded.emplace(path, t).first;
        }
        if (size) *size = it->second.size;
        return it->second.id;
    }

    // The font for a family: its own font, else the default one, else ImGui's current font.
    ImFont* font_for(GuiFont f) const {
        ImFont* font = fonts[static_cast<usize>(f)];
        if (!font) font = fonts[0];
        return font ? font : ImGui::GetFont();
    }
};

GameGui::GameGui() : impl_(std::make_unique<Impl>()) {}
GameGui::~GameGui() = default;

void GameGui::set_asset_root(std::string dir) { impl_->asset_root = std::move(dir); }
void GameGui::set_texture_loader(TextureLoader loader) { impl_->loader = std::move(loader); }
void GameGui::enable_textures(const rhi::Device& device) {
    if (!impl_->textures) impl_->textures = std::make_unique<TextureCache>(device);
}
void GameGui::set_fonts(ImFont* regular, ImFont* bold, ImFont* mono) {
    impl_->fonts[0] = regular;
    impl_->fonts[1] = bold;
    impl_->fonts[2] = mono;
}
bool GameGui::wants_keyboard() const { return impl_->focus != 0; }

namespace {

struct DrawCtx {
    ImDrawList* dl;
    GameGui::Impl* impl;
    InputState* input;
    bool interactive;
    vec2 mouse;      // screen coordinates
    u64 top_hit = 0; // topmost button under the cursor (last drawn wins)
    u64 top_box = 0; // topmost TextBox under the cursor
};

void draw_object(DrawCtx& c, Instance& g, Rect r, ImVec2 clip_min, ImVec2 clip_max);

void draw_children(DrawCtx& c, Instance& parent, Rect rect, ImVec2 clip_min, ImVec2 clip_max) {
    for (const PlacedChild& p : layout_children(parent, rect)) draw_object(c, *p.object, p.rect, clip_min, clip_max);
}

void draw_gradient(ImDrawList* dl, const Instance& grad, ImVec2 a, ImVec2 b, f32 bg_t) {
    const vec3 c0 = grad.get_vec3("Color0", {1, 1, 1}), c1 = grad.get_vec3("Color1", {0.5f, 0.5f, 0.5f});
    const f32 rad = radians(grad.get_float("Rotation"));
    const f32 dx = std::cos(rad), dy = std::sin(rad);
    const ImVec2 centre{(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f};
    const f32 half = std::fabs(dx) * (b.x - a.x) * 0.5f + std::fabs(dy) * (b.y - a.y) * 0.5f;
    auto at = [&](f32 x, f32 y) {
        const f32 t =
            half > 0.0f ? clamp(0.5f + ((x - centre.x) * dx + (y - centre.y) * dy) / (2.0f * half), 0.0f, 1.0f) : 0.5f;
        return srgb_color(lerp(c0, c1, t), bg_t);
    };
    dl->AddRectFilledMultiColor(a, b, at(a.x, a.y), at(b.x, a.y), at(b.x, b.y), at(a.x, b.y));
}

void draw_background(DrawCtx& c, Instance& g, ImVec2 a, ImVec2 b, f32 radius, f32 bg_t, bool button) {
    if (const Instance* grad = child_of(g, "UIGradient")) {
        draw_gradient(c.dl, *grad, a, b, bg_t);
        return;
    }
    vec3 bg = g.get_vec3("BackgroundColor", {1, 1, 1});
    if (button && g.get_bool("AutoButtonColor", true) && c.interactive && c.impl->hovered == g.id())
        bg = bg * (c.impl->pressed == g.id() ? 0.7f : 0.85f);
    c.dl->AddRectFilled(a, b, srgb_color(bg, bg_t), radius);
}

void draw_border(DrawCtx& c, Instance& g, ImVec2 a, ImVec2 b, f32 radius, f32 bg_t) {
    f32 thick = g.get_float("BorderSize");
    vec3 col = g.get_vec3("BorderColor");
    f32 tr = bg_t;
    if (const Instance* st = child_of(g, "UIStroke")) {
        thick = st->get_float("Thickness", 1.0f);
        col = st->get_vec3("Color", {1, 1, 1});
        tr = st->get_float("Transparency");
    }
    if (thick > 0.0f) c.dl->AddRect(a, b, srgb_color(col, tr), radius, 0, thick);
}

void draw_image(DrawCtx& c, Instance& g, Rect r, ImVec2 a, ImVec2 b, f32 radius) {
    const std::string& img = g.get_string("Image");
    if (img.empty()) return;
    vec2 isz{};
    void* tex = c.impl->resolve_texture(img, &isz);
    if (!tex || isz.x <= 0.0f || isz.y <= 0.0f) return;
    const ImU32 tint = srgb_color(g.get_vec3("ImageColor", {1, 1, 1}), g.get_float("ImageTransparency"));
    const ImTextureID id = reinterpret_cast<ImTextureID>(tex);
    const f32 bw = r.size.x, bh = r.size.y;
    const f32 img_ar = isz.x / isz.y, box_ar = bh > 0.0f ? bw / bh : 1.0f;
    const u32 scale = enum_at(g, "ScaleType");
    if (scale == 1) { // Fit: the whole image inside the box, keeping its aspect
        const vec2 s = img_ar > box_ar ? vec2{bw, bw / img_ar} : vec2{bh * img_ar, bh};
        const ImVec2 p0{r.min.x + (bw - s.x) * 0.5f, r.min.y + (bh - s.y) * 0.5f};
        c.dl->AddImage(id, p0, {p0.x + s.x, p0.y + s.y}, {0, 0}, {1, 1}, tint);
        return;
    }
    ImVec2 uv0{0, 0}, uv1{1, 1};
    if (scale == 2) { // Crop: fill the box, cutting the overflow evenly from both sides
        if (img_ar > box_ar) {
            uv0.x = (1.0f - box_ar / img_ar) * 0.5f;
            uv1.x = 1.0f - uv0.x;
        } else {
            uv0.y = (1.0f - img_ar / box_ar) * 0.5f;
            uv1.y = 1.0f - uv0.y;
        }
    }
    c.dl->AddImageRounded(id, a, b, uv0, uv1, tint, radius);
}

void draw_text(DrawCtx& c, Instance& g, Rect r, ImVec2 a, ImVec2 b) {
    const bool box = g.is_a("TextBox");
    const bool focused = box && c.impl->focus == g.id();
    std::string text = g.get_string("Text");
    bool placeholder = false;
    if (text.empty() && box && !focused) {
        text = g.get_string("PlaceholderText");
        placeholder = true;
    }
    if (text.empty() && !focused) return;
    const GuiFont family = static_cast<GuiFont>(std::min<u32>(enum_at(g, "Font"), 2));
    // A TextSize of 0 or a negative one from a script would make ImGui bake an empty or huge glyph set; keep it sane.
    f32 size = clamp(g.get_float("TextSize", 20.0f), kMinTextPx, kMaxTextPx);
    ImFont* font = c.impl->font_for(family);
    ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str());
    if (g.get_bool("TextScaled") && ts.y > 0.0f) {
        size = clamp(text_scaled_size(size, {r.size.x, r.size.y}, {ts.x, ts.y}), kMinTextPx, kMaxTextPx);
        ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str());
    }
    const u32 xal = enum_at(g, "TextXAlignment", 1), yal = enum_at(g, "TextYAlignment", 1);
    const f32 x = xal == 0 ? a.x + 4 : xal == 2 ? b.x - ts.x - 4 : a.x + (r.size.x - ts.x) * 0.5f;
    const f32 y = yal == 0 ? a.y + 2 : yal == 2 ? b.y - ts.y - 2 : a.y + (r.size.y - ts.y) * 0.5f;
    f32 tt = g.get_float("TextTransparency", 0.0f);
    if (placeholder) tt = std::max(tt, 0.55f);
    const f32 st = g.get_float("TextStrokeTransparency", 1.0f);
    if (st < 1.0f && !text.empty()) {
        const ImU32 sc = srgb_color({0, 0, 0}, std::max(st, tt));
        for (int dx = -1; dx <= 1; ++dx)
            for (int dy = -1; dy <= 1; ++dy)
                if (dx || dy) c.dl->AddText(font, size, {x + f32(dx), y + f32(dy)}, sc, text.c_str());
    }
    if (!text.empty())
        c.dl->AddText(font, size, {x, y}, srgb_color(g.get_vec3("TextColor", {0, 0, 0}), tt), text.c_str());
    if (focused && std::fmod(static_cast<f32>(ImGui::GetTime()), 1.0f) < 0.5f) { // caret after the text
        const f32 cx = x + ts.x + 1.0f;
        c.dl->AddRectFilled({cx, y}, {cx + 2.0f, y + ts.y}, srgb_color(g.get_vec3("TextColor", {1, 1, 1}), 0.0f));
    }
}

// Draws one GuiObject at `r` (its layout rectangle), then its children, then rotates everything it drew.
void draw_object(DrawCtx& c, Instance& g, Rect r, ImVec2 clip_min, ImVec2 clip_max) {
    const ImVec2 a{r.min.x, r.min.y}, b{r.min.x + r.size.x, r.min.y + r.size.y};
    const f32 rot = g.get_float("Rotation");
    const int v0 = c.dl->VtxBuffer.Size;
    const bool button = g.is_a("TextButton") || g.is_a("ImageButton");
    const bool textbox = g.is_a("TextBox");
    const f32 bg_t = g.get_float("BackgroundTransparency", 0.0f);
    f32 radius = g.get_float("CornerRadius", 0.0f);
    if (const Instance* corner = child_of(g, "UICorner")) radius = corner->get_float("CornerRadius", radius);

    c.dl->PushClipRect(clip_min, clip_max, true);
    if (bg_t < 1.0f) draw_background(c, g, a, b, radius, bg_t, button);
    draw_border(c, g, a, b, radius, bg_t);
    if (g.is_a("ImageLabel")) draw_image(c, g, r, a, b, radius);
    if (g.is_a("TextLabel")) draw_text(c, g, r, a, b);
    c.dl->PopClipRect();

    // Hit test: a later (higher) object replaces the hit; an opaque Frame clears a button underneath it.
    const bool in_clip =
        c.mouse.x >= clip_min.x && c.mouse.y >= clip_min.y && c.mouse.x < clip_max.x && c.mouse.y < clip_max.y;
    if (c.interactive && in_clip && inside(r, c.mouse) && (button || textbox || bg_t < 0.99f)) {
        c.top_hit = button ? g.id() : 0;
        c.top_box = textbox ? g.id() : 0;
        if (c.input) c.input->mouse_over_ui = true;
    }

    ImVec2 child_min = clip_min, child_max = clip_max;
    if (g.get_bool("ClipsDescendants")) {
        child_min = {std::max(clip_min.x, a.x), std::max(clip_min.y, a.y)};
        child_max = {std::max(child_min.x, std::min(clip_max.x, b.x)),
                     std::max(child_min.y, std::min(clip_max.y, b.y))};
    }
    draw_children(c, g, r, child_min, child_max);
    if (rot != 0.0f) rotate_vertices(c.dl, v0, {(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f}, rot);
}

// BillboardGui: drawn at the projected Adornee (+ StudsOffset), centred on it, never interactive.
void draw_billboards(DrawCtx& c, DataModel& dm, Instance& workspace, const GuiCamera& cam, Rect view) {
    for (Instance* d : workspace.descendants()) {
        if (!d->is_a("BillboardGui") || !d->get_bool("Enabled", true)) continue;
        const Value* ref = d->get("Adornee");
        Instance* target = ref && ref->ref ? dm.find(ref->ref) : d->parent();
        Instance* part = anchor_of(target, dm);
        if (!part) continue;
        const vec3 world = part->get_vec3("Position") + d->get_vec3("StudsOffset");
        if (length(world - cam.position) > d->get_float("MaxDistance", 100.0f)) continue;
        vec2 sp{};
        if (!world_to_screen(cam, world, view, &sp)) continue;
        const vec3 sz = d->get_vec3("Size", {200, 50, 0});
        const Rect r{{sp.x - sz.x * 0.5f, sp.y - sz.y * 0.5f}, {sz.x, sz.y}};
        const ImVec2 cmin{r.min.x, r.min.y}, cmax{r.min.x + r.size.x, r.min.y + r.size.y};
        DrawCtx bc = c;
        bc.interactive = false;
        bc.input = nullptr;
        draw_children(bc, *d, r, cmin, cmax);
    }
}

// Name plates: the DisplayName of every Humanoid, above its head (DisplayDistanceType: Viewer, Subject, None).
void draw_nameplates(DrawCtx& c, DataModel& dm, Instance& workspace, const GuiCamera& cam, Rect view) {
    const Instance* local = local_character(dm);
    for (Instance* h : workspace.descendants()) {
        if (!h->is_a("Humanoid")) continue;
        const std::string name = h->get_string("DisplayName");
        const u32 kind = enum_at(*h, "DisplayDistanceType"); // 0 Viewer, 1 Subject, 2 None
        if (name.empty() || kind == 2) continue;
        Instance* model = h->parent();
        if (!model || (kind == 1 && model != local)) continue;
        vec3 p{};
        if (Instance* head = model->find_first_child("Head"))
            p = head->get_vec3("Position") + vec3{0.0f, head->get_vec3("Size").y * 0.5f + 0.5f, 0.0f};
        else if (Instance* root = model->find_first_child("HumanoidRootPart"))
            p = root->get_vec3("Position") + vec3{0.0f, 2.5f, 0.0f};
        else
            continue;
        if (length(p - cam.position) > h->get_float("NameDisplayDistance", 100.0f)) continue;
        vec2 sp{};
        if (!world_to_screen(cam, p, view, &sp)) continue;
        ImFont* font = c.impl->font_for(GuiFont::Bold);
        const ImVec2 ts = font->CalcTextSizeA(16.0f, FLT_MAX, 0.0f, name.c_str());
        const ImVec2 a{sp.x - ts.x * 0.5f - 6.0f, sp.y - ts.y - 10.0f}, b{sp.x + ts.x * 0.5f + 6.0f, sp.y - 2.0f};
        c.dl->AddRectFilled(a, b, srgb_color({0, 0, 0}, 0.6f), 4.0f);
        c.dl->AddText(font, 16.0f, {a.x + 6.0f, a.y + 5.0f}, srgb_color({1, 1, 1}, 0.0f), name.c_str());
    }
}

void unfocus(GameGui::Impl& im, DataModel& dm, f64 number) {
    if (!im.focus) return;
    const u64 id = im.focus;
    im.focus = 0;
    if (Instance* box = dm.find(id)) dm.fire_event(*box, "FocusLost", nullptr, number);
}

void focus_box(GameGui::Impl& im, DataModel& dm, Instance& box) {
    if (im.focus == box.id()) return;
    unfocus(im, dm, 0.0);
    im.focus = box.id();
    if (box.get_bool("ClearTextOnFocus", true)) box.set("Text", Value::string(""));
    dm.fire_event(box, "Focused");
}

// Typing into the focused TextBox: printable characters from ImGui's queue, Backspace, Enter (FocusLost 1) and
// Escape (FocusLost 0). Skipped while another ImGui widget (a Studio panel) owns the text input.
void edit_focused(GameGui::Impl& im, DataModel& dm) {
    Instance* box = dm.find(im.focus);
    if (!box) {
        im.focus = 0;
        return;
    }
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) return;
    std::string text = box->get_string("Text");
    bool changed = false;
    for (ImWchar ch : io.InputQueueCharacters) {
        const u32 cp = static_cast<u32>(ch);
        if (cp >= 32 && cp != 127) {
            append_utf8(text, cp);
            changed = true;
        }
    }
    io.InputQueueCharacters.resize(0);
    if (ImGui::IsKeyPressed(ImGuiKey_Backspace, true)) {
        pop_utf8(text);
        changed = true;
    }
    if (changed) box->set("Text", Value::string(text));
    if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false))
        unfocus(im, dm, 1.0);
    else if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        unfocus(im, dm, 0.0);
}

// Hover, clicks and focus changes for the view.
void process_mouse(DrawCtx& c, GameGui::Impl& im, DataModel& dm, InputState* input) {
    if (!input) return;
    const u64 target = c.interactive ? c.top_hit : 0;
    if (target != im.hovered) {
        if (Instance* old = dm.find(im.hovered)) dm.fire_event(*old, "MouseLeave");
        im.hovered = target;
        if (Instance* now = dm.find(im.hovered)) dm.fire_event(*now, "MouseEnter");
    }
    if (!c.interactive) {
        if (input->mouse_released[0]) im.pressed = 0;
        return;
    }
    if (input->mouse_pressed[0]) {
        im.pressed = c.top_hit;
        if (c.top_box) {
            if (Instance* box = dm.find(c.top_box)) focus_box(im, dm, *box);
        } else {
            unfocus(im, dm, 0.0); // a click elsewhere in the view ends text entry
        }
    }
    if (input->mouse_released[0]) {
        if (im.pressed && im.pressed == c.top_hit)
            if (Instance* btn = dm.find(im.pressed)) {
                dm.fire_event(*btn, "MouseButton1Click");
                dm.fire_event(*btn, "Activated");
            }
        im.pressed = 0;
    }
}

Instance* pick_in(Instance& parent, Rect rect, vec2 mouse, const Rect* clip) {
    Rect inner{};
    const Rect* child_clip = clip;
    if (parent.get_bool("ClipsDescendants")) {
        inner = clip ? intersect(rect, *clip) : rect;
        child_clip = &inner;
    }
    const std::vector<PlacedChild> kids = layout_children(parent, rect);
    for (usize i = kids.size(); i-- > 0;) {
        const PlacedChild& p = kids[i];
        if (Instance* deeper = pick_in(*p.object, p.rect, mouse, child_clip)) return deeper;
        if (inside(p.rect, mouse) && (!child_clip || inside(*child_clip, mouse))) return p.object;
    }
    return nullptr;
}

} // namespace

void GameGui::draw(ImDrawList* dl, Rect view, Instance& root, InputState* input, bool interactive,
                   const GuiCamera* camera) {
    if (!dl) return;
    // The editor preview of StarterGui (no input) obeys StarterGui.ShowDevelopmentGui.
    if (!interactive && root.is_a("StarterGui") && !root.get_bool("ShowDevelopmentGui", true)) return;
    DataModel& dm = root.model();
    Impl& im = *impl_;
    if (im.focus && !dm.find(im.focus)) im.focus = 0;
    DrawCtx c{dl, &im, input, interactive, {}, 0, 0};
    if (input) {
        c.mouse = {view.min.x + input->mouse_pos.x, view.min.y + input->mouse_pos.y};
        input->mouse_over_ui = false;
        if (!input->hovered) c.interactive = false;
    } else {
        c.interactive = false;
    }
    const ImVec2 cmin{view.min.x, view.min.y}, cmax{view.min.x + view.size.x, view.min.y + view.size.y};
    for (Instance* s : visible_screens(root)) draw_children(c, *s, view, cmin, cmax);
    if (camera)
        if (Instance* ws = dm.find_path("Workspace")) {
            draw_billboards(c, dm, *ws, *camera, view);
            draw_nameplates(c, dm, *ws, *camera, view);
        }
    if (input && im.focus) edit_focused(im, dm); // text entry works even when the cursor is outside the view
    process_mouse(c, im, dm, input);
    if (input && im.focus) input->keyboard = false; // the game must not walk while typing
}

Instance* GameGui::pick(Rect view, Instance& root, vec2 mouse) {
    const std::vector<Instance*> screens = visible_screens(root);
    for (usize i = screens.size(); i-- > 0;)
        if (Instance* hit = pick_in(*screens[i], view, mouse, nullptr)) return hit;
    return nullptr;
}

} // namespace tefra::ui
