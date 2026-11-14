#pragma once
// HAL Nano's "voice": his device has only a buzzer, so he chirps his line while the words show
// on his screen. One short tone a letter, its pitch taken from the letter; a pause at a space;
// a low note where a sentence rests and two rising ones at a question. The same words always
// chirp the same way.
#include <cstdint>
#include <string>
#include <vector>

namespace hal {

struct Chirp {
  uint16_t hz;     // the tone
  uint16_t ms;     // how long it sounds
  uint16_t gapMs;  // silence after it
};

constexpr uint32_t kChirpFloorMs = 2500;  // a line is never over sooner (the next one must be ready)

std::vector<Chirp> chirpsFor(const std::string& text);
// How long the line takes: what the chirps add up to, and kChirpFloorMs at least.
uint32_t chirpMs(const std::string& text);

}  // namespace hal
