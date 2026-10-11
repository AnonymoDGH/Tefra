#include "sound_driver.h"

#include "tefra/core/log.h"

#include <cmath>
#include <unordered_set>

namespace tefra::audio {

using world::DataModel;
using world::Instance;
using world::Value;

namespace {
constexpr usize kMaxOneShots = 48;
constexpr f64 kSeekEpsilon = 1e-3;   // a TimePosition this far from the voice was written by the game: seek
constexpr f64 kWriteEpsilon = 1e-6;  // write TimePosition back only when it moved
constexpr f64 kLoopEpsilon = 1e-4;   // jitter of the device cursor that is not a loop
constexpr f32 kSettleSeconds = 0.1f; // a device applies a seek within one audio callback; this covers a few of them
constexpr f32 kSettleDone = 1e-4f;   // rounding of the countdown does not keep the write-back off
} // namespace

SoundDriver::SoundDriver(Backend& backend, SoundPaths paths) : backend_(backend), paths_(std::move(paths)) {}

SoundDriver::~SoundDriver() { stop_all(); }

u32 SoundDriver::active_voices() const {
    u32 n = static_cast<u32>(oneshots_.size());
    for (const auto& [id, t] : tracked_)
        if (t.voice) ++n;
    return n;
}

void SoundDriver::stop_all() {
    oneshots_.clear();
    tracked_.clear();
    clips_.clear();
}

void SoundDriver::warn_once(const std::string& key, const std::string& message) {
    if (warned_.insert(key).second) TEFRA_LOG_WARN("%s", message.c_str());
}

bool SoundDriver::anchor_position(const DataModel& dm, const Instance& s, vec3& out) const {
    const Instance* parent = s.parent();
    if (!parent) return false;
    if (parent->is_a("BasePart")) {
        out = parent->get_vec3("Position");
        return true;
    }
    if (parent->is_a("Model")) {
        const Value* pp = parent->get("PrimaryPart");
        if (pp && pp->ref != 0) {
            const Instance* part = dm.find(pp->ref);
            if (part && part->is_a("BasePart")) {
                out = part->get_vec3("Position");
                return true;
            }
        }
    }
    return false;
}

f32 SoundDriver::distance_gain(vec3 where, f32 min_distance, f32 max_distance) const {
    return attenuation_gain(length(where - listener_.position), min_distance, max_distance, rolloff_);
}

void SoundDriver::load(Instance& s, Tracked& t, bool spatial) {
    t.loaded = true;
    t.sound_id = s.get_string("SoundId");
    t.spatial = spatial;
    t.voice.reset();
    t.clip.reset();
    t.pos = 0.0;
    t.started = false;
    t.settle = 0.0f;

    bool unsupported = false;
    const std::string path = resolve_sound_id(t.sound_id, paths_.project_resolve, paths_.base_dirs, &unsupported);
    if (unsupported) {
        warn_once("rbxassetid:" + t.sound_id,
                  "audio: '" + t.sound_id + "' is a Roblox asset id and cannot be played; use a file path");
    } else if (path.empty()) {
        if (!t.sound_id.empty()) warn_once("missing:" + t.sound_id, "audio: sound not found: '" + t.sound_id + "'");
    } else {
        std::string error;
        t.clip = clips_.get(path, spatial, error);
        if (!t.clip) warn_once("load:" + path, "audio: cannot load '" + path + "': " + error);
    }
    if (t.clip) t.voice = backend_.create_voice(t.clip, spatial);
    s.set("TimeLength", Value::number(t.clip ? t.clip->seconds() : 0.0), true);
}

void SoundDriver::sync_sound(DataModel& dm, Instance& s, Tracked& t, f32 dt) {
    vec3 where{};
    const bool spatial = anchor_position(dm, s, where);
    if (!t.loaded || t.sound_id != s.get_string("SoundId") || t.spatial != spatial) load(s, t, spatial);

    // Snapshot for PlayOnRemove (read again when the Sound is gone).
    t.play_on_remove = s.get_bool("PlayOnRemove");
    t.volume = s.get_float("Volume", 0.5f);
    t.pitch = clamp(s.get_float("PlaybackSpeed", 1.0f), 0.05f, 8.0f);
    t.min_distance = s.get_float("RollOffMinDistance", 3.0f);
    t.max_distance = s.get_float("RollOffMaxDistance", 120.0f);
    t.where = where;

    const bool want = s.get_bool("Playing");
    if (!t.voice) {
        // Nothing to play (failed load or no SoundId): a Playing sound "ends" at once, so scripts waiting for Ended
        // are not stuck.
        if (want) {
            s.set("Playing", Value::boolean(false), true);
            dm.fire_event(s, "Ended");
        }
        return;
    }

    Voice& v = *t.voice;
    const bool looped = s.get_bool("Looped");
    f32 gain = t.volume * master_;
    v.set_looped(looped);
    v.set_pitch(t.pitch);
    if (spatial) {
        gain *= distance_gain(where, t.min_distance, t.max_distance);
        v.set_position(where);
    }
    v.set_gain(gain);

    // A TimePosition the game wrote since the last frame (a seek) is applied before anything else.
    const f64 wanted = s.get_float("TimePosition");
    if (std::abs(wanted - t.pos) > kSeekEpsilon) {
        v.seek(wanted);
        t.pos = wanted;
        t.settle = v.seek_is_async() ? kSettleSeconds : 0.0f;
    }

    if (!want) {
        if (v.playing()) v.play(false);
        t.started = false;
    } else {
        // A device stops a clip that reached its end between two frames, and miniaudio rewinds a clip that is at its
        // end when it is started again. So a non-looped clip is finished here, before the start below can restart it.
        if (t.started && !looped && v.finished()) {
            finish(dm, s, v, t);
            return;
        }
        if (!v.playing()) {
            v.play(true);
            t.started = true;
        }
    }
    v.advance(dt);

    const f64 now = v.position();
    // A looped clip wrapped around: the position jumped back since the last frame.
    const bool wrapped = want && looped && v.playing() && now + kLoopEpsilon < t.pos;
    if (want && !looped && t.started && v.finished()) {
        finish(dm, s, v, t);
        return;
    }

    // A device applies a seek in its audio thread, so for a moment the cursor still shows the old position. Keep the
    // value the game wrote until the seek has been applied.
    if (t.settle > 0.0f) t.settle -= dt;
    if (t.settle <= kSettleDone && std::abs(now - t.pos) > kWriteEpsilon) {
        s.set("TimePosition", Value::number(now), true);
        t.pos = now;
    }
    if (wrapped) dm.fire_event(s, "Looped"); // after the writes, for the same reason
}

void SoundDriver::finish(DataModel& dm, Instance& s, Voice& v, Tracked& t) {
    v.play(false);
    v.seek(0.0);
    t.pos = 0.0;
    t.started = false;
    t.settle = 0.0f;
    s.set("Playing", Value::boolean(false), true);
    s.set("TimePosition", Value::number(0.0), true);
    // Last thing done for this Sound: a listener may destroy it, so the caller must not use `s` after this.
    dm.fire_event(s, "Ended");
}

bool SoundDriver::start_one_shot(std::shared_ptr<const ClipData> clip, f32 gain, f32 pitch, const vec3* where) {
    if (oneshots_.size() >= kMaxOneShots || !clip) return false;
    auto voice = backend_.create_voice(clip, where != nullptr);
    if (!voice) return false;
    voice->set_looped(false);
    voice->set_pitch(pitch);
    voice->set_gain(gain);
    if (where) voice->set_position(*where);
    voice->play(true);
    oneshots_.push_back(OneShot{std::move(voice), std::move(clip)});
    return true;
}

bool SoundDriver::play_one_shot(const std::string& path, f32 volume, f32 pitch, const vec3* position) {
    if (path.empty()) return false;
    std::string error;
    auto clip = clips_.get(path, position != nullptr, error);
    if (!clip) {
        warn_once("load:" + path, "audio: cannot load '" + path + "': " + error);
        return false;
    }
    const f32 gain = volume * master_ * (position ? distance_gain(*position, 3.0f, 120.0f) : 1.0f);
    return start_one_shot(std::move(clip), gain, clamp(pitch, 0.05f, 8.0f), position);
}

void SoundDriver::fire_removed(Tracked& t) {
    if (!t.play_on_remove || !t.clip) return;
    const f32 gain = t.volume * master_ * (t.spatial ? distance_gain(t.where, t.min_distance, t.max_distance) : 1.0f);
    start_one_shot(t.clip, gain, t.pitch, t.spatial ? &t.where : nullptr);
}

void SoundDriver::update(DataModel& dm, const Listener& listener, f32 dt) {
    listener_ = listener;
    backend_.set_listener(listener.position, listener.forward);
    const Instance* service = dm.service("SoundService");
    master_ = service ? service->get_float("Volume", 1.0f) : 1.0f;
    rolloff_ = service ? service->get_float("RolloffScale", 1.0f) : 1.0f;

    // Ids, not pointers: a script that handles Ended or Looped may destroy other Sounds, and a snapshot of pointers
    // would then dangle. Each Sound is looked up again right before it is synced.
    std::vector<u64> ids;
    for (Instance* s : dm.root().descendants())
        if (s->is_a("Sound")) ids.push_back(s->id());
    std::unordered_set<u64> alive;
    for (u64 id : ids) {
        Instance* s = dm.find(id);
        if (!s || !s->is_a("Sound")) continue;
        alive.insert(id);
        sync_sound(dm, *s, tracked_[id], dt);
    }
    for (auto it = tracked_.begin(); it != tracked_.end();) {
        if (!alive.count(it->first)) {
            fire_removed(it->second);
            it = tracked_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = oneshots_.begin(); it != oneshots_.end();) {
        it->voice->advance(dt);
        if (it->voice->finished() || !it->voice->playing()) {
            it = oneshots_.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace tefra::audio
