#include "tefra/audio/audio_system.h"

namespace tefra::audio {

namespace {
// Placeholder until the miniaudio backend lands: sounds are silent.
class NullAudio final : public runtime::RuntimeSystem {
  public:
    const char* name() const override { return "audio"; }
};
} // namespace

std::unique_ptr<runtime::RuntimeSystem> make_audio_system() { return std::make_unique<NullAudio>(); }

} // namespace tefra::audio
