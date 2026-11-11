#pragma once
// The desk HAL's end of the HAL Zero link (link=on in config.txt): announces
// itself as hal9000-old.local and takes one connection on the link port.
#include <WiFi.h>

#include <vector>

#include "hal_link.h"

class LinkServer {
 public:
  bool begin(int port);  // Wi-Fi must be connected
  bool started() const { return server_ != nullptr; }
  bool connected() { return client_ && client_.connected(); }
  void poll(std::vector<hal::Message>* in, bool* dropped);  // never blocks
  bool send(const hal::Message& m);

 private:
  WiFiServer* server_ = nullptr;
  WiFiClient client_;
  bool hadClient_ = false;
  hal::LineSplitter splitter_;
};
