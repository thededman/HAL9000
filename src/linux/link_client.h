#pragma once
// HAL Zero's connection to the desk HAL (TCP, one text line per message, see
// hal_link.h). A background thread keeps trying to connect while the link is
// on; the main loop sends and polls without ever blocking.
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "hal_link.h"

class LinkClient {
 public:
  ~LinkClient() { stop(); }
  void start(const std::string& host, int port);  // keeps (re)connecting until stop()
  void stop();
  bool connected() const { return fd_.load() >= 0; }
  bool send(const hal::Message& m);  // false if not connected (or the write failed)
  void drop();                       // give up on this connection (the peer went quiet); reconnects
  // Messages received since the last call; `dropped` is set when the connection went away.
  // Never takes more in one call than hal::LinkGuard allows: a peer who sends more is hung up on.
  void poll(std::vector<hal::Message>* in, bool* dropped);

 private:
  void run();
  void closeFd();
  std::string host_;
  int port_ = 0;
  std::atomic<int> fd_{-1};
  std::atomic<bool> running_{false};
  std::atomic<bool> dropped_{false};
  std::atomic<unsigned> connections_{0};  // how many times the worker thread has connected
  unsigned polledConnection_ = 0;         // the connection poll() last read from
  std::thread worker_;
  std::mutex writeMu_;
  hal::LinkGuard guard_;                  // only poll() touches it
};
