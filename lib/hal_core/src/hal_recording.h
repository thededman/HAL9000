#pragma once
// What the microphone heard while the talk key was held: 16 kHz mono samples, how
// loud they are (for a meter), and the WAV to send to the AI brain's /hear, with the
// silence trimmed from both ends and quiet speech brought up to a usable loudness.
// (The Cardputer Zero's microphone is quiet: speech from across a room peaks at
// under 1 % of full scale.)
#include <cstddef>
#include <cstdint>
#include <vector>

namespace hal {

class Recording {
 public:
  static constexpr int kRate = 16000;
  static constexpr size_t kMaxSamples = 15 * kRate;  // 15 s: plenty for a question
  static constexpr int kQuietPeak = 150;      // nothing louder than this: nothing was said
  static constexpr int kTargetPeak = 16384;   // quiet speech is scaled up to half of full scale...
  static constexpr int kMaxGain = 40;         // ... but never by more than this
  static constexpr int kMinMs = 300;          // shorter than this (after trimming): a brushed key

  void start() { samples_.clear(); }
  void append(const int16_t* samples, size_t n);  // ignored past 15 s
  bool full() const { return samples_.size() >= kMaxSamples; }
  uint32_t ms() const { return static_cast<uint32_t>(samples_.size() * 1000 / kRate); }
  uint8_t level() const;  // 0..255: how loud the last 100 ms were
  // The WAV to send (16 kHz mono 16-bit); empty if nothing was heard.
  std::vector<uint8_t> wav() const;

 private:
  std::vector<int16_t> samples_;
};

}  // namespace hal
