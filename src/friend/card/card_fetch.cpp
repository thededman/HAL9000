#include "card_fetch.h"

#include <Arduino.h>

#include "hal_fetchrule.h"
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
  request_ = hal::httpRequest("GET", host, port, "/clip/" + token, "", "", "");
  if (request_.empty()) return refuse("bad request");
  if (!socket_.open(host, port)) return refuse("couldn't start a connection");
  buf_ = buf;
  capacity_ = capacity;
  progressMs_ = startedMs_ = millis();
  state_ = State::Fetching;  // poll() sees the connection come up, however long the Wi-Fi takes over it
  return true;
}

CardFetch::State CardFetch::fail(const char* why) {
  halLog("clip: %s (after %u ms)", why, unsigned(millis() - startedMs_));
  socket_.close();
  asked_ = inBody_ = false;
  len_ = got_ = 0;
  head_.clear();
  state_ = State::Failed;
  return state_;
}

CardFetch::State CardFetch::poll() {
  if (state_ != State::Fetching) return state_;
  bool hungUp = false;
  if (!asked_) {
    const PatientSocket::State up = socket_.poll();
    if (up == PatientSocket::State::Failed) return fail("the brain refused the connection, or can't be reached");
    if (up == PatientSocket::State::Open) {
      if (!socket_.send(request_)) return fail("the request wouldn't go");
      asked_ = true;
      progressMs_ = millis();
      halLog("clip: connected in %u ms", unsigned(socket_.connectMs()));
    }
  } else {
    const uint32_t started = millis();
    while (millis() - started < kSliceMs) {
      if (!inBody_) {  // still in the head: a byte at a time, up to its blank line
        uint8_t c = 0;
        const int n = socket_.read(&c, 1);
        if (n <= 0) {
          hungUp = n < 0;
          break;
        }
        progressMs_ = millis();
        head_.push_back(static_cast<char>(c));
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
      const int n = socket_.read(buf_ + got_, len_ - got_);
      if (n <= 0) {
        hungUp = n < 0;
        break;
      }
      progressMs_ = millis();
      got_ += static_cast<size_t>(n);
      if (got_ == len_) break;
    }
    if (inBody_ && got_ == len_) {  // all here: it has to be a clip he can play
      socket_.close();
      hal::WavInfo info;
      if (!hal::parseClipWav(buf_, len_, &info) || !hal::playableClip(info) || info.bitsPerSample != 8 || info.channels != 1)
        return fail("that isn't a small 8-bit clip he can play");
      state_ = State::Done;
      return state_;
    }
    if (hungUp) return fail("the brain hung up early");
  }
  switch (hal::fetchVerdict(asked_, startedMs_, progressMs_, millis())) {
    case hal::FetchVerdict::NoAnswer: return fail("the brain didn't answer the connection");
    case hal::FetchVerdict::Stalled: return fail("the download stalled");
    case hal::FetchVerdict::TooLong: return fail("the download is taking too long");
    case hal::FetchVerdict::Wait: break;
  }
  return state_;
}

void CardFetch::abort() {
  socket_.close();
  asked_ = inBody_ = false;
  len_ = got_ = 0;
  head_.clear();
  state_ = State::Idle;
}
