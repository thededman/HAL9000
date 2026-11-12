#pragma once
// HAL Zero's side of a scripted conversation: who speaks next, waiting for the
// desk HAL, gaps between lines, and what happens when the desk HAL lets him down.
//
// Lines by NEW play here (playLocal); lines by OLD are sent as "SAY <id> <n>"
// and must be acknowledged with PLAYING within 2 s, then DONE within the line's
// length + 3 s. 400 ms of silence between lines unless the script says PAUSE.
//
// A live conversation (the AI brain) has no script: startLive(), then append() each
// line once its audio is ready, then close(). Between lines the conductor waits, up
// to 30 s, for the next one. A chat answer is a live conversation of one line.
#include <cstdint>
#include <string>
#include <vector>

#include "hal_dialogue.h"
#include "hal_link.h"

namespace hal {

struct ConductorOut {
  bool playLocal = false;  // play HAL Zero's line `localLine`
  int localLine = 0;
  bool send = false;       // send `msg` to the desk HAL
  Message msg;
  bool finished = false;   // the conversation is over
  std::string reason;      // done, interrupted, timeout, busy, offline
  bool gloat = false;      // the desk HAL let him down: HAL Zero gets a smug line
};

class Conductor {
 public:
  static constexpr uint32_t kGapMs = 400;
  static constexpr uint32_t kAckMs = 2000;
  static constexpr uint32_t kDoneGraceMs = 3000;

  static constexpr uint32_t kWaitLineMs = 30000;

  void start(const Dialogue& d, uint32_t nowMs);
  void startLive(uint32_t nowMs);                    // id "live", no lines yet
  int append(Speaker who, const std::string& text);  // the new line's number; 0 if not live, closed, over or full
  void close();                                      // no more lines: ends "done" after the last
  bool live() const { return live_ && state_ != State::Off; }
  ConductorOut tick(uint32_t nowMs);
  ConductorOut onRemote(const Message& m, uint32_t nowMs);
  ConductorOut onLocalDone(uint32_t nowMs);
  ConductorOut onDisconnected(uint32_t nowMs);
  ConductorOut interrupt(uint32_t nowMs);  // Dave tapped or held HAL Zero

  bool active() const { return state_ != State::Off; }
  int line() const { return idx_ + 1; }  // 1-based line being spoken (or next)
  const Dialogue& dialogue() const { return d_; }

 private:
  enum class State { Off, Gap, WaitAck, WaitDone, PlayingLocal, WaitLine };
  void advance(uint32_t nowMs, ConductorOut* out);
  void finish(const char* reason, bool gloat, bool tellRemote, ConductorOut* out);
  static bool reached(uint32_t now, uint32_t at) { return static_cast<int32_t>(now - at) >= 0; }
  static bool past(uint32_t now, uint32_t at) { return static_cast<int32_t>(now - at) > 0; }

  Dialogue d_;
  State state_ = State::Off;
  int idx_ = 0;
  uint32_t at_ = 0;  // next line due (Gap, WaitLine) or deadline (WaitAck / WaitDone)
  bool live_ = false, closed_ = false;
  uint32_t giveUpAt_ = 0;  // WaitLine: when to stop waiting for the next line
};

}  // namespace hal
