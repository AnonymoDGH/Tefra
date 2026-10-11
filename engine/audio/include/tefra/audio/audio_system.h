#pragma once

// Game audio (spec M3b §6): plays the Sound instances of a running game. A Sound parented to a BasePart (or to a
// Model with a PrimaryPart) is 3D: attenuated between RollOffMinDistance and RollOffMaxDistance (scaled by
// SoundService.RolloffScale) and panned from the camera (the listener); anywhere else it is 2D. Playing / Looped /
// Volume / PlaybackSpeed / TimePosition follow the properties live; scripts call Play()/Stop() by setting Playing.
// Fires "Ended" (and "Looped") GameEvents. SoundService.Volume is the master volume. PlayOnRemove plays a one-shot
// copy when the Sound is destroyed. The local character gets jump, landing, footstep and death sounds while
// SoundService.CharacterSounds is true.
//
// SoundId: a path relative to the project folder, an absolute path, or "builtin:<name>" for the sound effects in
// resources/sfx (jump, land, footstep_grass, footstep_stone, checkpoint, win, death, click, coin, wind_loop).
// rbxassetid:// ids cannot be resolved: they are reported once and stay silent.
//
// Without an audio device (CI, headless, no sound card) a null backend runs the same logic: TimePosition advances
// in real time and Ended / Looped fire at the same moments, so game logic does not change.

#include "tefra/core/types.h"
#include "tefra/runtime/game_runtime.h"

#include <memory>

namespace tefra::audio {

struct AudioOptions {
    bool use_device = true; // false: always the null backend (tests, servers)
};

class SoundDriver;
class Backend;
class CharacterSounds;

// The runtime system. Created by make_audio_system(); public so tests can inspect it.
class AudioSystem final : public runtime::RuntimeSystem {
  public:
    explicit AudioSystem(AudioOptions options = {});
    ~AudioSystem() override;
    AudioSystem(const AudioSystem&) = delete;
    AudioSystem& operator=(const AudioSystem&) = delete;

    const char* name() const override { return "audio"; }
    bool start(runtime::GameRuntime& rt) override;
    void post_physics(runtime::GameRuntime& rt, f32 dt) override;
    void stop(runtime::GameRuntime& rt) override;

    // Introspection (valid while running; zero otherwise).
    const char* backend_name() const; // "miniaudio" or "null"
    u32 active_voices() const;        // sounds and one-shots with a voice
    u32 loaded_clips() const;         // decoded files in the cache

  private:
    AudioOptions options_;
    std::unique_ptr<Backend> backend_;
    std::unique_ptr<SoundDriver> driver_;
    std::unique_ptr<CharacterSounds> character_;
};

std::unique_ptr<runtime::RuntimeSystem> make_audio_system(AudioOptions options = {}); // name() == "audio"

} // namespace tefra::audio
