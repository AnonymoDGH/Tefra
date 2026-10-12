// Spatial query geometry (see spatial.h). Shape ids follow the Part.Shape enum of world/src/classes.cpp.
#include "tefra/physics/spatial.h"

#include <cmath>

namespace tefra::physics {
namespace {

constexpr u32 kBall = 1, kCylinder = 2, kWedge = 3;
constexpr f32 kPi = 3.14159265358979f;
constexpr f32 kMinSize = 0.02f; // same floor as the collision shapes (physics_world.cpp)
constexpr f32 kDefaultDensity = 700.0f;

quat conjugate(quat q) { return {-q.x, -q.y, -q.z, q.w}; }

u32 shape_of(const world::Instance& part) {
    const world::Value* v = part.get("Shape");
    return v ? v->as_enum() : 0;
}

vec3 abs3(vec3 v) { return {std::fabs(v.x), std::fabs(v.y), std::fabs(v.z)}; }

} // namespace

Obb part_obb(const world::Instance& part) {
    Obb box;
    box.center = part.get_vec3("Position");
    box.rotation = world::orientation_to_quat(part.get_vec3("Orientation"));
    box.half = abs3(part.get_vec3("Size", {4.0f, 1.0f, 2.0f})) * 0.5f;
    box.ball = shape_of(part) == kBall;
    return box;
}

bool sphere_overlaps(const Obb& box, vec3 center, f32 radius) {
    if (box.ball) {
        const f32 inscribed = min(box.half.x, min(box.half.y, box.half.z));
        return length(center - box.center) <= radius + inscribed;
    }
    // Closest point of the box to the sphere centre, computed in the box's own axes.
    const vec3 local = rotate(conjugate(box.rotation), center - box.center);
    const vec3 closest{clamp(local.x, -box.half.x, box.half.x), clamp(local.y, -box.half.y, box.half.y),
                       clamp(local.z, -box.half.z, box.half.z)};
    return length(local - closest) <= radius;
}

bool obb_overlaps(const Obb& a, const Obb& b) {
    vec3 ax[3], bx[3];
    for (int i = 0; i < 3; ++i) {
        vec3 unit{0, 0, 0};
        unit[i] = 1.0f;
        ax[i] = rotate(a.rotation, unit);
        bx[i] = rotate(b.rotation, unit);
    }
    const vec3 t = b.center - a.center;
    // True when the boxes are separated along the (not necessarily unit) axis `axis`.
    auto separated = [&](vec3 axis) {
        const f32 len2 = dot(axis, axis);
        if (len2 < 1e-12f) return false; // parallel edges give a degenerate axis: nothing to separate on it
        const vec3 l = axis * (1.0f / std::sqrt(len2));
        f32 ra = 0.0f, rb = 0.0f;
        for (int i = 0; i < 3; ++i) {
            ra += a.half[i] * std::fabs(dot(ax[i], l));
            rb += b.half[i] * std::fabs(dot(bx[i], l));
        }
        return std::fabs(dot(t, l)) > ra + rb;
    };
    for (int i = 0; i < 3; ++i)
        if (separated(ax[i]) || separated(bx[i])) return false;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            if (separated(cross(ax[i], bx[j]))) return false;
    return true;
}

f32 part_volume(u32 shape, vec3 size) {
    const f32 x = max(std::fabs(size.x), kMinSize);
    const f32 y = max(std::fabs(size.y), kMinSize);
    const f32 z = max(std::fabs(size.z), kMinSize);
    switch (shape) {
    case kBall: return 4.0f / 3.0f * kPi * (x * 0.5f) * (y * 0.5f) * (z * 0.5f);
    case kCylinder: return kPi * (x * 0.5f) * (z * 0.5f) * y;
    case kWedge: return 0.5f * x * y * z;
    default: return x * y * z;
    }
}

f32 part_mass(const world::Instance& part) {
    const f32 density = max(part.get_float("Density", kDefaultDensity), 1.0f);
    return density * part_volume(shape_of(part), part.get_vec3("Size", {4.0f, 1.0f, 2.0f}));
}

void find_parts(const world::DataModel& model, const world::OverlapRequest& query, std::vector<world::Instance*>& out) {
    out.clear();
    const world::Instance* ws = model.workspace();
    if (!ws) return;
    if (query.kind == world::OverlapKind::Part && !query.part) return;

    Obb box;
    if (query.kind == world::OverlapKind::Box) {
        box.center = query.center;
        box.rotation = world::orientation_to_quat(query.orientation);
        box.half = abs3(query.size) * 0.5f;
    } else if (query.kind == world::OverlapKind::Part) {
        box = part_obb(*query.part);
        box.ball = false; // part against part: boxes
    }

    for (world::Instance* p : ws->descendants()) {
        if (!p->is_a("BasePart")) continue;
        if (query.kind == world::OverlapKind::Part && p == query.part) continue;
        if (query.filter && !query.filter->accepts(p)) continue;
        const Obb other = part_obb(*p);
        const bool hit = query.kind == world::OverlapKind::Radius ? sphere_overlaps(other, query.center, query.radius)
                                                                  : obb_overlaps(other, box);
        if (!hit) continue;
        out.push_back(p);
        if (query.max_parts != 0 && out.size() >= query.max_parts) break;
    }
}

} // namespace tefra::physics
