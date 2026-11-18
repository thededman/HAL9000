#include "card_fetch.h"

#include <Arduino.h>

#include "hal_http.h"
#include "hal_link.h"
#include "hal_log.h"
#include "hal_wav.h"

bool CardFetch::begin(const std::string& host, int port, const std::string& token, uint8_t* buf, size_t capacity) {
  abort();
  const auto refuse = [this](const char* why) {
    halLog("clip: %s", why);
    state_ = State::Failed;
    return false;
  };
  if (!buf || capacity < 44) return refuse("no memory was kept for a clip");
  // The brain is on this network. Anything else a link message names is not ours to fetch from.
  if (!hal::sameSubnet(host, WiFi.localIP().toString().c_str(), WiFi.subnetMask().toString().c_str()))
    return refuse("that address isn't on this network");
  const std::string request = hal::httpRequest("GET", host, port, "/clip/" + token, "", "", "");
  if (request.empty()) return refuse("bad request");
  // Connecting is the one step that holds the loop up. If nobody answered a moment ago, don't
  // wait again so soon: a peer can't freeze him by naming dead addresses in a row.
  if (refusedMs_ && millis() - refusedMs_ < kRetryMs) return refuse("the brain didn't answer a moment ago");
  host_ = host;
  port_ = port;
  request_ = request;
  buf_ = buf;
  capacity_ = capacity;
  progressMs_ = startedMs_ = nextTryMs_ = millis();
  state_ = State::Fetching;  // poll() connects: at once, and once more a little later if that fails
  return true;
}

CardFetch::State CardFetch::fail(const char* why) {
  halLog("clip: %s", why);
  client_.stop();
  connected_ = inBody_ = false;
  len_ = got_ = 0;
  head_.clear();
  state_ = State::Failed;
  return state_;
}

CardFetch::State CardFetch::poll() {
  if (state_ != State::Fetching) return state_;
  if (!connected_) {
    if (static_cast<int32_t>(millis() - nextTryMs_) < 0) return state_;  // waiting to try again
    ++tries_;
    if (client_.connect(host_.c_str(), port_, kConnectMs)) {
      connected_ = true;
      refusedMs_ = 0;
      client_.setNoDelay(true);
      client_.print(request_.c_str());
      progressMs_ = millis();
    } else {
      client_.stop();
      if (tries_ >= kTries) {
        refusedMs_ = millis() ? millis() : 1;
        return fail("can't reach the brain");
      }
      halLog("clip: the brain didn't answer; one more try in a moment");
      nextTryMs_ = millis() + kTryGapMs;
    }
    return state_;
  }
  const uint32_t started = millis();
  while (client_.available() > 0 && millis() - started < kSliceMs) {
    progressMs_ = millis();
    if (!inBody_) {  // still in the head: a byte at a time, up to its blank line
      head_.push_back(static_cast<char>(client_.read()));
      if (head_.size() > 1024) return fail("the reply's head is too long");
      if (head_.size() < 4 || head_.compare(head_.size() - 4, 4, "\r\n\r\n") != 0) continue;
      int status = 0;
      long length = -1;
      if (!hal::parseHttpHead(head_, &status, &length)) return fail("not an HTTP reply");
      if (status != 200) return fail("the brain doesn't have that clip");
      if (length < 44) return fail("the clip is missing its size");
      if (static_cast<size_t>(length) > capacity_) return fail("the clip is bigger than his memory");
      len_ = static_cast<size_t>(length);
      got_ = 0;
      inBody_ = true;
      head_.clear();
      continue;
    }
    const int n = client_.read(buf_ + got_, len_ - got_);
    if (n > 0) got_ += static_cast<size_t>(n);
    if (got_ == len_) break;
  }
  if (inBody_ && got_ == len_) {  // all here: it has to be a clip he can play
    client_.stop();
    connected_ = false;
    hal::WavInfo info;
    if (!hal::parseClipWav(buf_, len_, &info) || !hal::playableClip(info) || info.bitsPerSample != 8 || info.channels != 1)
      return fail("that isn't a small 8-bit clip he can play");
    state_ = State::Done;
    return state_;
  }
  if (!client_.connected() && client_.available() <= 0) return fail("the brain hung up early");
  if (millis() - progressMs_ > kStallMs) return fail("the download stalled");
  if (millis() - startedMs_ > kTotalMs) return fail("the download is taking too long");
  return state_;
}

void CardFetch::abort() {
  client_.stop();
  connected_ = inBody_ = false;
  tries_ = 0;
  len_ = got_ = 0;
  head_.clear();
  state_ = State::Idle;
}
