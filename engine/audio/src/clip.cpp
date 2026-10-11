#include "backend.h"

#include "tefra/core/fs.h"

#include "miniaudio.h"

#include <cstddef>
#include <memory>

namespace tefra::audio {

namespace {
constexpr ma_uint64 kChunkFrames = 4096;
constexpr f64 kMaxSeconds = 600.0; // longer files are refused: whole clips are kept in memory
} // namespace

bool decode_clip(const std::string& path, bool mono, ClipData& out, std::string& error) {
    std::vector<u8> bytes;
    if (!file_read_all(path.c_str(), bytes)) {
        error = "file not readable";
        return false;
    }
    if (bytes.empty()) {
        error = "empty file";
        return false;
    }
    // Decoding from memory: miniaudio's path handling is ANSI on Windows, our file reader is UTF-8.
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, mono ? 1u : 0u, 0u);
    ma_decoder decoder;
    if (ma_decoder_init_memory(bytes.data(), bytes.size(), &cfg, &decoder) != MA_SUCCESS) {
        error = "unsupported format (WAV, FLAC, MP3 and Ogg Vorbis are supported)";
        return false;
    }
    const ma_uint32 channels = decoder.outputChannels;
    const ma_uint32 rate = decoder.outputSampleRate;
    if (channels == 0 || rate == 0) {
        ma_decoder_uninit(&decoder);
        error = "unknown channel layout";
        return false;
    }
    const ma_uint64 max_frames = static_cast<ma_uint64>(kMaxSeconds * static_cast<f64>(rate));
    std::vector<f32> chunk(static_cast<usize>(kChunkFrames) * channels);
    std::vector<f32> samples;
    ma_uint64 total = 0;
    while (total < max_frames) {
        ma_uint64 got = 0;
        (void)ma_decoder_read_pcm_frames(&decoder, chunk.data(), kChunkFrames, &got);
        if (got == 0) break;
        const auto count = static_cast<std::ptrdiff_t>(got * channels);
        samples.insert(samples.end(), chunk.begin(), chunk.begin() + count);
        total += got;
    }
    ma_decoder_uninit(&decoder);
    if (samples.empty()) {
        error = "no audio data";
        return false;
    }
    out.samples = std::move(samples);
    out.channels = channels;
    out.sample_rate = rate;
    return true;
}

std::shared_ptr<const ClipData> ClipCache::get(const std::string& path, bool mono, std::string& error) {
    const std::string key = path + (mono ? "|mono" : "|native");
    if (auto it = clips_.find(key); it != clips_.end()) {
        if (!it->second) error = "previously failed to load";
        return it->second;
    }
    ClipData data;
    std::shared_ptr<const ClipData> result;
    if (decode_clip(path, mono, data, error)) result = std::make_shared<ClipData>(std::move(data));
    clips_.emplace(key, result);
    return result;
}

} // namespace tefra::audio
