// Tefra Studio panels (spec M3 §1): menu, toolbar, Explorer, Properties, Viewport (camera, picking, gizmos),
// Output and the C# script editor. Everything edits the place through Session so undo/redo stays consistent.
#include "tefra/studio/app.h"

#include "tefra/core/log.h"
#include "tefra/world/scene_sync.h"

#include <imgui.h>
#include <imgui_internal.h>
// ImGuizmo needs imgui.h first.
#include <ImGuizmo.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace tefra::studio {

using world::Instance;
using world::PropInfo;
using world::PropType;
using world::Value;

namespace {

// Short badge and colour per class family, shown in the Explorer.
struct Badge {
    const char* text;
    ImU32 color;
};
Badge class_badge(const Instance& i) {
    if (i.cls().service) return {"S", IM_COL32(150, 150, 160, 255)};
    if (i.is_a("SpawnLocation")) return {"Sp", IM_COL32(90, 170, 255, 255)};
    if (i.is_a("BasePart")) return {"P", IM_COL32(120, 200, 120, 255)};
    if (i.is_a("Model")) return {"M", IM_COL32(230, 170, 80, 255)};
    if (i.is_a("Folder")) return {"F", IM_COL32(220, 200, 120, 255)};
    if (i.is_a("BaseScript") || i.is_a("ModuleScript")) return {"C#", IM_COL32(170, 120, 255, 255)};
    if (i.is_a("Light")) return {"L", IM_COL32(255, 230, 120, 255)};
    if (i.is_a("Humanoid") || i.is_a("Animator") || i.is_a("Motor6D")) return {"H", IM_COL32(255, 140, 140, 255)};
    return {"I", IM_COL32(170, 170, 180, 255)};
}

// Column-major 4x4 helpers for ImGuizmo (OpenGL conventions: -Z forward, clip z in [-1, 1]).
void mat_from_trs(vec3 t, quat q, vec3 s, f32 m[16]) {
    const vec3 x = rotate(q, {1, 0, 0}) * s.x, y = rotate(q, {0, 1, 0}) * s.y, z = rotate(q, {0, 0, 1}) * s.z;
    const f32 v[16] = {x.x, x.y, x.z, 0, y.x, y.y, y.z, 0, z.x, z.y, z.z, 0, t.x, t.y, t.z, 1};
    std::memcpy(m, v, sizeof(v));
}
quat quat_from_basis(vec3 x, vec3 y, vec3 z) {
    const f32 tr = x.x + y.y + z.z;
    quat q;
    if (tr > 0.0f) {
        const f32 s = std::sqrt(tr + 1.0f) * 2.0f;
        q = {(y.z - z.y) / s, (z.x - x.z) / s, (x.y - y.x) / s, 0.25f * s};
    } else if (x.x > y.y && x.x > z.z) {
        const f32 s = std::sqrt(1.0f + x.x - y.y - z.z) * 2.0f;
        q = {0.25f * s, (y.x + x.y) / s, (z.x + x.z) / s, (y.z - z.y) / s};
    } else if (y.y > z.z) {
        const f32 s = std::sqrt(1.0f + y.y - x.x - z.z) * 2.0f;
        q = {(y.x + x.y) / s, 0.25f * s, (z.y + y.z) / s, (z.x - x.z) / s};
    } else {
        const f32 s = std::sqrt(1.0f + z.z - x.x - y.y) * 2.0f;
        q = {(z.x + x.z) / s, (z.y + y.z) / s, 0.25f * s, (x.y - y.x) / s};
    }
    return normalize(q);
}
void mat_to_trs(const f32 m[16], vec3& t, quat& q, vec3& s) {
    vec3 x{m[0], m[1], m[2]}, y{m[4], m[5], m[6]}, z{m[8], m[9], m[10]};
    s = {length(x), length(y), length(z)};
    x = x / max(s.x, 1e-6f);
    y = y / max(s.y, 1e-6f);
    z = z / max(s.z, 1e-6f);
    q = quat_from_basis(x, y, z);
    t = {m[12], m[13], m[14]};
}
void mat_mul(const f32 a[16], const f32 b[16], f32 out[16]) {
    f32 r[16];
    for (int c = 0; c < 4; ++c)
        for (int row = 0; row < 4; ++row) {
            f32 sum = 0.0f;
            for (int k = 0; k < 4; ++k) sum += a[k * 4 + row] * b[c * 4 + k];
            r[c * 4 + row] = sum;
        }
    std::memcpy(out, r, sizeof(r));
}
bool mat_inverse_affine(const f32 m[16], f32 out[16]) {
    // General 4x4 inverse (cofactors) - small, used once per gizmo frame.
    f32 inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] +
             m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] -
             m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] +
             m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] -
              m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] -
             m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] +
             m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] -
             m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] +
              m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] +
             m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] -
             m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] +
              m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] -
              m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] -
             m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] +
             m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] -
              m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] -
              m[8] * m[2] * m[5];
    f32 det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (std::fabs(det) < 1e-12f) return false;
    det = 1.0f / det;
    for (int i = 0; i < 16; ++i) out[i] = inv[i] * det;
    return true;
}

int text_resize_cb(ImGuiInputTextCallbackData* data) {
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        auto* str = static_cast<std::string*>(data->UserData);
        str->resize(static_cast<usize>(data->BufTextLen));
        data->Buf = str->data();
    }
    return 0;
}
bool input_string(const char* label, std::string& s, ImGuiInputTextFlags flags = 0) {
    return ImGui::InputText(label, s.data(), s.capacity() + 1, flags | ImGuiInputTextFlags_CallbackResize,
                            text_resize_cb, &s);
}

} // namespace

// ---- Top level --------------------------------------------------------------------------------------------------

void App::draw_ui(f32 dt) {
    ImGuizmo::BeginFrame();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::SetNextWindowViewport(vp->ID);
    const ImGuiWindowFlags host_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                                        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                                        ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("##host", nullptr, host_flags);
    ImGui::PopStyleVar(2);
    draw_menu_bar();
    draw_toolbar();
    const ImGuiID dock = ImGui::GetID("StudioDock");
    if (!layout_built_ && ImGui::DockBuilderGetNode(dock) == nullptr) {
        // Default layout: Explorer and Properties on the right, Output below the viewport.
        ImGui::DockBuilderRemoveNode(dock);
        ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dock, ImGui::GetContentRegionAvail());
        ImGuiID main = dock;
        ImGuiID right = ImGui::DockBuilderSplitNode(main, ImGuiDir_Right, 0.24f, nullptr, &main);
        ImGuiID right_bottom = ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.55f, nullptr, &right);
        ImGuiID bottom = ImGui::DockBuilderSplitNode(main, ImGuiDir_Down, 0.24f, nullptr, &main);
        ImGui::DockBuilderDockWindow("Explorer", right);
        ImGui::DockBuilderDockWindow("Properties", right_bottom);
        ImGui::DockBuilderDockWindow("Output", bottom);
        ImGui::DockBuilderDockWindow("Viewport", main);
        ImGui::DockBuilderFinish(dock);
    }
    layout_built_ = true;
    ImGui::DockSpace(dock, {0, 0}, ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::End();

    draw_viewport(dt);
    draw_explorer();
    draw_properties();
    draw_output();
    draw_script_editors();
    draw_file_dialogs();
    handle_shortcuts();
}

void App::handle_shortcuts() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) return;
    const bool ctrl = io.KeyCtrl;
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z)) session_.undo();
    if (ctrl && (ImGui::IsKeyPressed(ImGuiKey_Y) || (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z)))) session_.redo();
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_D)) session_.duplicate_selection();
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_G)) session_.group_selection();
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S)) {
        if (session_.path().empty())
            show_save_dialog_ = true;
        else
            session_.save();
    }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_O)) show_open_dialog_ = true;
    if (ImGui::IsKeyPressed(ImGuiKey_Delete)) session_.destroy_selection();
    if (ImGui::IsKeyPressed(ImGuiKey_F5)) {
        if (session_.play_state() == PlayState::Edit)
            session_.play();
        else
            session_.stop();
    }
    if (!io.MouseDown[1] && !ctrl) {
        if (ImGui::IsKeyPressed(ImGuiKey_1)) tool_ = Tool::Select;
        if (ImGui::IsKeyPressed(ImGuiKey_2)) tool_ = Tool::Move;
        if (ImGui::IsKeyPressed(ImGuiKey_3)) tool_ = Tool::Scale;
        if (ImGui::IsKeyPressed(ImGuiKey_4)) tool_ = Tool::Rotate;
        if (ImGui::IsKeyPressed(ImGuiKey_F) && viewport_hovered_) focus_selection();
    }
}

void App::draw_menu_bar() {
    if (!ImGui::BeginMenuBar()) return;
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New Baseplate")) session_.new_place(true);
        if (ImGui::MenuItem("New Empty")) session_.new_place(false);
        if (ImGui::MenuItem("Demo Scene")) {
            session_.new_place(false);
            build_demo_scene();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Open...", "Ctrl+O")) show_open_dialog_ = true;
        if (ImGui::MenuItem("Save", "Ctrl+S", false, session_.play_state() == PlayState::Edit)) {
            if (session_.path().empty())
                show_save_dialog_ = true;
            else
                session_.save();
        }
        if (ImGui::MenuItem("Save As...", nullptr, false, session_.play_state() == PlayState::Edit))
            show_save_dialog_ = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Quit")) quit_ = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        const std::string undo = "Undo " + session_.history().undo_label();
        const std::string redo = "Redo " + session_.history().redo_label();
        if (ImGui::MenuItem(undo.c_str(), "Ctrl+Z", false, session_.history().can_undo())) session_.undo();
        if (ImGui::MenuItem(redo.c_str(), "Ctrl+Y", false, session_.history().can_redo())) session_.redo();
        ImGui::Separator();
        if (ImGui::MenuItem("Duplicate", "Ctrl+D")) session_.duplicate_selection();
        if (ImGui::MenuItem("Group as Model", "Ctrl+G")) session_.group_selection();
        if (ImGui::MenuItem("Delete", "Del")) session_.destroy_selection();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Insert")) {
        Instance* parent = session_.primary();
        if (!parent || parent->is_a("BasePart") == false) parent = nullptr;
        draw_insert_menu(parent);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem("Snap to grid", nullptr, &snap_);
        ImGui::SliderFloat("Move snap", &move_snap_, 0.05f, 4.0f, "%.2f m");
        ImGui::SliderFloat("Rotate snap", &rotate_snap_, 1.0f, 90.0f, "%.0f deg");
        ImGui::MenuItem("Gizmo in local space", nullptr, &gizmo_local_);
        ImGui::EndMenu();
    }
    // Place name + modified marker on the right.
    std::string title = session_.path().empty() ? std::string("Untitled") : session_.path();
    if (session_.modified()) title += " *";
    const f32 w = ImGui::CalcTextSize(title.c_str()).x;
    ImGui::SameLine(ImGui::GetWindowWidth() - w - 16.0f);
    ImGui::TextDisabled("%s", title.c_str());
    ImGui::EndMenuBar();
}

void App::draw_insert_menu(Instance* parent) {
    // Classes grouped by their Insert category.
    std::vector<std::string> cats;
    for (const world::ClassInfo* c : world::ClassRegistry::get().all())
        if (c->creatable && !c->category.empty() && std::find(cats.begin(), cats.end(), c->category) == cats.end())
            cats.push_back(c->category);
    for (const std::string& cat : cats) {
        if (!ImGui::BeginMenu(cat.c_str())) continue;
        for (const world::ClassInfo* c : world::ClassRegistry::get().all()) {
            if (!c->creatable || c->category != cat) continue;
            if (ImGui::MenuItem(c->name.c_str())) {
                Instance* inst = session_.create(c->name, parent);
                // Drop new parts in front of the camera.
                if (inst && inst->is_a("BasePart") && !parent) {
                    const vec3 f = camera_.forward();
                    const vec3 p =
                        vec3{f32(camera_.position.x), f32(camera_.position.y), f32(camera_.position.z)} + f * 12.0f;
                    world::RayHit hit = sync_ ? sync_->raycast(vec3{f32(camera_.position.x), f32(camera_.position.y),
                                                                    f32(camera_.position.z)},
                                                               f, 60.0f, inst)
                                              : world::RayHit{};
                    const vec3 at = hit.instance ? hit.position + vec3{0, inst->get_vec3("Size").y * 0.5f, 0} : p;
                    inst->set("Position", Value::vector3(at));
                }
                if (inst && (inst->is_a("BaseScript") || inst->is_a("ModuleScript"))) open_script(inst->id());
            }
            if (ImGui::IsItemHovered() && !c->doc.empty()) ImGui::SetTooltip("%s", c->doc.c_str());
        }
        ImGui::EndMenu();
    }
}

void App::draw_toolbar() {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {10, 5});
    ImGui::Dummy({4, 0});
    ImGui::SameLine();
    auto tool_button = [&](const char* label, Tool t, const char* tip) {
        const bool on = tool_ == t;
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::Button(label)) tool_ = t;
        if (on) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
        ImGui::SameLine();
    };
    tool_button("Select", Tool::Select, "Select (1)");
    tool_button("Move", Tool::Move, "Move (2)");
    tool_button("Scale", Tool::Scale, "Scale (3)");
    tool_button("Rotate", Tool::Rotate, "Rotate (4)");
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    const PlayState ps = session_.play_state();
    if (ps == PlayState::Edit) {
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(40, 110, 50, 255));
        if (ImGui::Button("  Play  ")) session_.play();
        ImGui::PopStyleColor();
    } else {
        if (ImGui::Button(ps == PlayState::Paused ? "Resume" : "Pause")) session_.pause(ps != PlayState::Paused);
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(140, 40, 40, 255));
        if (ImGui::Button("  Stop  ")) session_.stop();
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::Text("Playing  %.1f s", session_.play_time());
    }
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::Checkbox("Snap", &snap_);
    ImGui::PopStyleVar();
}

// ---- Explorer ---------------------------------------------------------------------------------------------------

void App::draw_explorer() {
    if (!ImGui::Begin("Explorer")) {
        ImGui::End();
        return;
    }
    for (Instance* s : session_.model().root().children()) draw_explorer_node(*s, 0);
    ImGui::End();
}

void App::draw_explorer_node(Instance& inst, int depth) {
    ImGui::PushID(static_cast<int>(inst.id()));
    ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnDoubleClick;
    if (inst.children().empty()) flags |= ImGuiTreeNodeFlags_Leaf;
    if (session_.is_selected(inst.id())) flags |= ImGuiTreeNodeFlags_Selected;
    if (depth == 0 && inst.class_name() == "Workspace") flags |= ImGuiTreeNodeFlags_DefaultOpen;
    // Auto-expand to the selection.
    if (Instance* p = session_.primary(); p && p != &inst && p->is_descendant_of(&inst))
        ImGui::SetNextItemOpen(true, ImGuiCond_Always);
    const Badge b = class_badge(inst);
    bool open;
    if (renaming_ == inst.id()) {
        open = ImGui::TreeNodeEx("##node", flags | ImGuiTreeNodeFlags_AllowOverlap);
        ImGui::SameLine();
        ImGui::SetKeyboardFocusHere();
        if (ImGui::InputText("##rename", rename_buf_, sizeof(rename_buf_),
                             ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
            session_.rename(&inst, rename_buf_);
            renaming_ = 0;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape) || (!ImGui::IsItemActive() && ImGui::IsMouseClicked(0))) renaming_ = 0;
    } else {
        open = ImGui::TreeNodeEx("##node", flags, "%s", "");
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) session_.select(inst.id(), ImGui::GetIO().KeyCtrl);
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
            if (inst.is_a("BaseScript") || inst.is_a("ModuleScript"))
                open_script(inst.id());
            else
                focus_selection();
        }
        if (ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_F2) && !inst.cls().service) {
            renaming_ = inst.id();
            std::snprintf(rename_buf_, sizeof(rename_buf_), "%s", inst.name().c_str());
        }
        // Drag & drop: reparent.
        if (!inst.cls().service && ImGui::BeginDragDropSource()) {
            const u64 id = inst.id();
            ImGui::SetDragDropPayload("TEFRA_INSTANCE", &id, sizeof(id));
            ImGui::Text("%s", inst.name().c_str());
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("TEFRA_INSTANCE")) {
                u64 id = 0;
                std::memcpy(&id, pl->Data, sizeof(id));
                session_.reparent(session_.model().find(id), &inst);
            }
            ImGui::EndDragDropTarget();
        }
        if (ImGui::BeginPopupContextItem("ctx")) {
            if (!session_.is_selected(inst.id())) session_.select(inst.id());
            draw_context_menu(inst);
            ImGui::EndPopup();
        }
        // Badge + name drawn after the (empty) tree node label.
        ImGui::SameLine();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const ImVec2 ts = ImGui::CalcTextSize(b.text);
        dl->AddRectFilled({p.x, p.y + 1}, {p.x + ts.x + 6, p.y + ts.y + 1}, b.color & IM_COL32(255, 255, 255, 70),
                          3.0f);
        dl->AddText({p.x + 3, p.y + 1}, b.color, b.text);
        ImGui::Dummy({ts.x + 8, ts.y});
        ImGui::SameLine();
        ImGui::TextUnformatted(inst.name().c_str());
    }
    if (open) {
        for (Instance* c : inst.children()) draw_explorer_node(*c, depth + 1);
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void App::draw_context_menu(Instance& inst) {
    if (ImGui::BeginMenu("Insert Object")) {
        draw_insert_menu(&inst);
        ImGui::EndMenu();
    }
    const bool editable = !inst.cls().service;
    if (ImGui::MenuItem("Rename", "F2", false, editable)) {
        renaming_ = inst.id();
        std::snprintf(rename_buf_, sizeof(rename_buf_), "%s", inst.name().c_str());
    }
    if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, editable)) session_.duplicate_selection();
    if (ImGui::MenuItem("Group as Model", "Ctrl+G", false, editable)) session_.group_selection();
    if (ImGui::MenuItem("Focus", "F")) focus_selection();
    if (inst.is_a("BaseScript") || inst.is_a("ModuleScript"))
        if (ImGui::MenuItem("Edit Script")) open_script(inst.id());
    ImGui::Separator();
    if (ImGui::MenuItem("Delete", "Del", false, editable)) session_.destroy_selection();
}

// ---- Properties -------------------------------------------------------------------------------------------------

void App::draw_properties() {
    if (!ImGui::Begin("Properties")) {
        ImGui::End();
        return;
    }
    Instance* inst = session_.primary();
    if (!inst) {
        ImGui::TextDisabled("Select an object to see its properties.");
        ImGui::End();
        return;
    }
    ImGui::Text("%s", inst->name().c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("(%s)", inst->class_name().c_str());
    if (ImGui::IsItemHovered() && !inst->cls().doc.empty()) ImGui::SetTooltip("%s", inst->cls().doc.c_str());
    ImGui::Separator();

    // Collect properties by category, base classes first.
    std::vector<const PropInfo*> props;
    for (u32 i = 0; i < inst->cls().prop_count(); ++i) props.push_back(inst->cls().prop(i));
    std::vector<std::string> cats;
    for (const PropInfo* p : props)
        if (!(p->flags & world::kPropHidden) && std::find(cats.begin(), cats.end(), p->category) == cats.end())
            cats.push_back(p->category);

    if (ImGui::BeginTable("props", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthStretch, 0.42f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.58f);
        for (const std::string& cat : cats) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            const bool open =
                ImGui::TreeNodeEx(cat.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAllColumns);
            if (!open) continue;
            for (const PropInfo* p : props) {
                if (p->category != cat || (p->flags & world::kPropHidden)) continue;
                ImGui::PushID(static_cast<int>(p->index));
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(p->name.c_str());
                if (ImGui::IsItemHovered() && !p->doc.empty()) ImGui::SetTooltip("%s", p->doc.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::SetNextItemWidth(-1);
                const bool ro = (p->flags & world::kPropReadOnly) != 0;
                if (ro) ImGui::BeginDisabled();
                Value v = inst->get(p->index);
                bool changed = false, live = false;
                switch (p->type) {
                case PropType::Bool: {
                    bool b = v.as_bool();
                    if (ImGui::Checkbox("##v", &b)) {
                        v = Value::boolean(b);
                        changed = true;
                    }
                    break;
                }
                case PropType::Int: {
                    int x = static_cast<int>(v.as_int());
                    if (ImGui::DragInt("##v", &x)) {
                        v = Value::integer(x);
                        live = true;
                    }
                    break;
                }
                case PropType::Float: {
                    f32 x = v.as_float();
                    if (ImGui::DragFloat("##v", &x, 0.05f, 0.0f, 0.0f, "%.3f")) {
                        v = Value::number(x);
                        live = true;
                    }
                    break;
                }
                case PropType::String: {
                    std::string s = v.s;
                    s.reserve(s.size() + 64);
                    if (input_string("##v", s, ImGuiInputTextFlags_EnterReturnsTrue)) {
                        v = Value::string(s);
                        changed = true;
                    }
                    break;
                }
                case PropType::Vector3: {
                    f32 x[3] = {f32(v.n[0]), f32(v.n[1]), f32(v.n[2])};
                    if (ImGui::DragFloat3("##v", x, 0.05f, 0.0f, 0.0f, "%.2f")) {
                        v = Value::vector3({x[0], x[1], x[2]});
                        live = true;
                    }
                    break;
                }
                case PropType::Color3: {
                    f32 x[3] = {f32(v.n[0]), f32(v.n[1]), f32(v.n[2])};
                    if (ImGui::ColorEdit3("##v", x, ImGuiColorEditFlags_Float)) {
                        v = Value::color3({x[0], x[1], x[2]});
                        live = true;
                    }
                    break;
                }
                case PropType::Enum: {
                    const auto& items = p->enum_info->items;
                    const u32 cur = v.as_enum();
                    if (ImGui::BeginCombo("##v", cur < items.size() ? items[cur].c_str() : "?")) {
                        for (u32 k = 0; k < items.size(); ++k)
                            if (ImGui::Selectable(items[k].c_str(), k == cur)) {
                                v = Value::enumeration(k);
                                changed = true;
                            }
                        ImGui::EndCombo();
                    }
                    break;
                }
                case PropType::Ref: {
                    Instance* target = v.ref ? session_.model().find(v.ref) : nullptr;
                    const std::string label = target ? target->name() : std::string("(none)");
                    ImGui::Button(label.c_str(), {-1, 0});
                    if (ImGui::BeginDragDropTarget()) {
                        if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("TEFRA_INSTANCE")) {
                            u64 id = 0;
                            std::memcpy(&id, pl->Data, sizeof(id));
                            v = Value::reference(id);
                            changed = true;
                        }
                        ImGui::EndDragDropTarget();
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Drag an instance from the Explorer here. Right click to clear.");
                    if (ImGui::IsItemClicked(1)) {
                        v = Value::reference(0);
                        changed = true;
                    }
                    break;
                }
                }
                if (ro) ImGui::EndDisabled();
                // Continuous widgets: one undo step per drag gesture.
                if (ImGui::IsItemActivated() && (p->type == PropType::Float || p->type == PropType::Int ||
                                                 p->type == PropType::Vector3 || p->type == PropType::Color3)) {
                    session_.begin_edit(("Change " + p->name).c_str());
                    session_.track(inst, p->index);
                }
                if (live) session_.set_live(inst, p->index, v);
                if (changed) session_.set_property(inst, p->index, v);
                if (ImGui::IsItemDeactivated()) session_.end_edit();
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
        ImGui::EndTable();
    }
    if (inst->is_a("BaseScript") || inst->is_a("ModuleScript")) {
        ImGui::Spacing();
        if (ImGui::Button("Edit Script", {-1, 0})) open_script(inst->id());
    }
    ImGui::End();
}

// ---- Viewport ---------------------------------------------------------------------------------------------------

void App::focus_selection() {
    Instance* inst = session_.primary();
    if (!inst || !sync_) return;
    vec3 lo, hi;
    if (!sync_->bounds(*inst, lo, hi)) return;
    const vec3 c = (lo + hi) * 0.5f;
    const f32 r = max(length(hi - lo) * 0.5f, 1.0f);
    const vec3 f = camera_.forward();
    const vec3 p = c - f * (r / std::tan(camera_.fovy * 0.5f) * 1.1f);
    camera_.position = {f64(p.x), f64(p.y), f64(p.z)};
}

void App::draw_viewport(f32 dt) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    const bool visible = ImGui::Begin("Viewport");
    ImGui::PopStyleVar();
    if (!visible) {
        ImGui::End();
        return;
    }
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    viewport_wanted_ = {static_cast<u32>(max(avail.x, 16.0f)), static_cast<u32>(max(avail.y, 16.0f))};
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    viewport_x_ = origin.x;
    viewport_y_ = origin.y;
    if (viewport_tex_)
        ImGui::Image(reinterpret_cast<ImTextureID>(viewport_tex_), avail);
    else
        ImGui::Dummy(avail);
    viewport_hovered_ = ImGui::IsItemHovered();
    ImGuiIO& io = ImGui::GetIO();

    const bool playing = session_.play_state() != PlayState::Edit;
    if (playing) {
        // Play mode: third-person camera following the player; no editing gizmos or picking.
        play_input({origin.x, origin.y}, {avail.x, avail.y});
        ImDrawList* pdl = ImGui::GetWindowDrawList();
        pdl->AddRect(origin, {origin.x + avail.x, origin.y + avail.y}, IM_COL32(60, 200, 90, 255), 0.0f, 0, 3.0f);
        pdl->AddText({origin.x + 8, origin.y + avail.y - 20}, IM_COL32(230, 230, 230, 180),
                     "PLAY  |  WASD move  |  Space jump  |  RMB orbit  |  wheel zoom  |  F5 stop");
        ImGui::End();
        return;
    }
    // Fly camera: hold the right mouse button to look, WASD/QE to move, Shift = fast, wheel = dolly.
    static bool looking = false;
    if (viewport_hovered_ && ImGui::IsMouseClicked(1)) looking = true;
    if (!io.MouseDown[1]) looking = false;
    if (looking) {
        camera_.yaw -= io.MouseDelta.x * 0.0035f;
        camera_.pitch = clamp(camera_.pitch - io.MouseDelta.y * 0.0035f, radians(-89.0f), radians(89.0f));
        const vec3 f = camera_.forward(), r = camera_.right();
        vec3 m{};
        if (ImGui::IsKeyDown(ImGuiKey_W)) m = m + f;
        if (ImGui::IsKeyDown(ImGuiKey_S)) m = m - f;
        if (ImGui::IsKeyDown(ImGuiKey_D)) m = m + r;
        if (ImGui::IsKeyDown(ImGuiKey_A)) m = m - r;
        if (ImGui::IsKeyDown(ImGuiKey_E)) m = m + vec3{0, 1, 0};
        if (ImGui::IsKeyDown(ImGuiKey_Q)) m = m - vec3{0, 1, 0};
        const f32 speed = (io.KeyShift ? 60.0f : 16.0f) * dt;
        camera_.position = {camera_.position.x + f64(m.x * speed), camera_.position.y + f64(m.y * speed),
                            camera_.position.z + f64(m.z * speed)};
    }
    if (viewport_hovered_ && io.MouseWheel != 0.0f) {
        const vec3 f = camera_.forward() * (io.MouseWheel * 3.0f);
        camera_.position = {camera_.position.x + f64(f.x), camera_.position.y + f64(f.y),
                            camera_.position.z + f64(f.z)};
    }

    // Gizmo on the primary selection (parts and models).
    const f32 aspect = avail.x / max(avail.y, 1.0f);
    const vec3 cam{f32(camera_.position.x), f32(camera_.position.y), f32(camera_.position.z)};
    const vec3 fwd = camera_.forward(), right = camera_.right();
    const vec3 up = normalize(cross(right, fwd));
    f32 view[16] = {right.x, up.x, -fwd.x, 0, right.y,          up.y,          -fwd.y,        0,
                    right.z, up.z, -fwd.z, 0, -dot(right, cam), -dot(up, cam), dot(fwd, cam), 1};
    const f32 n = 0.1f, far_ = 4000.0f, t = 1.0f / std::tan(camera_.fovy * 0.5f);
    f32 proj[16] = {
        t / aspect, 0, 0, 0, 0, t, 0, 0, 0, 0, -(far_ + n) / (far_ - n), -1, 0, 0, -2.0f * far_ * n / (far_ - n), 0};
    Instance* sel = session_.primary();
    bool gizmo_using = false;
    if (sel && tool_ != Tool::Select && (sel->is_a("BasePart") || sel->is_a("Model")) && sync_) {
        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetDrawlist();
        ImGuizmo::SetRect(origin.x, origin.y, avail.x, avail.y);
        // Pivot: the part itself, or the centre of a model's bounds (axis aligned).
        f32 model[16];
        vec3 pivot_pos{};
        quat pivot_rot{};
        vec3 pivot_size{1, 1, 1};
        if (sel->is_a("BasePart")) {
            pivot_pos = sel->get_vec3("Position");
            pivot_rot = world::orientation_to_quat(sel->get_vec3("Orientation"));
            pivot_size = sel->get_vec3("Size");
        } else {
            vec3 lo, hi;
            if (sync_->bounds(*sel, lo, hi)) pivot_pos = (lo + hi) * 0.5f;
        }
        mat_from_trs(pivot_pos, pivot_rot, pivot_size, model);
        f32 before[16];
        std::memcpy(before, model, sizeof(model));
        ImGuizmo::OPERATION op = tool_ == Tool::Move     ? ImGuizmo::TRANSLATE
                                 : tool_ == Tool::Rotate ? ImGuizmo::ROTATE
                                                         : ImGuizmo::SCALE;
        f32 snap_v[3] = {move_snap_, move_snap_, move_snap_};
        if (op == ImGuizmo::ROTATE) snap_v[0] = snap_v[1] = snap_v[2] = rotate_snap_;
        const bool local = gizmo_local_ || op == ImGuizmo::SCALE;
        const bool can_scale = sel->is_a("BasePart");
        if (op != ImGuizmo::SCALE || can_scale) {
            ImGuizmo::Manipulate(view, proj, op, local ? ImGuizmo::LOCAL : ImGuizmo::WORLD, model, nullptr,
                                 snap_ ? snap_v : nullptr);
        }
        gizmo_using = ImGuizmo::IsUsing();
        if (gizmo_using && !gizmo_active_) {
            session_.begin_edit(op == ImGuizmo::TRANSLATE ? "Move" : op == ImGuizmo::ROTATE ? "Rotate" : "Scale");
            gizmo_active_ = true;
        }
        if (gizmo_using) {
            if (sel->is_a("BasePart")) {
                vec3 tpos, tscale;
                quat trot;
                mat_to_trs(model, tpos, trot, tscale);
                const auto& c = sel->cls();
                session_.set_live(sel, c.find_prop("Position")->index, Value::vector3(tpos));
                if (op == ImGuizmo::ROTATE)
                    session_.set_live(sel, c.find_prop("Orientation")->index,
                                      Value::vector3(world::quat_to_orientation(trot)));
                if (op == ImGuizmo::SCALE)
                    session_.set_live(
                        sel, c.find_prop("Size")->index,
                        Value::vector3({max(tscale.x, 0.05f), max(tscale.y, 0.05f), max(tscale.z, 0.05f)}));
            } else {
                // Model: apply the delta transform to every descendant part.
                f32 inv_before[16], delta[16];
                if (mat_inverse_affine(before, inv_before)) {
                    mat_mul(model, inv_before, delta);
                    for (Instance* d : sel->descendants()) {
                        if (!d->is_a("BasePart")) continue;
                        f32 pm[16], out[16];
                        mat_from_trs(d->get_vec3("Position"), world::orientation_to_quat(d->get_vec3("Orientation")),
                                     {1, 1, 1}, pm);
                        mat_mul(delta, pm, out);
                        vec3 tp, ts;
                        quat tr;
                        mat_to_trs(out, tp, tr, ts);
                        const auto& c = d->cls();
                        session_.set_live(d, c.find_prop("Position")->index, Value::vector3(tp));
                        session_.set_live(d, c.find_prop("Orientation")->index,
                                          Value::vector3(world::quat_to_orientation(tr)));
                    }
                }
            }
        }
    }
    if (!gizmo_using && gizmo_active_) {
        session_.end_edit();
        gizmo_active_ = false;
    }

    // Click to select (ignored while the gizmo is hovered or dragged).
    if (viewport_hovered_ && ImGui::IsMouseClicked(0) && !ImGuizmo::IsOver() && !gizmo_using && sync_) {
        const f32 mx = (io.MousePos.x - origin.x) / avail.x * 2.0f - 1.0f;
        const f32 my = 1.0f - (io.MousePos.y - origin.y) / avail.y * 2.0f;
        const f32 th = std::tan(camera_.fovy * 0.5f);
        const vec3 dir = normalize(fwd + right * (mx * th * aspect) + up * (my * th));
        world::RayHit hit = sync_->raycast(cam, dir, 5000.0f);
        if (hit.instance) {
            // Clicking a part inside a model selects the top-most model under the Workspace (Alt = the part).
            Instance* pick = hit.instance;
            if (!io.KeyAlt)
                for (Instance* p = pick->parent(); p && p != session_.model().workspace(); p = p->parent())
                    if (p->is_a("Model")) pick = p;
            session_.select(pick->id(), io.KeyCtrl);
        } else if (!io.KeyCtrl) {
            session_.clear_selection();
        }
    }

    // Overlay: help and camera info.
    ImDrawList* dl = ImGui::GetWindowDrawList();
    char info[160];
    std::snprintf(info, sizeof(info), "RMB+WASD fly  |  F focus  |  %s  |  %ux%u",
                  session_.play_state() == PlayState::Edit ? "Edit" : "PLAY", viewport_extent_.width,
                  viewport_extent_.height);
    dl->AddText({origin.x + 8, origin.y + avail.y - 20}, IM_COL32(230, 230, 230, 160), info);
    if (session_.play_state() != PlayState::Edit)
        dl->AddRect(origin, {origin.x + avail.x, origin.y + avail.y}, IM_COL32(60, 200, 90, 255), 0.0f, 0, 3.0f);
    ImGui::End();
}

// ---- Output -----------------------------------------------------------------------------------------------------

void App::draw_output() {
    if (!ImGui::Begin("Output")) {
        ImGui::End();
        return;
    }
    if (ImGui::Button("Clear")) {
        std::lock_guard<std::mutex> lock(log_mutex_);
        log_.clear();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &log_autoscroll_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(220);
    ImGui::InputTextWithHint("##filter", "Filter", log_filter_, sizeof(log_filter_));
    ImGui::Separator();
    ImGui::BeginChild("log", {0, 0}, ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    {
        std::lock_guard<std::mutex> lock(log_mutex_);
        for (const LogLine& l : log_) {
            if (log_filter_[0] && l.text.find(log_filter_) == std::string::npos) continue;
            ImVec4 c = ImGui::GetStyleColorVec4(ImGuiCol_Text);
            if (l.level == LogLevel::Warn) c = ImVec4(0.95f, 0.75f, 0.2f, 1.0f);
            if (l.level >= LogLevel::Error) c = ImVec4(1.0f, 0.35f, 0.3f, 1.0f);
            if (l.level <= LogLevel::Debug) c = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
            ImGui::TextDisabled("%8.2f", l.time);
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, c);
            ImGui::TextUnformatted(l.text.c_str());
            ImGui::PopStyleColor();
        }
    }
    if (log_autoscroll_ && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
    ImGui::End();
}

// ---- Script editor ----------------------------------------------------------------------------------------------

void App::open_script(u64 id) {
    if (std::find(open_scripts_.begin(), open_scripts_.end(), id) == open_scripts_.end()) open_scripts_.push_back(id);
    focus_script_ = id;
}

void App::draw_script_editors() {
    for (usize k = 0; k < open_scripts_.size();) {
        Instance* inst = session_.model().find(open_scripts_[k]);
        if (!inst) {
            open_scripts_.erase(open_scripts_.begin() + static_cast<long>(k));
            continue;
        }
        const std::string title = inst->name() + " (C#)###script" + std::to_string(inst->id());
        bool open = true;
        if (focus_script_ == inst->id()) {
            ImGui::SetNextWindowFocus();
            ImGui::SetNextWindowSize({720, 520}, ImGuiCond_FirstUseEver);
            focus_script_ = 0;
        }
        if (ImGui::Begin(title.c_str(), &open)) {
            const world::PropInfo* src = inst->cls().find_prop("Source");
            std::string text = inst->get(src->index).s;
            text.reserve(text.size() + 1024);
            ImGui::TextDisabled("%s  -  compiled with Roslyn when you press Play", inst->full_name().c_str());
            if (ImGui::InputTextMultiline("##code", text.data(), text.capacity() + 1, ImGui::GetContentRegionAvail(),
                                          ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_CallbackResize,
                                          text_resize_cb, &text))
                session_.set_live(inst, src->index, Value::string(text));
            if (ImGui::IsItemActivated()) {
                session_.begin_edit("Edit script");
                session_.track(inst, src->index);
            }
            if (ImGui::IsItemDeactivated()) session_.end_edit();
        }
        ImGui::End();
        if (!open) {
            open_scripts_.erase(open_scripts_.begin() + static_cast<long>(k));
            continue;
        }
        ++k;
    }
}

// ---- File dialogs (simple path prompts) ---------------------------------------------------------------------------

void App::draw_file_dialogs() {
    auto dialog = [&](const char* id, bool& show, bool save) {
        if (show) {
            ImGui::OpenPopup(id);
            if (path_buf_[0] == 0)
                std::snprintf(path_buf_, sizeof(path_buf_), "%s",
                              session_.path().empty() ? "places/MyGame.tefra" : session_.path().c_str());
            show = false;
        }
        if (ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text(save ? "Save place as (.tefra):" : "Open place (.tefra):");
            ImGui::SetNextItemWidth(460);
            const bool enter =
                ImGui::InputText("##path", path_buf_, sizeof(path_buf_), ImGuiInputTextFlags_EnterReturnsTrue);
            if (ImGui::Button(save ? "Save" : "Open", {120, 0}) || enter) {
                std::string err;
                const bool ok = save ? session_.save(path_buf_) : session_.open(path_buf_, &err);
                if (!ok) TEFRA_LOG_ERROR("%s %s failed %s", save ? "Save" : "Open", path_buf_, err.c_str());
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", {120, 0})) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    };
    dialog("Open Place", show_open_dialog_, false);
    dialog("Save Place", show_save_dialog_, true);
}

// ---- Demo scene ---------------------------------------------------------------------------------------------------

void App::build_demo_scene() {
    world::DataModel& m = session_.model();
    build_baseplate(m);
    Instance* ws = m.workspace();
    auto part = [&](Instance* parent, const char* name, vec3 pos, vec3 size, vec3 color, u32 material = 0,
                    u32 shape = 0) {
        Instance* p = m.create("Part", parent, name);
        p->set("Position", Value::vector3(pos));
        p->set("Size", Value::vector3(size));
        p->set("Color", Value::color3(color));
        p->set("Material", Value::enumeration(material));
        p->set("Shape", Value::enumeration(shape));
        p->set("Anchored", Value::boolean(true));
        return p;
    };
    // A small obby: platforms of rising height, a tower and a spinning sign.
    Instance* obby = m.create("Model", ws, "Obby");
    const vec3 colors[5] = {
        {0.95f, 0.35f, 0.3f}, {0.98f, 0.7f, 0.2f}, {0.35f, 0.8f, 0.4f}, {0.3f, 0.6f, 0.95f}, {0.7f, 0.45f, 0.95f}};
    for (int i = 0; i < 5; ++i)
        part(obby, ("Platform" + std::to_string(i + 1)).c_str(), {8.0f + f32(i) * 7.0f, 1.0f + f32(i) * 1.6f, -6.0f},
             {4, 1, 4}, colors[i], 1);
    Instance* tower = m.create("Model", ws, "Tower");
    part(tower, "Base", {-14, 4, -10}, {8, 8, 8}, {0.85f, 0.82f, 0.75f}, 7);
    part(tower, "Roof", {-14, 9.5f, -10}, {9, 3, 9}, {0.6f, 0.25f, 0.2f}, 2, 3);
    part(tower, "Door", {-14, 2, -5.9f}, {2.5f, 4, 0.3f}, {0.35f, 0.22f, 0.12f}, 2);
    part(ws, "Ball", {-4, 1.5f, 8}, {3, 3, 3}, {0.95f, 0.95f, 0.95f}, 4, 1);
    part(ws, "Pillar", {4, 3, 10}, {2, 6, 2}, {0.25f, 0.75f, 0.85f}, 18, 2);
    Instance* sign = part(ws, "SpinningSign", {0, 6, -16}, {6, 3, 0.4f}, {1.0f, 0.85f, 0.3f}, 18);
    Instance* script = m.create("Script", sign, "Spinner");
    script->set("Source", Value::string("using Tefra;\n\npublic class Spinner : TefraScript\n{\n"
                                        "    public float DegreesPerSecond = 90;\n\n"
                                        "    public override void Update(float dt)\n    {\n"
                                        "        var part = (Part)Parent;\n"
                                        "        part.Orientation += new Vector3(0, DegreesPerSecond * dt, 0);\n"
                                        "    }\n}\n"));
    m.service("Lighting")->set("ClockTime", Value::number(15.5));
    session_.history().clear();
    session_.select(sign->id());
}

} // namespace tefra::studio
