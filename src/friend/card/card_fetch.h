#pragma once
// HAL Card downloads a line the AI brain made for him: HAL Zero says where it is (FETCH n
// <address> <port> <token>, already checked by the link's parser) and this fetches
// http://address:port/clip/token into the one buffer he has, a few milliseconds at a time so
// his eye keeps moving. Like the desk HAL (src/clip_fetch.h) he fetches only from his own
// network; unlike him he has no spare memory, so only a small unsigned 8-bit mono clip that
// fits the buffer is taken.
#include <WiFi.h>

#include <string>

class CardFetch {
 public:
  enum class State { Idle, Fetching, Done, Failed };
  static constexpr uint32_t kConnectMs = 500;  // the one wait that holds the loop up: keep it short
  static constexpr int kTries = 2;             // ... and try twice: a computer that has just woken is slow to answer
  static constexpr uint32_t kTryGapMs = 1500;
  static constexpr uint32_t kRetryMs = 2000;   // after nobody answered, don't try (and wait) again for this long
  static constexpr uint32_t kStallMs = 3000;   // no progress for this long: give up
  static constexpr uint32_t kTotalMs = 7000;   // the whole download: HAL Zero stops waiting after 8 s
  static constexpr uint32_t kSliceMs = 5;      // reading time per loop pass

  // Starts the download into `buf` (room for `capacity` bytes), dropping one in progress.
  // False if it can't even be tried; whether the brain was reached comes from poll().
  bool begin(const std::string& host, int port, const std::string& token, uint8_t* buf, size_t capacity);
  State poll();                       // call every loop pass
  size_t length() const { return state_ == State::Done ? len_ : 0; }  // the clip's bytes, once Done
  void abort();                       // drop the download; back to Idle

 private:
  State fail(const char* why);

  WiFiClient client_;
  State state_ = State::Idle;
  std::string host_, request_, head_;
  int port_ = 0;
  bool connected_ = false, inBody_ = false;
  int tries_ = 0;
  uint8_t* buf_ = nullptr;
  size_t capacity_ = 0, len_ = 0, got_ = 0;
  uint32_t nextTryMs_ = 0, progressMs_ = 0, startedMs_ = 0;
  uint32_t refusedMs_ = 0;  // when a connection last failed (0: none has)
};
