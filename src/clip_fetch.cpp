#include "clip_fetch.h"

#include <Arduino.h>

#include "hal_http.h"
#include "hal_link.h"
#include "hal_wav.h"

bool ClipFetch::begin(const std::string& host, int port, const std::string& token) {
  abort();
  const auto refuse = [this](const char* why) {
    Serial.printf("clip: %s\n", why);
    client_.stop();
    state_ = State::Failed;
    return false;
  };
  // The brain is on this network. Anything else a link message names (another private network
  // the router can reach, a VPN) is not ours to fetch from.
  if (!hal::sameSubnet(host, WiFi.localIP().toString().c_str(), WiFi.subnetMask().toString().c_str()))
    return refuse("that address isn't on this network");
  const std::string request = hal::httpRequest("GET", host, port, "/clip/" + token, "", "", "");
  if (request.empty()) return refuse("bad request");
  // Connecting is the one step that holds the loop (and the eye) up. If nobody answered a moment
  // ago, don't wait again so soon: a peer can't freeze him by naming dead addresses in a row.
  if (refusedMs_ && millis() - refusedMs_ < kRetryMs) return refuse("the brain didn't answer a moment ago");
  if (!client_.connect(host.c_str(), port, kConnectMs)) {
    refusedMs_ = millis() ? millis() : 1;
    return refuse("can't reach the brain");
  }
  refusedMs_ = 0;
  client_.setNoDelay(true);
  client_.print(request.c_str());
  head_.clear();
  progressMs_ = startedMs_ = millis();
  state_ = State::Fetching;
  return true;
}

ClipFetch::State ClipFetch::fail(const char* why) {
  Serial.printf("clip: %s\n", why);
  client_.stop();
  if (buf_) free(buf_);
  buf_ = nullptr;
  len_ = got_ = 0;
  head_.clear();
  state_ = State::Failed;
  return state_;
}

ClipFetch::State ClipFetch::poll() {
  if (state_ != State::Fetching) return state_;
  const uint32_t started = millis();
  while (client_.available() > 0 && millis() - started < kSliceMs) {
    progressMs_ = millis();
    if (!buf_) {  // still in the head: a byte at a time, up to its blank line
      head_.push_back(static_cast<char>(client_.read()));
      if (head_.size() > 1024) return fail("the reply's head is too long");
      if (head_.size() < 4 || head_.compare(head_.size() - 4, 4, "\r\n\r\n") != 0) continue;
      int status = 0;
      long length = -1;
      if (!hal::parseHttpHead(head_, &status, &length)) return fail("not an HTTP reply");
      if (status != 200) return fail("the brain doesn't have that clip");
      if (length < 44 || static_cast<size_t>(length) > kMaxBytes) return fail("the clip is missing its size, or too big");
      buf_ = static_cast<uint8_t*>(ps_malloc(static_cast<size_t>(length)));
      if (!buf_) return fail("no memory for the clip");
      len_ = static_cast<size_t>(length);
      got_ = 0;
      continue;
    }
    int n = client_.read(buf_ + got_, len_ - got_);
    if (n > 0) got_ += static_cast<size_t>(n);
    if (got_ == len_) break;
  }
  if (buf_ && got_ == len_) {  // all here: it has to be a WAV he can play
    client_.stop();
    hal::WavInfo info;
    if (!hal::parseWav(buf_, len_, &info) || !hal::playableClip(info)) return fail("that isn't a WAV he can play");
    state_ = State::Done;
    return state_;
  }
  if (!client_.connected() && client_.available() <= 0) return fail("the brain hung up early");
  if (millis() - progressMs_ > kStallMs) return fail("the download stalled");
  if (millis() - startedMs_ > kTotalMs) return fail("the download is taking too long");
  return state_;
}

bool ClipFetch::take(uint8_t** data, size_t* len) {
  if (state_ != State::Done) return false;
  *data = buf_;
  *len = len_;
  buf_ = nullptr;
  len_ = got_ = 0;
  state_ = State::Idle;
  return true;
}

void ClipFetch::abort() {
  client_.stop();
  if (buf_) free(buf_);
  buf_ = nullptr;
  len_ = got_ = 0;
  head_.clear();
  state_ = State::Idle;
}
