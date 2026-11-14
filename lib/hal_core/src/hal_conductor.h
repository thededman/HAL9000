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
//
// Several HALs (v0.7): a conversation has a set of HALs present (cast numbers, hal_cast.h),
// HAL Zero always among them, and each line names its speaker. A cue goes to that HAL only
// and only his answers count. A HAL who is busy, silent or gone leaves the conversation: it
// carries on while two or more remain (his lines are passed over) and otherwise ends exactly
// as a two-HAL conversation always has. END from any of them ends it for all.
#include <cstdint>
#include <string>
#include <vector>

#include "hal_dialogue.h"
#include "hal_link.h"

namespace hal {

struct ConductorOut {
  bool playLocal = false;  // play HAL Zero's line `localLine`
  int localLine = 0;
  bool send = false;       // send `msg` to the HAL `to`
  Message msg;
  int to = kDesk;          // ... a cast number, or kEveryone (an END for all of them)
  int started = 0;         // line `started` has just begun (the glue tells every friend: NOW)
  int left = -1;           // this HAL is out of the conversation. With `finished` and no `send`,
                           // the glue passes the END on to everyone but him (he knows)
  bool finished = false;   // the conversation is over
  std::string reason;      // done, interrupted, timeout, busy, offline
  bool gloat = false;      // the others let him down: HAL Zero gets a smug line
};

class Conductor {
 public:
  static constexpr uint32_t kGapMs = 400;
  static constexpr uint32_t kAckMs = 2000;
  static constexpr uint32_t kDoneGraceMs = 3000;

  static constexpr uint32_t kWaitLineMs = 30000;

  // `present`: who is in the conversation, one bit per HAL (HAL Zero always is).
  void start(const Dialogue& d, uint32_t nowMs, unsigned present = kPair);
  void startLive(uint32_t nowMs, unsigned present = kPair);  // id "live", no lines yet
  // The new line's number; 0 if not live, closed, over, full, or (the second form) for a HAL who
  // isn't in the conversation.
  int append(Speaker who, const std::string& text);
  int append(int who, const std::string& text);      // a cast number
  void close();                                      // no more lines: ends "done" after the last
  bool live() const { return live_ && state_ != State::Off; }
  ConductorOut tick(uint32_t nowMs);
  // `from`: which HAL said it, or hung up. One who isn't in the conversation changes nothing.
  ConductorOut onRemote(const Message& m, uint32_t nowMs, int from = kDesk);
  ConductorOut onLocalDone(uint32_t nowMs);
  ConductorOut onDisconnected(uint32_t nowMs, int from = kDesk);
  ConductorOut leave(int who, const char* reason, uint32_t nowMs);  // he can't go on, for a reason of the glue's
  ConductorOut interrupt(uint32_t nowMs);  // Dave tapped or held HAL Zero
  unsigned present() const { return present_; }

  bool active() const { return state_ != State::Off; }
  int line() const { return idx_ + 1; }  // 1-based line being spoken (or next)
  const Dialogue& dialogue() const { return d_; }

 private:
  enum class State { Off, Gap, WaitAck, WaitDone, PlayingLocal, WaitLine };
  void advance(uint32_t nowMs, ConductorOut* out);
  void finish(const char* reason, bool gloat, bool tellRemote, ConductorOut* out);
  void drop(int who, const char* reason, bool tellHim, uint32_t nowMs, ConductorOut* out);
  static bool reached(uint32_t now, uint32_t at) { return static_cast<int32_t>(now - at) >= 0; }
  static bool past(uint32_t now, uint32_t at) { return static_cast<int32_t>(now - at) > 0; }

  Dialogue d_;
  State state_ = State::Off;
  int idx_ = 0;
  uint32_t at_ = 0;  // next line due (Gap, WaitLine) or deadline (WaitAck / WaitDone)
  bool live_ = false, closed_ = false;
  unsigned present_ = kPair;
  uint32_t giveUpAt_ = 0;  // WaitLine: when to stop waiting for the next line
};

}  // namespace hal
