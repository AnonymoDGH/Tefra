#pragma once

// Game audio (spec M3b §6): plays the Sound instances of a running game. A Sound parented to a BasePart (or an
// Attachment-less Model with a PrimaryPart) is 3D: attenuated between RollOffMinDistance and RollOffMaxDistance and
// panned from the camera (the listener); anywhere else it is 2D. Playing / Looped / Volume / PlaybackSpeed /
// TimePosition follow the properties live; scripts call Play()/Stop() by setting Playing. Fires "Ended" (and
// "Looped") GameEvents. SoundService.Volume is the master volume. Without an audio device (CI, servers) the system
// still runs and advances TimePosition so games behave the same.

#include "tefra/runtime/game_runtime.h"

#include <memory>

namespace tefra::audio {

std::unique_ptr<runtime::RuntimeSystem> make_audio_system(); // name() == "audio"

} // namespace tefra::audio
