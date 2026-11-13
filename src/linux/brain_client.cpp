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

BrainEvent failed(const std::string& error) {
  BrainEvent event;
  event.error = error;
  return event;
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

void BrainClient::push(Job job) {
  if (!running_) return;
  job.generation = generation_.load();
  {
    std::lock_guard<std::mutex> lock(mu_);
    jobs_.push_back(std::move(job));
    ++pending_;
  }
  wake_.notify_all();
}

void BrainClient::ask(const std::string& who, const std::string& body) {
  Job job;
  job.kind = Job::Kind::Ask;
  job.who = who;
  job.body = body;
  push(std::move(job));
}

void BrainClient::debate(const std::string& body, int turns) {
  Job job;
  job.kind = Job::Kind::Debate;
  job.body = body;
  job.n = turns;
  push(std::move(job));
}

void BrainClient::debateLine(const std::string& id, int n) {
  Job job;
  job.kind = Job::Kind::DebateLine;
  job.id = id;
  job.n = n;
  push(std::move(job));
}

void BrainClient::stopDebate(const std::string& id) {
  Job job;
  job.kind = Job::Kind::StopDebate;
  job.id = id;
  push(std::move(job));
}

void BrainClient::hear(std::vector<uint8_t> wav) {
  Job job;
  job.kind = Job::Kind::Hear;
  job.body.assign(wav.begin(), wav.end());
  push(std::move(job));
}

void BrainClient::cancel() {
  ++generation_;
  {
    std::lock_guard<std::mutex> lock(mu_);
    pending_ -= static_cast<int>(jobs_.size());
    jobs_.clear();
    done_.clear();
  }
  // The worker notices by itself: it looks at the generation every tenth of a second while it waits.
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
    if (job.kind == Job::Kind::StopDebate) {  // nothing to report back
      hal::HttpResponse ignored;
      http(job.generation, "POST", "/debate/" + job.id + "/stop", "", "", 3000, 4096, &ignored);
      std::lock_guard<std::mutex> lock(mu_);
      --pending_;
      continue;
    }
    BrainEvent event = job.kind == Job::Kind::Ask      ? doAsk(job)
                       : job.kind == Job::Kind::Debate ? doDebate(job)
                       : job.kind == Job::Kind::Hear   ? doHear(job)
                                                       : doDebateLine(job);
    std::lock_guard<std::mutex> lock(mu_);
    --pending_;
    if (job.generation == generation_.load()) done_.push_back(std::move(event));  // else: cancelled meanwhile
  }
}

std::string BrainClient::fetchClip(int generation, const std::string& token, std::vector<uint8_t>* wav) {
  hal::HttpResponse clip;
  std::string error = http(generation, "GET", "/clip/" + token, "", "", kClipMs, kMaxClipBytes, &clip);
  if (!error.empty()) return error;
  if (clip.status != 200) return "bad-clip";
  wav->assign(clip.body.begin(), clip.body.end());
  hal::WavInfo info;
  if (!hal::parseWav(wav->data(), wav->size(), &info) || !hal::playableClip(info)) return "bad-clip";
  return "";
}

BrainEvent BrainClient::doAsk(const Job& job) {
  BrainEvent event;
  hal::HttpResponse response;
  std::string error = http(job.generation, "POST", "/ask?who=" + job.who, "text/plain", job.body, kAskMs, 4096, &response);
  if (!error.empty()) return failed(error);
  event.reply = hal::parseBrainReply(response);
  if (!event.reply.ok) return failed(event.reply.error);
  if (event.reply.text.empty() || event.reply.clip.empty()) return failed("bad-reply");
  if (job.who == "zero") {  // his own line: the audio is needed here
    error = fetchClip(job.generation, event.reply.clip, &event.wav);
    if (!error.empty()) return failed(error);
  }
  event.reply.who = job.who;
  event.kind = BrainEvent::Kind::Answer;
  return event;
}

BrainEvent BrainClient::doHear(const Job& job) {
  BrainEvent event;
  hal::HttpResponse response;
  std::string error = http(job.generation, "POST", "/hear", "audio/wav", job.body, kAskMs, 4096, &response);
  if (!error.empty()) return failed(error);
  event.reply = hal::parseBrainReply(response);
  if (!event.reply.ok) return failed(event.reply.error);
  if (!event.reply.hasHeard) return failed("bad-reply");
  event.kind = BrainEvent::Kind::Heard;
  return event;
}

BrainEvent BrainClient::doDebate(const Job& job) {
  BrainEvent event;
  hal::HttpResponse response;
  std::string error = http(job.generation, "POST", "/debate?turns=" + std::to_string(job.n), "text/plain", job.body,
                           kAskMs, 4096, &response);
  if (!error.empty()) return failed(error);
  event.reply = hal::parseBrainReply(response);
  if (!event.reply.ok) return failed(event.reply.error);
  if (event.reply.debate.empty()) return failed("bad-reply");
  event.kind = BrainEvent::Kind::DebateStarted;
  return event;
}

BrainEvent BrainClient::doDebateLine(const Job& job) {
  BrainEvent event;
  const auto giveUp = Clock::now() + std::chrono::milliseconds(kLineMs);
  const int generation = job.generation;
  while (true) {
    hal::HttpResponse response;
    // The brain holds the request for up to 20 s while the line is being written.
    std::string error =
        http(generation, "GET", "/debate/" + job.id + "/" + std::to_string(job.n), "", "", 25000, 4096, &response);
    if (!error.empty()) return failed(error);
    event.reply = hal::parseBrainReply(response);
    if (!event.reply.ok) return failed(event.reply.error);
    if (event.reply.end) {
      event.kind = BrainEvent::Kind::DebateOver;
      return event;
    }
    if (!event.reply.wait) break;
    if (Clock::now() >= giveUp || generation != generation_.load()) return failed("timeout");
  }
  if (event.reply.text.empty() || event.reply.clip.empty() || event.reply.who.empty()) return failed("bad-reply");
  if (event.reply.who == "zero") {
    std::string error = fetchClip(generation, event.reply.clip, &event.wav);
    if (!error.empty()) return failed(error);
  }
  event.kind = BrainEvent::Kind::Line;
  return event;
}

std::string BrainClient::http(int generation, const std::string& method, const std::string& path,
                              const std::string& contentType, const std::string& body, int timeoutMs, size_t maxBytes,
                              hal::HttpResponse* out) {
  const std::string request = hal::httpRequest(method, host_, port_, path, key_, contentType, body);
  if (request.empty()) return "bad-request";
  // Cancelled (or the program is closing)? Asked before each step and every tenth of a second
  // while waiting, so a question given up on is dropped wherever it had got to.
  const auto cancelled = [&] { return generation != generation_.load() || !running_; };
  if (cancelled()) return "cancelled";

  addrinfo hints{};
  hints.ai_family = AF_INET;  // the desk HAL is told this address too, and he speaks IPv4
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* found = nullptr;
  if (getaddrinfo(host_.c_str(), std::to_string(port_).c_str(), &hints, &found) != 0 || !found) return "offline";
  char dotted[INET_ADDRSTRLEN] = "";
  inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(found->ai_addr)->sin_addr, dotted, sizeof dotted);

  int fd = cancelled() ? -1 : socket(AF_INET, SOCK_STREAM, 0);  // (the name lookup above can take a while)
  if (fd < 0) {
    freeaddrinfo(found);
    return cancelled() ? "cancelled" : "offline";
  }
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
  std::string error;
  const auto fail = [&](const char* why) {
    error = why;
    return false;
  };
  // Wait until the socket is ready for `events`, within `ms`.
  const auto ready = [&](short events, int ms) {
    const auto until = Clock::now() + std::chrono::milliseconds(ms);
    while (true) {
      if (cancelled()) return fail("cancelled");
      pollfd p{fd, events, 0};
      const int left = msLeft(until);
      int r = ::poll(&p, 1, left < 100 ? left : 100);
      if (r > 0) return (p.revents & (POLLERR | POLLNVAL)) ? fail("offline") : true;
      if (r < 0 && errno != EINTR) return fail("offline");  // (a signal just means: look again)
      if (msLeft(until) == 0) return fail("timeout");
    }
  };

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
  close(fd);
  if (cancelled()) return "cancelled";
  if (!ok) return error.empty() ? "offline" : error;
  if (!hal::parseHttpResponse(raw, out)) return "bad-reply";
  std::lock_guard<std::mutex> lock(mu_);
  address_ = dotted;
  return "";
}
