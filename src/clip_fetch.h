#pragma once
// The desk HAL downloads a line the AI brain wrote for him: HAL Zero says where it is
// (FETCH n <address> <port> <token>, already checked by the link's parser), and this
// fetches http://address:port/clip/token into PSRAM a few milliseconds at a time, so
// the eye keeps moving. Only an address on his own network is ever fetched from, and only
// a 16-bit PCM WAV of a sensible size and length is accepted.
#include <WiFi.h>

#include <string>

class ClipFetch {
 public:
  enum class State { Idle, Fetching, Done, Failed };
  static constexpr size_t kMaxBytes = 1536000;
  static constexpr uint32_t kConnectMs = 400;  // the one wait that holds the loop up: keep it short
  static constexpr uint32_t kRetryMs = 2000;   // after nobody answered, don't try (and wait) again for this long
  static constexpr uint32_t kStallMs = 3000;   // no progress for this long: give up
  static constexpr uint32_t kTotalMs = 7000;   // the whole download: HAL Zero stops waiting after 8 s
  static constexpr uint32_t kSliceMs = 5;      // reading time per loop pass

  ~ClipFetch() { abort(); }
  // Starts the download (drops one in progress). False if the brain can't be reached.
  bool begin(const std::string& host, int port, const std::string& token);
  State poll();                              // call every loop pass
  State state() const { return state_; }
  // After Done: hands the clip over (free() it when finished) and returns to Idle.
  bool take(uint8_t** data, size_t* len);
  void abort();                              // drop the download and any clip not taken

 private:
  State fail(const char* why);

  WiFiClient client_;
  State state_ = State::Idle;
  std::string head_;        // the response head, until its blank line
  uint8_t* buf_ = nullptr;  // the body, in PSRAM
  size_t len_ = 0, got_ = 0;
  uint32_t progressMs_ = 0, startedMs_ = 0;
  uint32_t refusedMs_ = 0;  // when a connection last failed (0: none has)
};
