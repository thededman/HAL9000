#pragma once
// The desk HAL downloads a line the AI brain wrote for him: HAL Zero says where it is
// (FETCH n <address> <port> <token>, already checked by the link's parser), and this
// fetches http://address:port/clip/token into PSRAM a few milliseconds at a time, so
// the eye keeps moving. Only an address on his own network is ever fetched from, and only
// a 16-bit PCM WAV of a sensible size and length is accepted. Nothing here waits: not even
// connecting, which on Wi-Fi can take a second or more (hal_fetchrule.h).
#include <WiFi.h>

#include <string>

#include "patient_socket.h"

class ClipFetch {
 public:
  enum class State { Idle, Fetching, Done, Failed };
  static constexpr size_t kMaxBytes = 1536000;
  static constexpr uint32_t kSliceMs = 5;  // reading time per loop pass

  ~ClipFetch() { abort(); }
  // Starts the download (drops one in progress). False if it can't even be tried (an address
  // that isn't ours to fetch from); whether the brain was reached comes from poll().
  bool begin(const std::string& host, int port, const std::string& token);
  State poll();                              // call every loop pass
  State state() const { return state_; }
  // After Done: hands the clip over (free() it when finished) and returns to Idle.
  bool take(uint8_t** data, size_t* len);
  void abort();                              // drop the download and any clip not taken

 private:
  State fail(const char* why);

  PatientSocket socket_;
  State state_ = State::Idle;
  std::string request_;
  bool asked_ = false;      // the connection is up and the request has gone
  std::string head_;        // the response head, until its blank line
  uint8_t* buf_ = nullptr;  // the body, in PSRAM
  size_t len_ = 0, got_ = 0;
  uint32_t progressMs_ = 0, startedMs_ = 0;
};
