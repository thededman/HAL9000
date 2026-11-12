#pragma once
// HAL Zero's line to the AI brain on the computer (docs/SETUP.md, step 9). All the
// network happens on a worker thread: the main loop hands over a question and polls
// for the answer, so the eye, the keys and the link to the desk HAL never wait.
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "hal_http.h"

struct BrainEvent {
  enum class Kind { Answer, Failed } kind = Kind::Failed;
  hal::BrainReply reply;     // Answer: what he says (checked), and a screen to show
  std::vector<uint8_t> wav;  // Answer: its audio, a 16-bit PCM WAV
  std::string error;         // Failed: offline, timeout, bad-key, no-model, bad-reply, bad-clip, ...
};

class BrainClient {
 public:
  static constexpr int kConnectMs = 1500;
  static constexpr int kAskMs = 30000;
  static constexpr int kClipMs = 10000;
  static constexpr size_t kMaxClipBytes = 1536 * 1024;

  ~BrainClient() { stop(); }
  void start(const std::string& host, int port, const std::string& key);
  void stop();
  bool started() const { return running_.load(); }

  // Ask `who` ("zero" or "desk"); `body` is facts, a blank line, the question (hal::askBody).
  // The answer arrives through poll(), with its audio already downloaded.
  void ask(const std::string& who, const std::string& body);
  void cancel();                             // drop whatever is in flight or waiting
  bool busy() const { return pending_.load() > 0; }
  void poll(std::vector<BrainEvent>* out);   // never blocks
  std::string address();                     // the brain's IPv4 address once it has answered, else ""

 private:
  struct Job {
    int generation;
    std::string who, body;
  };
  void run();
  BrainEvent doAsk(const Job& job);
  // One request; returns "" and fills `out`, or an error word.
  std::string http(const std::string& method, const std::string& path, const std::string& contentType,
                   const std::string& body, int timeoutMs, size_t maxBytes, hal::HttpResponse* out);

  std::string host_, key_;
  int port_ = 0;
  std::thread worker_;
  std::atomic<bool> running_{false};
  std::atomic<int> generation_{0};  // bumped by cancel(): older results are dropped
  std::atomic<int> pending_{0};
  std::atomic<int> fd_{-1};         // the socket in use, so cancel() can interrupt it
  std::mutex mu_;
  std::condition_variable wake_;
  std::deque<Job> jobs_;
  std::deque<BrainEvent> done_;
  std::string address_;
};
