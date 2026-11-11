#include "link_server.h"

#include <ESPmDNS.h>

#include "hal_log.h"

bool LinkServer::begin(int port) {
  if (server_ || WiFi.status() != WL_CONNECTED) return server_ != nullptr;
  if (MDNS.begin("hal9000-old")) MDNS.addService("hal9000", "tcp", port);
  server_ = new WiFiServer(port);
  server_->begin();
  server_->setNoDelay(true);
  halLog("link: listening on %s:%d (hal9000-old.local)", WiFi.localIP().toString().c_str(), port);
  return true;
}

void LinkServer::poll(std::vector<hal::Message>* in, bool* dropped) {
  in->clear();
  *dropped = false;
  if (!server_) return;
  if (!client_ || !client_.connected()) {
    if (hadClient_) {
      halLog("link: HAL Zero hung up");
      *dropped = true;
      hadClient_ = false;
    }
    WiFiClient c = server_->available();
    if (c) {
      client_ = c;
      client_.setNoDelay(true);
      hadClient_ = true;
      halLog("link: HAL Zero connected from %s", client_.remoteIP().toString().c_str());
      hal::Message hello;
      hello.type = hal::MsgType::Hello;
      hello.role = hal::Role::Old;
      hello.version = hal::kLinkVersion;
      send(hello);
    }
    return;
  }
  char buf[128];
  while (client_.available()) {
    int n = client_.read(reinterpret_cast<uint8_t*>(buf), sizeof buf);
    if (n <= 0) break;
    for (const std::string& line : splitter_.feed(buf, size_t(n))) {
      hal::Message m = hal::parseMessage(line);
      if (m.type != hal::MsgType::Invalid) in->push_back(m);
    }
  }
}

bool LinkServer::send(const hal::Message& m) {
  if (!client_ || !client_.connected()) return false;
  std::string line = hal::formatMessage(m) + "\n";
  return client_.write(reinterpret_cast<const uint8_t*>(line.data()), line.size()) == line.size();
}
