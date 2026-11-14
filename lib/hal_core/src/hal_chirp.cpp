#include "hal_chirp.h"

namespace hal {

std::vector<Chirp> chirpsFor(const std::string& text) {
  std::vector<Chirp> out;
  for (unsigned char c : text) {
    if (c == ' ') {
      if (!out.empty()) out.back().gapMs += 60;  // a breath between words
    } else if (c == '.' || c == ',' || c == '!' || c == ';' || c == ':') {
      out.push_back({1100, 80, 100});            // the sentence rests
    } else if (c == '?') {
      out.push_back({2400, 60, 10});             // a question rises
      out.push_back({3100, 90, 110});
    } else if (c > ' ' && c <= '~') {
      out.push_back({static_cast<uint16_t>(1500 + (c * 53) % 1700), 38, 14});
    }
  }
  return out;
}

uint32_t chirpMs(const std::string& text) {
  uint32_t total = 0;
  for (const Chirp& c : chirpsFor(text)) total += c.ms + c.gapMs;
  return total < kChirpFloorMs ? kChirpFloorMs : total;
}

}  // namespace hal
