#include "link_server.h"

#include <ESPmDNS.h>
#include <lwip/sockets.h>

#include "hal_log.h"

bool LinkServer::begin(int port, const char* host, int version, const char* iam, size_t maxLine) {
  if (server_ || WiFi.status() != WL_CONNECTED) return server_ != nullptr;
  version_ = version;
  iam_ = iam;
  guard_ = hal::LinkGuard(maxLine);
  if (MDNS.begin(host)) MDNS.addService("hal9000", "tcp", port);
  server_ = new WiFiServer(port);
  server_->begin();
  server_->setNoDelay(true);
  halLog("link: listening on %s:%d (%s.local)", WiFi.localIP().toString().c_str(), port, host);
  return true;
}

void LinkServer::poll(std::vector<hal::Message>* in, bool* dropped) {
  in->clear();
  *dropped = false;
  if (!server_) return;
  const uint32_t now = millis();
  if (!client_.connected()) {
    if (hadClient_) {
      if (!hungUp_) halLog("link: HAL Zero hung up");
      *dropped = true;
      hadClient_ = hungUp_ = false;
      // Nobody else comes in on this pass: the caller sees the link down for one pass, and so
      // greets whoever is next as a new peer instead of carrying on with the last one's state.
      return;
    }
    if (guard_.coolingOff(now)) return;  // someone flooded us a moment ago: nobody in just yet
    WiFiClient c = server_->available();
    if (c) {
      client_ = c;
      client_.setNoDelay(true);
      hadClient_ = true;
      guard_.newPeer();
      halLog("link: HAL Zero connected from %s", client_.remoteIP().toString().c_str());
      hal::Message hello;
      hello.type = hal::MsgType::Hello;
      hello.role = hal::Role::Old;
      hello.version = version_;
      send(hello);
      if (!iam_.empty()) {  // a friend says which HAL he is
        hal::Message iam;
        iam.type = hal::MsgType::Iam;
        iam.who = iam_;
        send(iam);
      }
    }
    return;
  }
  // The guard decides how much is read and handed over in one pass (see hal_link.h); read()
  // only returns what has already arrived, so this never waits.
  auto read = [this](char* buf, size_t max) { return client_.read(reinterpret_cast<uint8_t*>(buf), max); };
  if (!guard_.take(now, read, in)) {
    hangUp("sent more than a HAL Zero ever does");
    return;
  }
  msgsIn_ += int(in->size());
  if (!in->empty()) last_ = guard_.lastLine();
}

bool LinkServer::send(const hal::Message& m) {
  if (!client_.connected()) return false;
  const std::string line = hal::formatMessage(m) + "\n";
  // Not client_.write(): the core's write waits for a peer who has stopped reading, a second at
  // a time and up to ten times over, with the eye and the touch screen frozen meanwhile. This
  // asks the socket once, without waiting. A line is at most 129 bytes and the socket keeps
  // 5744 for the peer, so a HAL Zero who is reading always has room. If the whole line doesn't
  // go, the peer has stopped reading or the network has gone: hang up, as HAL Zero does to us
  // (src/linux/link_client.cpp). Half a line can't be taken back, so that ends it as well.
  const int took = lwip_send(client_.fd(), line.data(), line.size(), MSG_DONTWAIT);
  if (took != int(line.size())) {
    hangUp("has stopped reading");
    return false;
  }
  ++msgsOut_;
  return true;
}

void LinkServer::drop() {
  if (client_) client_.stop();
}

void LinkServer::hangUp(const char* why) {
  halLog("link: hanging up on %s: he %s", client_.remoteIP().toString().c_str(), why);
  client_.stop();
  hungUp_ = true;
}

std::string LinkServer::peerAddress() {
  if (!client_ || !client_.connected()) return "";
  return client_.remoteIP().toString().c_str();
}
