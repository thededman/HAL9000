#pragma once
// The talk key: held by itself for a moment, HAL Zero listens; let go, he sends what he heard.
// (On the Cardputer it is Fn, which is also half of every Fn combination: the other key of a
// combination follows within a few milliseconds, so nothing starts until Fn has been down alone
// for a moment. A hold is used once: when its recording ends some other way, nothing more
// happens until the key has been let go.)
#include <cstdint>

namespace hal {

class TalkKey {
 public:
  static constexpr uint32_t kAloneMs = 150;
  enum class Event { None, Start, Send, Drop };

  // Call every pass. `down`: the key is held, by itself. `combined`: another key was just pressed
  // with it, so this was an ordinary key combination.
  Event update(bool down, bool combined, uint32_t nowMs) {
    switch (state_) {
      case State::Up:
        if (down && !combined) {
          state_ = State::Waiting;
          sinceMs_ = nowMs;
        }
        break;
      case State::Waiting:
        if (combined) {
          state_ = State::Spent;
        } else if (!down) {
          state_ = State::Up;  // a brushed key
        } else if (nowMs - sinceMs_ >= kAloneMs) {
          state_ = State::Recording;
          return Event::Start;
        }
        break;
      case State::Recording:
        if (combined) {
          state_ = State::Spent;
          return Event::Drop;
        }
        if (!down) {
          state_ = State::Up;
          return Event::Send;
        }
        break;
      case State::Spent:
        if (!down && !combined) state_ = State::Up;
        break;
    }
    return Event::None;
  }

  // The recording ended without the key (its time ran out, it couldn't start, it was cancelled).
  void finish() {
    if (state_ == State::Recording || state_ == State::Waiting) state_ = State::Spent;
  }
  bool recording() const { return state_ == State::Recording; }

 private:
  enum class State { Up, Waiting, Recording, Spent } state_ = State::Up;
  uint32_t sinceMs_ = 0;
};

}  // namespace hal
