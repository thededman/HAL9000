#include "brain_client.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>

#include "hal_log.h"
#include "hal_wav.h"

namespace {

using Clock = std::chrono::steady_clock;

int msLeft(Clock::time_point deadline) {
  auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
  return left > 0 ? static_cast<int>(left) : 0;
}

}  // namespace

void BrainClient::start(const std::string& host, int port, const std::string& key) {
  stop();
  host_ = host;
  port_ = port;
  key_ = key;
  std::signal(SIGPIPE, SIG_IGN);  // a brain that hangs up mid-send is an error, not a crash
  running_ = true;
  worker_ = std::thread([this] { run(); });
}

void BrainClient::stop() {
  if (!running_.exchange(false)) return;
  cancel();
  wake_.notify_all();
  if (worker_.joinable()) worker_.join();
}

void BrainClient::ask(const std::string& who, const std::string& body) {
  if (!running_) return;
  {
    std::lock_guard<std::mutex> lock(mu_);
    jobs_.push_back({generation_.load(), who, body});
    ++pending_;
  }
  wake_.notify_all();
}

void BrainClient::cancel() {
  ++generation_;
  {
    std::lock_guard<std::mutex> lock(mu_);
    pending_ -= static_cast<int>(jobs_.size());
    jobs_.clear();
    done_.clear();
  }
  int fd = fd_.load();
  if (fd >= 0) shutdown(fd, SHUT_RDWR);  // wakes the worker out of connect/recv
}

void BrainClient::poll(std::vector<BrainEvent>* out) {
  std::lock_guard<std::mutex> lock(mu_);
  while (!done_.empty()) {
    out->push_back(std::move(done_.front()));
    done_.pop_front();
  }
}

std::string BrainClient::address() {
  std::lock_guard<std::mutex> lock(mu_);
  return address_;
}

void BrainClient::run() {
  while (running_) {
    Job job;
    {
      std::unique_lock<std::mutex> lock(mu_);
      wake_.wait(lock, [this] { return !running_ || !jobs_.empty(); });
      if (!running_) return;
      job = std::move(jobs_.front());
      jobs_.pop_front();
    }
    BrainEvent event = doAsk(job);
    std::lock_guard<std::mutex> lock(mu_);
    --pending_;
    if (job.generation == generation_.load()) done_.push_back(std::move(event));  // else: cancelled meanwhile
  }
}

BrainEvent BrainClient::doAsk(const Job& job) {
  BrainEvent event;
  hal::HttpResponse response;
  std::string error = http("POST", "/ask?who=" + job.who, "text/plain", job.body, kAskMs, 4096, &response);
  if (error.empty()) {
    event.reply = hal::parseBrainReply(response);
    if (!event.reply.ok) error = event.reply.error;
    else if (event.reply.text.empty() || event.reply.clip.empty()) error = "bad-reply";
  }
  if (error.empty()) {
    hal::HttpResponse clip;
    error = http("GET", "/clip/" + event.reply.clip, "", "", kClipMs, kMaxClipBytes, &clip);
    hal::WavInfo info;
    if (error.empty() && clip.status != 200) error = "bad-clip";
    if (error.empty()) {
      event.wav.assign(clip.body.begin(), clip.body.end());
      if (!hal::parseWav(event.wav.data(), event.wav.size(), &info) || info.dataBytes == 0) error = "bad-clip";
    }
  }
  if (!error.empty()) {
    event = BrainEvent();
    event.error = error;
    return event;
  }
  event.kind = BrainEvent::Kind::Answer;
  return event;
}

std::string BrainClient::http(const std::string& method, const std::string& path, const std::string& contentType,
                              const std::string& body, int timeoutMs, size_t maxBytes, hal::HttpResponse* out) {
  const std::string request = hal::httpRequest(method, host_, port_, path, key_, contentType, body);
  if (request.empty()) return "bad-request";

  addrinfo hints{};
  hints.ai_family = AF_INET;  // the desk HAL is told this address too, and he speaks IPv4
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* found = nullptr;
  if (getaddrinfo(host_.c_str(), std::to_string(port_).c_str(), &hints, &found) != 0 || !found) return "offline";
  char dotted[INET_ADDRSTRLEN] = "";
  inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(found->ai_addr)->sin_addr, dotted, sizeof dotted);

  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    freeaddrinfo(found);
    return "offline";
  }
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
  fd_ = fd;
  std::string error;
  const auto fail = [&](const char* why) {
    error = why;
    return false;
  };
  // Wait until the socket is ready for `events`, within `ms`.
  const auto ready = [&](short events, int ms) {
    pollfd p{fd, events, 0};
    int r = ::poll(&p, 1, ms);
    return r > 0 && !(p.revents & (POLLERR | POLLNVAL)) ? true : fail(r == 0 ? "timeout" : "offline");
  };

  const int generation = generation_.load();
  bool ok = true;
  if (connect(fd, found->ai_addr, found->ai_addrlen) != 0) {
    ok = errno == EINPROGRESS && ready(POLLOUT, kConnectMs);
    int soError = 0;
    socklen_t len = sizeof soError;
    if (ok && (getsockopt(fd, SOL_SOCKET, SO_ERROR, &soError, &len) != 0 || soError != 0)) ok = fail("offline");
    if (!ok && error == "timeout") error = "offline";  // nobody at that address
  }
  freeaddrinfo(found);

  const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
  size_t sent = 0;
  while (ok && sent < request.size()) {
    if (!ready(POLLOUT, msLeft(deadline))) {
      ok = false;
      break;
    }
    ssize_t n = send(fd, request.data() + sent, request.size() - sent, 0);
    if (n > 0) sent += static_cast<size_t>(n);
    else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) ok = fail("offline");
  }
  std::string raw;
  char buf[8192];
  while (ok) {
    if (!ready(POLLIN, msLeft(deadline))) {
      ok = false;
      break;
    }
    ssize_t n = recv(fd, buf, sizeof buf, 0);
    if (n == 0) break;  // the brain closed: the response is complete
    if (n < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) continue;
      ok = fail("offline");
    } else {
      raw.append(buf, static_cast<size_t>(n));
      if (raw.size() > maxBytes + 2048) ok = fail("bad-reply");  // more than any reply or clip
    }
  }
  fd_ = -1;
  close(fd);
  if (generation != generation_.load()) return "cancelled";
  if (!ok) return error.empty() ? "offline" : error;
  if (!hal::parseHttpResponse(raw, out)) return "bad-reply";
  std::lock_guard<std::mutex> lock(mu_);
  address_ = dotted;
  return "";
}
