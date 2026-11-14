#pragma once
// When the two HALs next strike up a conversation of their own. HAL Zero keeps this: a chat
// comes due a random while after the last conversation of any kind ended, and then waits for
// a moment when both of them are free.
#include <cstdint>

#include "hal_clock.h"

namespace hal {

class ChatTimer {
 public:
  static constexpr uint32_t kRetryMs = 60000;  // not a good moment: look again in a minute

  void configure(bool on, int minMinutes, int maxMinutes) {
    on_ = on;
    min_ = minMinutes;
    max_ = maxMinutes;
  }
  bool on() const { return on_; }

  // Counts from now to the next chat. Call at start-up and whenever a conversation ends (theirs
  // or one Dave asked for), so one never follows straight on another. `random`: any number.
  void restart(uint32_t nowMs, uint32_t random) {
    atMs_ = nowMs + nextIdleDelayMs(random, min_, max_);
    armed_ = on_;
  }

  // Call every pass. `free`: both HALs could talk right now. True once, when a chat should
  // start; nothing more comes due until restart().
  bool due(uint32_t nowMs, bool free) {
    if (!on_ || !armed_ || static_cast<int32_t>(nowMs - atMs_) < 0) return false;
    if (!free) {
      atMs_ = nowMs + kRetryMs;
      return false;
    }
    armed_ = false;
    return true;
  }

 private:
  bool on_ = false, armed_ = false;
  int min_ = 20, max_ = 40;
  uint32_t atMs_ = 0;
};

}  // namespace hal
