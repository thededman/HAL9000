#include "patient_socket.h"

#include <Arduino.h>
#include <lwip/sockets.h>

#include <cerrno>

bool PatientSocket::open(const std::string& ipv4, int port) {
  close();
  sockaddr_in to = {};
  to.sin_family = AF_INET;
  to.sin_port = htons(static_cast<uint16_t>(port));
  if (port < 1 || port > 65535 || lwip_inet_pton(AF_INET, ipv4.c_str(), &to.sin_addr) != 1) return false;
  fd_ = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (fd_ < 0) return false;
  const int flags = lwip_fcntl(fd_, F_GETFL, 0);
  lwip_fcntl(fd_, F_SETFL, flags | O_NONBLOCK);
  const int one = 1;
  lwip_setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
  startedMs_ = millis();
  connectMs_ = 0;
  if (lwip_connect(fd_, reinterpret_cast<sockaddr*>(&to), sizeof to) == 0) {  // (at once: only ever to oneself)
    state_ = State::Open;
    return true;
  }
  if (errno != EINPROGRESS) {
    close();
    state_ = State::Failed;
    return false;
  }
  state_ = State::Connecting;
  return true;
}

PatientSocket::State PatientSocket::poll() {
  if (state_ != State::Connecting) return state_;
  fd_set writable;
  FD_ZERO(&writable);
  FD_SET(fd_, &writable);
  timeval none = {0, 0};
  if (lwip_select(fd_ + 1, nullptr, &writable, nullptr, &none) <= 0) return state_;  // not yet
  int error = 0;
  socklen_t size = sizeof error;
  if (lwip_getsockopt(fd_, SOL_SOCKET, SO_ERROR, &error, &size) != 0 || error != 0) {  // refused, or no way there
    close();
    state_ = State::Failed;
    return state_;
  }
  connectMs_ = millis() - startedMs_;
  state_ = State::Open;
  return state_;
}

bool PatientSocket::send(const std::string& data) {
  if (state_ != State::Open) return false;
  // A request is a couple of hundred bytes and the connection is new: it always fits. If it
  // somehow doesn't, half a request is no use to anyone.
  const int took = lwip_send(fd_, data.data(), data.size(), MSG_DONTWAIT);
  if (took == static_cast<int>(data.size())) return true;
  close();
  state_ = State::Failed;
  return false;
}

int PatientSocket::read(uint8_t* buf, size_t max) {
  if (state_ != State::Open || max == 0) return state_ == State::Open ? 0 : -1;
  const int n = lwip_recv(fd_, buf, max, MSG_DONTWAIT);
  if (n > 0) return n;
  if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 0;  // nothing has arrived yet
  return -1;  // closed by the other end, or broken
}

void PatientSocket::close() {
  if (fd_ >= 0) lwip_close(fd_);
  fd_ = -1;
  state_ = State::Closed;
}
