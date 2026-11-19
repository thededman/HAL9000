#pragma once
// HAL Card downloads a line the AI brain made for him: HAL Zero says where it is (FETCH n
// <address> <port> <token>, already checked by the link's parser) and this fetches
// http://address:port/clip/token into the one buffer he has, a few milliseconds at a time so
// his eye keeps moving. Like the desk HAL (src/clip_fetch.h) he fetches only from his own
// network and never waits, not even to connect (hal_fetchrule.h); unlike him he has no spare
// memory, so only a small unsigned 8-bit mono clip that fits the buffer is taken.
#include <WiFi.h>

#include <string>

#include "../../patient_socket.h"

class CardFetch {
 public:
  enum class State { Idle, Fetching, Done, Failed };
  static constexpr uint32_t kSliceMs = 5;  // reading time per loop pass

  // Starts the download into `buf` (room for `capacity` bytes), dropping one in progress.
  // False if it can't even be tried; whether the brain was reached comes from poll().
  bool begin(const std::string& host, int port, const std::string& token, uint8_t* buf, size_t capacity);
  State poll();                       // call every loop pass
  size_t length() const { return state_ == State::Done ? len_ : 0; }  // the clip's bytes, once Done
  void abort();                       // drop the download; back to Idle

 private:
  State fail(const char* why);

  PatientSocket socket_;
  State state_ = State::Idle;
  std::string request_, head_;
  bool asked_ = false, inBody_ = false;
  uint8_t* buf_ = nullptr;
  size_t capacity_ = 0, len_ = 0, got_ = 0;
  uint32_t progressMs_ = 0, startedMs_ = 0;
};
