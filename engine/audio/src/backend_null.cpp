#include "backend.h"

#include <cmath>

namespace tefra::audio {

namespace {

// A voice with a simulated clock: it advances by dt x PlaybackSpeed each frame, so TimePosition, Ended and Looped
// behave like a device playing in real time. Used when no output device exists (CI, servers, tests).
class NullVoice final : public Voice {
  public:
    explicit NullVoice(f64 length) : length_(length) {}

    void play(bool on) override {
        if (on && finished_) {
            pos_ = 0.0;
            finished_ = false;
        }
        playing_ = on;
    }
    bool playing() const override { return playing_; }
    void seek(f64 seconds) override {
        pos_ = clamp(seconds, 0.0, length_);
        finished_ = false;
    }
    f64 position() const override { return pos_; }
    bool finished() const override { return finished_; }
    void set_looped(bool looped) override { looped_ = looped; }
    void set_gain(f32 gain) override { gain_ = gain; }
    void set_pitch(f32 speed) override { pitch_ = speed; }
    void set_position(vec3 p) override { (void)p; }

    void advance(f32 dt) override {
        if (!playing_) return;
        if (length_ <= 0.0) {
            if (!looped_) {
                finished_ = true;
                playing_ = false;
            }
            return;
        }
        pos_ += f64(dt) * f64(pitch_);
        if (looped_) {
            pos_ = std::fmod(pos_, length_);
        } else if (pos_ >= length_) {
            pos_ = length_;
            finished_ = true;
            playing_ = false;
        }
    }

    f32 gain() const { return gain_; }

  private:
    f64 length_ = 0.0;
    f64 pos_ = 0.0;
    f32 pitch_ = 1.0f;
    f32 gain_ = 1.0f;
    bool playing_ = false;
    bool looped_ = false;
    bool finished_ = false;
};

class NullBackend final : public Backend {
  public:
    const char* name() const override { return "null"; }
    std::unique_ptr<Voice> create_voice(const std::shared_ptr<const ClipData>& clip, bool spatial) override {
        (void)spatial;
        return std::make_unique<NullVoice>(clip ? clip->seconds() : 0.0);
    }
    void set_listener(vec3 position, vec3 forward) override { (void)position, (void)forward; }
};

} // namespace

std::unique_ptr<Backend> make_null_backend() { return std::make_unique<NullBackend>(); }

} // namespace tefra::audio
