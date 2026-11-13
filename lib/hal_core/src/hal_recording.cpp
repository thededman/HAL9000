#include "hal_recording.h"

#include <cmath>
#include <cstdlib>

namespace hal {
namespace {

constexpr size_t kFrame = Recording::kRate / 50;        // 20 ms
constexpr size_t kMargin = Recording::kRate / 5;        // 0.2 s kept either side of the speech

void put32(std::vector<uint8_t>& out, uint32_t v) {
  for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(v >> (8 * i)));
}
void put16(std::vector<uint8_t>& out, uint16_t v) {
  out.push_back(static_cast<uint8_t>(v));
  out.push_back(static_cast<uint8_t>(v >> 8));
}

}  // namespace

void Recording::append(const int16_t* samples, size_t n) {
  if (samples_.size() >= kMaxSamples) return;
  if (n > kMaxSamples - samples_.size()) n = kMaxSamples - samples_.size();
  samples_.insert(samples_.end(), samples, samples + n);
}

uint8_t Recording::level() const {
  const size_t window = kRate / 10;
  if (samples_.empty()) return 0;
  const size_t from = samples_.size() > window ? samples_.size() - window : 0;
  double sum = 0;
  for (size_t i = from; i < samples_.size(); ++i) sum += double(samples_[i]) * samples_[i];
  const double rms = std::sqrt(sum / double(samples_.size() - from));
  // Decibel-like, so quiet speech shows on the meter and a silent room reads zero.
  const double scaled = (std::log10(1.0 + rms) - 1.0) / (std::log10(8001.0) - 1.0);
  return scaled <= 0 ? 0 : scaled >= 1 ? 255 : static_cast<uint8_t>(scaled * 255);
}

std::vector<uint8_t> Recording::wav() const {
  std::vector<uint8_t> out;
  int peak = 0;
  for (int16_t s : samples_) peak = std::max(peak, std::abs(int(s)));
  if (peak < kQuietPeak) return out;  // nothing was said

  // The speech: from the first 20 ms frame with something a quarter as loud as the loudest
  // sample, to the last, with a little room either side.
  const int threshold = peak / 4;
  size_t first = samples_.size(), last = 0;
  for (size_t i = 0; i < samples_.size(); ++i) {
    if (std::abs(int(samples_[i])) < threshold) continue;
    if (first == samples_.size()) first = i;
    last = i;
  }
  first = first / kFrame * kFrame;
  size_t begin = first > kMargin ? first - kMargin : 0;
  size_t end = last + kMargin < samples_.size() ? last + kMargin : samples_.size();
  if ((last - first) * 1000 / kRate < static_cast<size_t>(kMinMs)) return out;  // a click or a brushed key

  // Quiet speech up to half of full scale, by 40 times at most; loud speech as it is.
  const double gain = peak >= kTargetPeak ? 1.0 : std::min(double(kMaxGain), double(kTargetPeak) / peak);
  const size_t n = end - begin;
  out.reserve(44 + n * 2);
  out.insert(out.end(), {'R', 'I', 'F', 'F'});
  put32(out, static_cast<uint32_t>(36 + n * 2));
  out.insert(out.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
  put32(out, 16);
  put16(out, 1);  // PCM
  put16(out, 1);  // mono
  put32(out, kRate);
  put32(out, kRate * 2);
  put16(out, 2);
  put16(out, 16);
  out.insert(out.end(), {'d', 'a', 't', 'a'});
  put32(out, static_cast<uint32_t>(n * 2));
  for (size_t i = begin; i < end; ++i) {
    double v = samples_[i] * gain;
    int s = static_cast<int>(v < -32767 ? -32767 : v > 32767 ? 32767 : v);
    put16(out, static_cast<uint16_t>(static_cast<int16_t>(s)));
  }
  return out;
}

}  // namespace hal
