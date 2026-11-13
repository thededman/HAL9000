#pragma once
// Reads 16-bit PCM WAV files held in memory and measures their loudness
// so the eye can glow in time with HAL's voice.
#include <cstddef>
#include <cstdint>
#include <vector>

namespace hal {

struct WavInfo {
  uint32_t sampleRate = 0;
  uint16_t channels = 0;
  uint16_t bitsPerSample = 0;
  size_t dataOffset = 0;  // byte offset of the first sample
  size_t dataBytes = 0;   // clamped to what is actually in the buffer
  uint32_t durationMs = 0;
};

// Walks the RIFF chunks (skipping LIST and friends, honouring pad bytes).
// Accepts PCM (format 1), 16-bit only. Never reads past `len`.
bool parseWav(const uint8_t* data, size_t len, WavInfo* out);

// A downloaded line the HALs will play: mono or stereo at an ordinary rate, with something in it,
// and no longer than a minute. (A megabyte that claims 50 Hz would otherwise "speak" for hours.)
bool playableClip(const WavInfo& info);

// RMS of the first channel per `frameMs` frame, scaled so a full-scale square
// wave is 255. Length = ceil(samples / samplesPerFrame).
std::vector<uint8_t> loudnessEnvelope(const uint8_t* data, const WavInfo& info, uint32_t frameMs);

}  // namespace hal
