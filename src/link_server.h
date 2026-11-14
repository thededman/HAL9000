#pragma once
// The desk HAL's end of the HAL Zero link (link=on in config.txt): announces
// itself as hal9000-old.local and takes one connection on the link port. The friends (HAL
// Nano, HAL Paper, HAL Card) use it too, each under his own name.
#include <WiFi.h>

#include <string>
#include <vector>

#include "hal_link.h"

class LinkServer {
 public:
  // Wi-Fi must be connected. `host` is the name announced on the network (<host>.local) and
  // `version` what his HELLO says. A friend (v4) also says who he is straight after it (`iam`:
  // a cast id, hal_cast.h) and reads the longer lines he is sent (`maxLine`).
  bool begin(int port, const char* host = "hal9000-old", int version = hal::kLinkVersion, const char* iam = "",
             size_t maxLine = hal::kLinkMaxLine);
  bool started() const { return server_ != nullptr; }
  bool connected() { return client_ && client_.connected(); }
  // Never blocks, and never takes more in one pass than hal::LinkGuard allows: a peer who sends
  // more is hung up on. `dropped` is set on the first poll after the link went, whoever ended it.
  void poll(std::vector<hal::Message>* in, bool* dropped);
  // Never waits: a peer who can't take the whole line at once is hung up on (the next poll reports it).
  bool send(const hal::Message& m);
  void drop();  // hang up on HAL Zero (he went quiet); the next poll reports it
  // For the comms screen.
  int msgsIn() const { return msgsIn_; }
  int msgsOut() const { return msgsOut_; }
  const std::string& lastMessage() const { return last_; }
  std::string peerAddress();

 private:
  void hangUp(const char* why);
  WiFiServer* server_ = nullptr;
  WiFiClient client_;
  bool hadClient_ = false;
  bool hungUp_ = false;  // we ended it and said why: no "HAL Zero hung up" in the log
  hal::LinkGuard guard_;
  int version_ = hal::kLinkVersion;
  std::string iam_;
  int msgsIn_ = 0, msgsOut_ = 0;
  std::string last_;
};
