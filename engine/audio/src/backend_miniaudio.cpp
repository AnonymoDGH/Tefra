#include "backend.h"

#include "tefra/core/log.h"

#include "miniaudio.h"

namespace tefra::audio {

namespace {

// One ma_sound playing a decoded clip from memory (the ClipData outlives the voice: it is held here).
class MaVoice final : public Voice {
  public:
    MaVoice(ma_engine* engine, std::shared_ptr<const ClipData> clip, bool spatial) : clip_(std::move(clip)) {
        const auto frames = static_cast<ma_uint64>(clip_->samples.size() / clip_->channels);
        ma_audio_buffer_config bc =
            ma_audio_buffer_config_init(ma_format_f32, clip_->channels, frames, clip_->samples.data(), nullptr);
        // Without this the buffer reports rate 0 and miniaudio plays the clip at the engine rate: a 22050 Hz file
        // ran 2.2x fast and TimeLength was infinite. The decoder kept the file's own rate, so it is passed on here.
        bc.sampleRate = clip_->sample_rate;
        if (ma_audio_buffer_init(&bc, &buffer_) != MA_SUCCESS) return;
        buffer_ready_ = true;
        // 2D sounds skip the spatializer. 3D sounds keep it for panning; the distance gain is ours (sound_math.cpp),
        // so miniaudio's own attenuation is switched off and the Doppler effect too.
        const ma_uint32 flags = spatial ? ma_uint32{0} : static_cast<ma_uint32>(MA_SOUND_FLAG_NO_SPATIALIZATION);
        if (ma_sound_init_from_data_source(engine, &buffer_, flags, nullptr, &sound_) != MA_SUCCESS) return;
        sound_ready_ = true;
        if (spatial) {
            ma_sound_set_attenuation_model(&sound_, ma_attenuation_model_none);
            ma_sound_set_doppler_factor(&sound_, 0.0f);
        }
    }
    ~MaVoice() override {
        if (sound_ready_) ma_sound_uninit(&sound_);
        if (buffer_ready_) ma_audio_buffer_uninit(&buffer_);
    }
    MaVoice(const MaVoice&) = delete;
    MaVoice& operator=(const MaVoice&) = delete;

    void play(bool on) override {
        if (!sound_ready_) return;
        if (on) {
            ma_sound_start(&sound_);
        } else {
            ma_sound_stop(&sound_);
        }
    }
    bool playing() const override { return sound_ready_ && ma_sound_is_playing(&sound_) != 0; }
    void seek(f64 seconds) override {
        if (sound_ready_) ma_sound_seek_to_second(&sound_, static_cast<f32>(seconds));
    }
    f64 position() const override {
        if (!sound_ready_) return 0.0;
        float seconds = 0.0f;
        (void)ma_sound_get_cursor_in_seconds(&sound_, &seconds);
        return f64(seconds);
    }
    bool finished() const override { return sound_ready_ && ma_sound_at_end(&sound_) != 0; }
    bool seek_is_async() const override { return true; } // ma_sound_seek_to_second sets a target the mixer applies
    void set_looped(bool looped) override {
        if (sound_ready_) ma_sound_set_looping(&sound_, looped ? MA_TRUE : MA_FALSE);
    }
    void set_gain(f32 gain) override {
        if (sound_ready_) ma_sound_set_volume(&sound_, gain);
    }
    void set_pitch(f32 speed) override {
        if (sound_ready_) ma_sound_set_pitch(&sound_, speed);
    }
    void set_position(vec3 p) override {
        if (sound_ready_) ma_sound_set_position(&sound_, p.x, p.y, p.z);
    }

  private:
    std::shared_ptr<const ClipData> clip_;
    ma_audio_buffer buffer_{};
    ma_sound sound_{};
    bool buffer_ready_ = false;
    bool sound_ready_ = false;
};

class MaBackend final : public Backend {
  public:
    ~MaBackend() override {
        if (ready_) ma_engine_uninit(&engine_);
    }
    // False when no real output device opened (miniaudio falls back to its null device in that case).
    bool init() {
        ma_engine_config cfg = ma_engine_config_init();
        if (ma_engine_init(&cfg, &engine_) != MA_SUCCESS) return false;
        const ma_device* device = ma_engine_get_device(&engine_);
        if (!device || !device->pContext || device->pContext->backend == ma_backend_null) {
            ma_engine_uninit(&engine_);
            return false;
        }
        ready_ = true;
        return true;
    }
    // Offline engine for tests: no device, rendered by render_for_test().
    bool init_offline() {
        ma_engine_config cfg = ma_engine_config_init();
        cfg.noDevice = MA_TRUE;
        cfg.channels = 2;
        cfg.sampleRate = kOfflineRate;
        if (ma_engine_init(&cfg, &engine_) != MA_SUCCESS) return false;
        ready_ = true;
        offline_ = true;
        return true;
    }

    const char* name() const override { return offline_ ? "miniaudio-offline" : "miniaudio"; }
    void render_for_test(u32 frames) override {
        if (!offline_) return;
        float scratch[512 * 2];
        while (frames > 0) {
            const auto n = static_cast<ma_uint32>(frames < 512u ? frames : 512u);
            ma_uint64 got = 0;
            (void)ma_engine_read_pcm_frames(&engine_, scratch, n, &got);
            if (got == 0) break;
            frames -= static_cast<u32>(got);
        }
    }
    std::unique_ptr<Voice> create_voice(const std::shared_ptr<const ClipData>& clip, bool spatial) override {
        if (!ready_ || !clip || clip->channels == 0 || clip->samples.empty()) return nullptr;
        return std::make_unique<MaVoice>(&engine_, clip, spatial);
    }
    void set_listener(vec3 position, vec3 forward) override {
        if (!ready_) return;
        ma_engine_listener_set_position(&engine_, 0, position.x, position.y, position.z);
        ma_engine_listener_set_direction(&engine_, 0, forward.x, forward.y, forward.z);
    }

  private:
    static constexpr ma_uint32 kOfflineRate = 48000;
    ma_engine engine_{};
    bool ready_ = false;
    bool offline_ = false;
};

} // namespace

std::unique_ptr<Backend> open_backend(bool use_device) {
    if (use_device) {
        auto backend = std::make_unique<MaBackend>();
        if (backend->init()) return backend;
        TEFRA_LOG_INFO("audio: no output device; sounds run on the null backend (timing and events are unchanged)");
    }
    return make_null_backend();
}

std::unique_ptr<Backend> make_offline_engine_backend() {
    auto backend = std::make_unique<MaBackend>();
    if (!backend->init_offline()) return nullptr;
    return backend;
}

} // namespace tefra::audio
