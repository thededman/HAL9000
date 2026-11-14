#include "hal_wav.h"

#include <cmath>
#include <cstring>

namespace hal {
namespace {

uint16_t rd16(const uint8_t* p) { return p[0] | (p[1] << 8); }
uint32_t rd32(const uint8_t* p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

bool parse(const uint8_t* data, size_t len, bool eightBitToo, WavInfo* out) {
  if (!data || len < 12) return false;
  if (std::memcmp(data, "RIFF", 4) != 0 || std::memcmp(data + 8, "WAVE", 4) != 0) return false;

  WavInfo info;
  bool haveFmt = false;
  size_t pos = 12;
  while (pos + 8 <= len) {
    const uint8_t* id = data + pos;
    uint32_t size = rd32(data + pos + 4);
    size_t body = pos + 8;

    if (std::memcmp(id, "fmt ", 4) == 0) {
      if (size < 16 || body + 16 > len) return false;
      uint16_t format = rd16(data + body);
      info.channels = rd16(data + body + 2);
      info.sampleRate = rd32(data + body + 4);
      info.bitsPerSample = rd16(data + body + 14);
      const bool depthOk = info.bitsPerSample == 16 || (eightBitToo && info.bitsPerSample == 8);
      if (format != 1 || !depthOk || info.channels == 0 || info.sampleRate == 0) {
        return false;
      }
      haveFmt = true;
    } else if (std::memcmp(id, "data", 4) == 0) {
      if (!haveFmt) return false;
      size_t available = len - body;
      size_t bytes = size < available ? size : available;
      size_t frameBytes = size_t(info.channels) * (info.bitsPerSample / 8);
      bytes -= bytes % frameBytes;
      info.dataOffset = body;
      info.dataBytes = bytes;
      uint64_t frames = bytes / frameBytes;
      info.durationMs = static_cast<uint32_t>(frames * 1000 / info.sampleRate);
      *out = info;
      return true;
    }
    size_t next = body + size_t(size) + (size & 1);
    if (next <= pos) return false;  // overflow guard
    pos = next;
  }
  return false;
}

}  // namespace

bool parseWav(const uint8_t* data, size_t len, WavInfo* out) { return parse(data, len, false, out); }
bool parseClipWav(const uint8_t* data, size_t len, WavInfo* out) { return parse(data, len, true, out); }

bool playableClip(const WavInfo& info) {
  return info.channels >= 1 && info.channels <= 2 && info.sampleRate >= 8000 && info.sampleRate <= 48000 &&
         info.dataBytes > 0 && info.durationMs <= 60000;
}

std::vector<uint8_t> loudnessEnvelope(const uint8_t* data, const WavInfo& info, uint32_t frameMs) {
  std::vector<uint8_t> env;
  const bool eight = info.bitsPerSample == 8;
  size_t stride = info.channels;
  size_t samples = info.dataBytes / (stride * (eight ? 1 : 2));
  size_t perFrame = static_cast<size_t>(info.sampleRate) * frameMs / 1000;
  if (perFrame == 0 || samples == 0) return env;

  const uint8_t* pcm = data + info.dataOffset;
  for (size_t start = 0; start < samples; start += perFrame) {
    size_t end = start + perFrame < samples ? start + perFrame : samples;
    double sum = 0;
    for (size_t i = start; i < end; ++i) {
      // Unsigned 8-bit rests at 128; brought up to the 16-bit scale it is measured on.
      int s = eight ? (int(pcm[i * stride]) - 128) * 258 : static_cast<int16_t>(rd16(pcm + i * stride * 2));
      sum += double(s) * s;
    }
    double rms = std::sqrt(sum / double(end - start));
    double v = rms * 255.0 / 32767.0;
    env.push_back(static_cast<uint8_t>(v > 255.0 ? 255 : std::lround(v)));
  }
  return env;
}

}  // namespace hal
