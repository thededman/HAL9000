#pragma once
// "Hey HAL": speaking to HAL Zero without holding a key (off unless wake=on, or switched on
// in his menu). His microphone stays open; when someone speaks, what they said is sent to the
// speech recogniser on the computer, and if it begins with "Hey HAL" the rest is a question
// for him. Said by itself, "Hey HAL" makes him listen for the question that follows.
//
// Here are the two judgements that need no microphone: when someone started and stopped
// speaking (from how loud each moment was), and whether what was heard was said to him.
#include <cstddef>
#include <cstdint>
#include <string>

namespace hal {

struct WakeHeard {
  bool forHim = false;   // it began with "Hey HAL" (as the recogniser spells it, on a good day or a bad one)
  std::string question;  // what followed, ready for the ask box: printable, 120 characters at most. "" = only his name
};
WakeHeard wakeMatch(const std::string& heard);

// How loud a stretch of 16-bit samples is, 0..255. A quiet room is 1 to 4, speech at arm's
// length 30 and up.
uint8_t wakeLevel(const int16_t* samples, size_t n);

// Fed the loudness of each 20 ms of sound, says when someone began to speak and when they
// had finished. It gets used to steady noise (a fan), ignores clicks and coughs, and gives
// up on sound that never pauses (a television) until there has been quiet again.
class WakeListener {
 public:
  static constexpr uint32_t kFrameMs = 20;
  static constexpr uint32_t kStartMs = 60;    // this long loud without a break: someone is speaking
  static constexpr uint32_t kEndMs = 700;     // this long quiet: they have finished
  static constexpr uint32_t kMinMs = 350;     // less speech than this was a click or a cough
  static constexpr uint32_t kMaxMs = 9000;    // more than this without a pause is not a question for him

  enum class Event {
    None,
    Started,  // someone began to speak (kStartMs ago)
    Ended,    // ... and has finished: the last heardMs() of sound is what they said
    Dropped,  // it wasn't speech to him after all (too short, or it never paused)
  };
  Event feed(uint8_t level);  // the next kFrameMs of sound
  void reset();               // stop listening to whatever is being said (a HAL began to speak)
  bool speaking() const { return state_ == State::Speaking; }
  uint32_t heardMs() const { return heardFrames_ * kFrameMs; }  // after Ended: from the first loud moment to now
  int roomLevel() const { return static_cast<int>(floor_ + 0.5f); }  // how loud the room is with nobody speaking

 private:
  enum class State { Idle, Speaking, Deaf };
  uint8_t threshold() const;

  State state_ = State::Idle;
  float floor_ = 2.0f;        // how loud the room is with nobody speaking
  uint32_t loudRun_ = 0;      // loud frames in a row (Idle)
  uint32_t heardFrames_ = 0;  // frames since the first loud one (Speaking)
  uint32_t quietRun_ = 0;     // quiet frames in a row (Speaking, Deaf)
};

}  // namespace hal
