#include "link_client.h"

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstring>

#include "hal_log.h"

namespace {

// Connect with a timeout; returns a non-blocking socket or -1.
int connectTo(const std::string& host, int port, int timeoutMs) {
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* res = nullptr;
  std::string portStr = std::to_string(port);
  if (getaddrinfo(host.c_str(), portStr.c_str(), &hints, &res) != 0 || !res) return -1;
  int fd = -1;
  for (addrinfo* a = res; a && fd < 0; a = a->ai_next) {
    int s = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
    if (s < 0) continue;
    fcntl(s, F_SETFL, fcntl(s, F_GETFL) | O_NONBLOCK);
    int rc = connect(s, a->ai_addr, a->ai_addrlen);
    if (rc != 0 && errno == EINPROGRESS) {
      pollfd p{s, POLLOUT, 0};
      int err = 0;
      socklen_t len = sizeof err;
      if (::poll(&p, 1, timeoutMs) == 1 && getsockopt(s, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err == 0) rc = 0;
    }
    if (rc == 0) {
      int one = 1;
      setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
#ifdef SO_NOSIGPIPE
      setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
      fd = s;
    } else {
      close(s);
    }
  }
  freeaddrinfo(res);
  return fd;
}

}  // namespace

void LinkClient::start(const std::string& host, int port) {
  stop();
  host_ = host;
  port_ = port;
  running_ = true;
  worker_ = std::thread(&LinkClient::run, this);
}

void LinkClient::stop() {
  running_ = false;
  if (worker_.joinable()) worker_.join();
  closeFd();
}

void LinkClient::run() {
  bool announced = false;
  while (running_) {
    if (fd_.load() < 0) {
      int fd = connectTo(host_, port_, 3000);
      if (fd >= 0) {
        fd_ = fd;
        halLog("link: connected to %s:%d", host_.c_str(), port_);
        hal::Message hello;
        hello.type = hal::MsgType::Hello;
        hello.role = hal::Role::New;
        hello.version = hal::kLinkVersion;
        send(hello);
        announced = false;
      } else if (!announced) {
        halLog("link: %s:%d not answering, will keep trying", host_.c_str(), port_);
        announced = true;
      }
    }
    for (int i = 0; i < 50 && running_; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}

void LinkClient::closeFd() {
  int fd = fd_.exchange(-1);
  if (fd >= 0) close(fd);
}

bool LinkClient::send(const hal::Message& m) {
  int fd = fd_.load();
  if (fd < 0) return false;
  std::string line = hal::formatMessage(m) + "\n";
  std::lock_guard<std::mutex> lock(writeMu_);
#ifdef MSG_NOSIGNAL
  ssize_t n = ::send(fd, line.data(), line.size(), MSG_NOSIGNAL);
#else
  ssize_t n = ::send(fd, line.data(), line.size(), 0);
#endif
  if (n != ssize_t(line.size())) {
    closeFd();
    dropped_ = true;
    return false;
  }
  return true;
}

void LinkClient::poll(std::vector<hal::Message>* in, bool* dropped) {
  in->clear();
  int fd = fd_.load();
  if (fd >= 0) {
    char buf[256];
    for (;;) {
      ssize_t n = recv(fd, buf, sizeof buf, 0);
      if (n > 0) {
        for (const std::string& line : splitter_.feed(buf, size_t(n))) {
          hal::Message m = hal::parseMessage(line);
          if (m.type != hal::MsgType::Invalid) in->push_back(m);
        }
        continue;
      }
      if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
        halLog("link: the desk HAL hung up");
        closeFd();
        dropped_ = true;
      }
      break;
    }
  }
  *dropped = dropped_.exchange(false);
}
