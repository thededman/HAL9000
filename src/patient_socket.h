#pragma once
// A connection that never makes the loop wait. Connecting to the brain is the one step of a
// fetch that can take a while (hal_fetchrule.h says why), and the usual call for it stops
// everything until it is done. This one is started and then asked, a pass at a time, whether
// it is up yet; sending and reading never wait either. Used by the desk HAL's clip fetcher and
// HAL Card's.
#include <cstddef>
#include <cstdint>
#include <string>

class PatientSocket {
 public:
  enum class State { Closed, Connecting, Open, Failed };

  ~PatientSocket() { close(); }
  // Starts connecting to a dotted IPv4 address (closing whatever was open). False if it can't
  // even be started.
  bool open(const std::string& ipv4, int port);
  // Connecting -> Open once the brain has answered, or Failed if it refused or can't be reached
  // (how long to go on waiting is the caller's business). Never waits.
  State poll();
  State state() const { return state_; }
  uint32_t connectMs() const { return connectMs_; }  // how long the connection took, once Open
  bool send(const std::string& data);                // all of it at once, or false (and the connection is closed)
  // What has already arrived: a count of bytes put in `buf`, 0 for nothing yet, -1 once the
  // other end has closed (or the connection broke) and nothing is left.
  int read(uint8_t* buf, size_t max);
  void close();

 private:
  int fd_ = -1;
  State state_ = State::Closed;
  uint32_t startedMs_ = 0, connectMs_ = 0;
};
