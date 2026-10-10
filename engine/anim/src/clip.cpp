#include "tefra/anim/clip.h"

#include "tefra/core/fs.h"

#include <algorithm>
#include <cmath>

namespace tefra::anim {

namespace {

constexpr const char* kFormat = "tefra-anim/1";

f32 ease(Easing e, f32 u) {
    switch (e) {
    case Easing::Linear: return u;
    case Easing::Smooth: return u * u * (3.0f - 2.0f * u);
    case Easing::Cubic: return u < 0.5f ? 4.0f * u * u * u : 1.0f - std::pow(-2.0f * u + 2.0f, 3.0f) * 0.5f;
    case Easing::Step: return 0.0f;
    }
    return u;
}

const char* easing_name(Easing e) {
    switch (e) {
    case Easing::Linear: return "linear";
    case Easing::Smooth: return "smooth";
    case Easing::Cubic: return "cubic";
    case Easing::Step: return "step";
    }
    return "smooth";
}

bool parse_easing(const std::string& s, Easing& out) {
    if (s == "linear")
        out = Easing::Linear;
    else if (s == "smooth")
        out = Easing::Smooth;
    else if (s == "cubic")
        out = Easing::Cubic;
    else if (s == "step")
        out = Easing::Step;
    else
        return false;
    return true;
}

bool set_error(std::string* error, const char* msg) {
    if (error) *error = msg;
    return false;
}

// Reads n numbers from a JSON array; false if the array is missing or too short.
bool read_numbers(const JsonValue& arr, f64* out, usize n) {
    if (!arr.is_array() || arr.size() < n) return false;
    for (usize i = 0; i < n; ++i) {
        if (!arr.at(i).is_number()) return false;
        out[i] = arr.at(i).as_number();
    }
    return true;
}

} // namespace

const JointTrack* AnimationClip::track(std::string_view joint) const {
    for (const JointTrack& t : tracks)
        if (t.joint == joint) return &t;
    return nullptr;
}

CFrame AnimationClip::sample(const JointTrack& t, f32 time) const {
    const std::vector<Keyframe>& k = t.keys;
    if (k.empty()) return {};
    if (k.size() == 1 || duration <= 0.0f) return {k[0].pos, k[0].rot};

    f32 tt = looped ? std::fmod(time, duration) : clamp(time, 0.0f, duration);
    if (tt < 0.0f) tt += duration;

    // Segment [k[i].time, k[i+1].time) that contains tt.
    usize i = 0;
    while (i + 1 < k.size() && k[i + 1].time <= tt) ++i;
    if (i + 1 >= k.size()) return {k.back().pos, k.back().rot};

    const Keyframe& a = k[i];
    const Keyframe& b = k[i + 1];
    const f32 span = b.time - a.time;
    f32 u = span > 0.0f ? (tt - a.time) / span : 1.0f;
    u = ease(a.easing, clamp(u, 0.0f, 1.0f));
    return {lerp(a.pos, b.pos, u), slerp(a.rot, b.rot, u)};
}

bool AnimationClip::sample_joint(std::string_view joint, f32 time, CFrame& out) const {
    const JointTrack* t = track(joint);
    if (!t) return false;
    out = sample(*t, time);
    return true;
}

void AnimationClip::finalize() {
    f32 d = 0.0f;
    for (const JointTrack& t : tracks)
        for (const Keyframe& k : t.keys) d = std::max(d, k.time);
    duration = d;
}

JsonValue AnimationClip::to_json() const {
    JsonValue root = JsonValue::object();
    root.set("format", kFormat);
    root.set("name", name);
    root.set("duration", duration);
    root.set("looped", looped);
    root.set("priority", priority);
    JsonValue tracks_json = JsonValue::array();
    for (const JointTrack& t : tracks) {
        JsonValue tj = JsonValue::object();
        tj.set("joint", t.joint);
        JsonValue keys = JsonValue::array();
        for (const Keyframe& k : t.keys) {
            JsonValue kj = JsonValue::object();
            kj.set("t", k.time);
            JsonValue rot = JsonValue::array();
            for (f64 v : {f64(k.rot.x), f64(k.rot.y), f64(k.rot.z), f64(k.rot.w)}) rot.push(v);
            kj.set("rot", rot);
            JsonValue pos = JsonValue::array();
            for (f64 v : {f64(k.pos.x), f64(k.pos.y), f64(k.pos.z)}) pos.push(v);
            kj.set("pos", pos);
            kj.set("ease", easing_name(k.easing));
            keys.push(kj);
        }
        tj.set("keys", keys);
        tracks_json.push(tj);
    }
    root.set("tracks", tracks_json);
    return root;
}

bool AnimationClip::from_json(const JsonValue& j, AnimationClip& out, std::string* error) {
    if (!j.is_object()) return set_error(error, "clip: root must be an object");
    if (j["format"].as_string() != kFormat) return set_error(error, "clip: unsupported format");
    AnimationClip c;
    c.name = j["name"].as_string();
    c.looped = j["looped"].as_bool(true);
    c.priority = static_cast<i32>(j["priority"].as_int(0));
    const JsonValue& tracks_json = j["tracks"];
    if (!tracks_json.is_array()) return set_error(error, "clip: 'tracks' must be an array");
    for (const JsonValue& tj : tracks_json.items()) {
        JointTrack t;
        t.joint = tj["joint"].as_string();
        if (t.joint.empty()) return set_error(error, "clip: track without 'joint'");
        if (!tj["keys"].is_array()) return set_error(error, "clip: track 'keys' must be an array");
        for (const JsonValue& kj : tj["keys"].items()) {
            Keyframe k;
            if (!kj["t"].is_number()) return set_error(error, "clip: key without 't'");
            k.time = static_cast<f32>(kj["t"].as_number());
            f64 r[4], p[3];
            if (!read_numbers(kj["rot"], r, 4)) return set_error(error, "clip: key 'rot' needs 4 numbers");
            k.rot = normalize(quat{f32(r[0]), f32(r[1]), f32(r[2]), f32(r[3])});
            if (kj.contains("pos")) {
                if (!read_numbers(kj["pos"], p, 3)) return set_error(error, "clip: key 'pos' needs 3 numbers");
                k.pos = {f32(p[0]), f32(p[1]), f32(p[2])};
            }
            if (!parse_easing(kj["ease"].as_string(), k.easing)) return set_error(error, "clip: unknown easing");
            if (!t.keys.empty() && k.time < t.keys.back().time)
                return set_error(error, "clip: keys must be sorted by time");
            t.keys.push_back(k);
        }
        c.tracks.push_back(std::move(t));
    }
    c.finalize();
    out = std::move(c);
    return true;
}

bool AnimationClip::save(const char* path, std::string* error) const {
    const std::string text = to_json().dump(true) + "\n";
    if (!file_write_all(path, text.data(), text.size())) return set_error(error, "clip: cannot write file");
    return true;
}

bool AnimationClip::load(const char* path, AnimationClip& out, std::string* error) {
    std::vector<u8> bytes;
    if (!file_read_all(path, bytes)) return set_error(error, "clip: cannot read file");
    JsonValue j;
    std::string parse_error;
    const std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    if (!JsonValue::parse(text, j, &parse_error)) {
        if (error) *error = parse_error;
        return false;
    }
    return from_json(j, out, error);
}

} // namespace tefra::anim
