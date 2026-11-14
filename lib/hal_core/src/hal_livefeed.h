#pragma once
// Feeds a live conversation (the AI brain's lines) to the conductor, one line at a time
// and in order. HAL Zero's own line is playable once its audio has been downloaded; another
// voiced HAL's once he has fetched it himself and said READY; a HAL with no voice (he chirps,
// or writes) has nothing to fetch, so his is playable at once.
//
//   brain line -> onLine()  -- HAL Zero --> (audio arrives) onLocalClip() -> append
//                           -- voiced ----> send FETCH n ... -> READY n   -> append
//                                                -> MISSING n / 8 s of silence / he left -> failed
//                           -- words -----> append
//
// A line that fails gives its number back: the conductor numbers lines as they are appended,
// and the next line must be fetched under the number it will be cued by.
#include <cstdint>
#include <string>

#include "hal_dialogue.h"
#include "hal_link.h"

namespace hal {

struct FeedOut {
  bool send = false;     // send `msg` (a FETCH) to the HAL `to`
  Message msg;
  int to = kDesk;
  bool append = false;   // the line is playable: Conductor::append(peer, text), which must return `n`
  Speaker who = Speaker::New;  // (the pair's old way of saying `peer`: HAL Zero, or not)
  int peer = kZero;      // who says the line, as a cast number (with append and with failed)
  int n = 0;             // the line's number (with append), or the number given back (with failed)
  std::string text;
  bool close = false;    // that was the last line: Conductor::close()
  bool failed = false;   // this line is lost (between two HALs: the conversation can't go on)
  bool last = false;     // ... and it was the last one
  std::string reason;    // "missing" or "timeout"
};

class LiveFeed {
 public:
  static constexpr uint32_t kReadyMs = 8000;

  void start();                      // a new conversation: line numbers restart at 1
  bool busy() const { return pending_ != Pending::None; }  // a line is on its way to being playable
  int pendingLine() const { return busy() ? n_ : 0; }

  // The next line from the brain (ignored while one is still pending). `who` is a cast number;
  // `voiced` says whether he speaks a clip. `host`, `port` and `token` say where a voiced HAL
  // can fetch his audio; they are checked as the link would check them.
  FeedOut onLine(int who, bool voiced, const std::string& text, bool last, const std::string& host, int port,
                 const std::string& token, uint32_t nowMs);
  // The pair's form: HAL Zero's line (New) or the desk HAL's (Old), both voiced.
  FeedOut onLine(Speaker who, const std::string& text, bool last, const std::string& host, int port,
                 const std::string& token, uint32_t nowMs) {
    return onLine(who == Speaker::New ? kZero : kDesk, true, text, last, host, port, token, nowMs);
  }
  FeedOut onLocalClip();             // HAL Zero's audio for the pending line is here
  // READY n -> append; MISSING n -> failed. Only from the HAL whose line is pending.
  FeedOut onRemote(const Message& m, int from = kDesk);
  FeedOut onGone(int who);           // he left the conversation: a line of his in flight is lost
  FeedOut tick(uint32_t nowMs);      // no READY within 8 s -> failed "timeout"

 private:
  enum class Pending { None, LocalClip, Ready };
  FeedOut playable();
  FeedOut fail(const char* reason);

  Pending pending_ = Pending::None;
  int n_ = 0;  // lines taken so far; the pending one is line n_
  int who_ = kZero;
  std::string text_;
  bool last_ = false;
  uint32_t deadline_ = 0;
};

}  // namespace hal
