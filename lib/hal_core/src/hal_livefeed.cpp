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
  out.who = who_;
  out.text = text_;
  out.close = last_;
  pending_ = Pending::None;
  return out;
}

FeedOut LiveFeed::fail(const char* reason) {
  FeedOut out;
  out.failed = true;
  out.reason = reason;
  pending_ = Pending::None;
  return out;
}

FeedOut LiveFeed::onLine(Speaker who, const std::string& text, bool last, const std::string& host, int port,
                         const std::string& token, uint32_t nowMs) {
  FeedOut out;
  if (busy()) return out;  // one at a time: the caller asks for the next line once this one is playable
  if (n_ >= kMaxDialogueLines) return fail("missing");
  ++n_;
  who_ = who;
  text_ = text;
  last_ = last;
  if (who == Speaker::New) {
    pending_ = Pending::LocalClip;
    return out;
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
  out.msg = fetch;
  return out;
}

FeedOut LiveFeed::onLocalClip() {
  if (pending_ != Pending::LocalClip) return FeedOut();
  return playable();
}

FeedOut LiveFeed::onRemote(const Message& m) {
  if (pending_ != Pending::Ready || m.n != n_) return FeedOut();
  if (m.type == MsgType::Ready) return playable();
  if (m.type == MsgType::Missing) return fail("missing");
  return FeedOut();
}

FeedOut LiveFeed::tick(uint32_t nowMs) {
  if (pending_ == Pending::Ready && static_cast<int32_t>(nowMs - deadline_) > 0) return fail("timeout");
  return FeedOut();
}

}  // namespace hal
