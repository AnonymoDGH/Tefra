#pragma once

// Internal: the sound backend (miniaudio or the null backend) and the decoded-clip cache. Nothing outside
// engine/audio includes this header.

#include "tefra/core/math.h"
#include "tefra/core/types.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace tefra::audio {

// A decoded file: interleaved f32 samples in the file's channel layout (mono for 3D sounds).
struct ClipData {
    std::vector<f32> samples;
    u32 channels = 1;
    u32 sample_rate = 44100;
    f64 seconds() const {
        return (channels != 0 && sample_rate != 0) ? f64(samples.size() / channels) / f64(sample_rate) : 0.0;
    }
};

// Decodes a whole file (WAV, FLAC, MP3, Ogg Vorbis) from memory. mono = downmix to one channel (3D sounds need it).
bool decode_clip(const std::string& path, bool mono, ClipData& out, std::string& error);

// One playing instance of a clip. Owned and driven by the game thread only.
class Voice {
  public:
    virtual ~Voice() = default;
    virtual void play(bool on) = 0; // on = start or resume; off = pause (keeps the position)
    virtual bool playing() const = 0;
    virtual void seek(f64 seconds) = 0;
    virtual f64 position() const = 0;  // seconds from the start of the clip
    virtual bool finished() const = 0; // reached the end of a non-looped clip
    virtual void set_looped(bool looped) = 0;
    virtual void set_gain(f32 gain) = 0;   // linear: volume x master x attenuation
    virtual void set_pitch(f32 speed) = 0; // PlaybackSpeed (pitch and speed together)
    virtual void set_position(vec3 p) = 0; // only meaningful for voices created spatial
    // True when seek() takes effect later, on the audio thread (a device); the cursor then lags for a moment.
    virtual bool seek_is_async() const { return false; }
    // Once per frame from the driver: the null backend advances its clock here, a device backend ignores it.
    virtual void advance(f32 dt) { (void)dt; }
};

class Backend {
  public:
    virtual ~Backend() = default;
    virtual const char* name() const = 0;
    virtual std::unique_ptr<Voice> create_voice(const std::shared_ptr<const ClipData>& clip, bool spatial) = 0;
    virtual void set_listener(vec3 position, vec3 forward) = 0;
    // Tests only: renders `frames` at the backend's own rate through an offline engine (no device). No-op otherwise.
    virtual void render_for_test(u32 frames) { (void)frames; }
};

// The device backend when an output device opens, else the null backend. use_device = false forces the null one.
std::unique_ptr<Backend> open_backend(bool use_device);
std::unique_ptr<Backend> make_null_backend();
// The miniaudio engine without a device, rendered by hand (render_for_test). Null when miniaudio fails to start.
std::unique_ptr<Backend> make_offline_engine_backend();

// Decoded clips by path and mono flag. Failed loads are cached as nullptr so they are not retried every frame.
class ClipCache {
  public:
    std::shared_ptr<const ClipData> get(const std::string& path, bool mono, std::string& error);
    void clear() { clips_.clear(); }
    usize size() const { return clips_.size(); }

  private:
    std::unordered_map<std::string, std::shared_ptr<const ClipData>> clips_;
};

} // namespace tefra::audio
