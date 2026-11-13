#pragma once
// Feeds a live conversation (the AI brain's lines) to the conductor, one line at a time
// and in order. HAL Zero's own line is playable once its audio has been downloaded; the
// desk HAL's once he has fetched it himself and said READY.
//
//   brain line -> onLine()  -- New --> (audio arrives) onLocalClip() -> append
//                           -- Old --> send FETCH n ...  -> READY n   -> append
//                                                        -> MISSING n / 8 s of silence -> failed
#include <cstdint>
#include <string>

#include "hal_dialogue.h"
#include "hal_link.h"

namespace hal {

struct FeedOut {
  bool send = false;     // send `msg` to the desk HAL (a FETCH)
  Message msg;
  bool append = false;   // the line is playable: Conductor::append(who, text)
  Speaker who = Speaker::New;
  std::string text;
  bool close = false;    // that was the last line: Conductor::close()
  bool failed = false;   // the conversation can't go on
  std::string reason;    // "missing" or "timeout"
};

class LiveFeed {
 public:
  static constexpr uint32_t kReadyMs = 8000;

  void start();                      // a new conversation: line numbers restart at 1
  bool busy() const { return pending_ != Pending::None; }  // a line is on its way to being playable
  int pendingLine() const { return busy() ? n_ : 0; }

  // The next line from the brain (ignored while one is still pending). `host`, `port` and
  // `token` say where the desk HAL can fetch his audio; they are checked as the link would.
  FeedOut onLine(Speaker who, const std::string& text, bool last, const std::string& host, int port,
                 const std::string& token, uint32_t nowMs);
  FeedOut onLocalClip();             // HAL Zero's audio for the pending line is here
  FeedOut onRemote(const Message& m);  // READY n -> append; MISSING n -> failed
  FeedOut tick(uint32_t nowMs);      // no READY within 8 s -> failed "timeout"

 private:
  enum class Pending { None, LocalClip, Ready };
  FeedOut playable();
  FeedOut fail(const char* reason);

  Pending pending_ = Pending::None;
  int n_ = 0;  // lines taken so far; the pending one is line n_
  Speaker who_ = Speaker::New;
  std::string text_;
  bool last_ = false;
  uint32_t deadline_ = 0;
};

}  // namespace hal
