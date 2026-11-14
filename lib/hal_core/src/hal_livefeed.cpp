#include "hal_livefeed.h"

namespace hal {

void LiveFeed::start() {
  pending_ = Pending::None;
  n_ = 0;
  text_.clear();
}

FeedOut LiveFeed::playable() {
  FeedOut out;
  out.append = true;
  out.who = who_ == kZero ? Speaker::New : Speaker::Old;
  out.peer = who_;
  out.n = n_;
  out.text = text_;
  out.close = last_;
  pending_ = Pending::None;
  return out;
}

// The line in hand is lost. Its number goes back, for the next line to take.
FeedOut LiveFeed::fail(const char* reason) {
  FeedOut out;
  out.failed = true;
  out.reason = reason;
  out.peer = who_;
  out.n = n_;
  out.last = last_;
  if (n_ > 0) --n_;
  pending_ = Pending::None;
  return out;
}

FeedOut LiveFeed::onLine(int who, bool voiced, const std::string& text, bool last, const std::string& host, int port,
                         const std::string& token, uint32_t nowMs) {
  FeedOut out;
  if (busy()) return out;  // one at a time: the caller asks for the next line once this one is playable
  ++n_;
  who_ = who;
  text_ = text;
  last_ = last;
  if (!castValid(who) || n_ > kMaxDialogueLines) return fail("missing");
  if (who == kZero) {
    pending_ = Pending::LocalClip;
    return out;
  }
  if (!voiced) {  // his words reach him in a NOW: only what that can carry can be said
    Message now;
    now.type = MsgType::Now;
    now.n = n_;
    now.who = castId(who);
    now.text = text;
    if (parseMessage(formatMessage(now)).type != MsgType::Now) return fail("missing");
    return playable();
  }
  Message fetch;
  fetch.type = MsgType::Fetch;
  fetch.n = n_;
  fetch.host = host;
  fetch.port = port;
  fetch.token = token;
  // Only what the strict parser on the other side will accept is worth sending.
  if (parseMessage(formatMessage(fetch)).type != MsgType::Fetch) return fail("missing");
  pending_ = Pending::Ready;
  deadline_ = nowMs + kReadyMs;
  out.send = true;
  out.to = who;
  out.msg = fetch;
  return out;
}

FeedOut LiveFeed::onLocalClip() {
  if (pending_ != Pending::LocalClip) return FeedOut();
  return playable();
}

FeedOut LiveFeed::onRemote(const Message& m, int from) {
  if (pending_ != Pending::Ready || m.n != n_ || from != who_) return FeedOut();
  if (m.type == MsgType::Ready) return playable();
  if (m.type == MsgType::Missing) return fail("missing");
  return FeedOut();
}

FeedOut LiveFeed::onGone(int who) {
  if (pending_ != Pending::Ready || who != who_) return FeedOut();
  return fail("missing");
}

FeedOut LiveFeed::tick(uint32_t nowMs) {
  if (pending_ == Pending::Ready && static_cast<int32_t>(nowMs - deadline_) > 0) return fail("timeout");
  return FeedOut();
}

}  // namespace hal
