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
  enum class Kind {
    Answer,         // a question's answer: `reply` (text, clip, show), and `wav` when it is HAL Zero's
    DebateStarted,  // an argument has begun: `reply.debate` is its id
    Line,           // the next line of an argument: `reply` (who, text, clip, last), `wav` for HAL Zero's
    DebateOver,     // there are no more lines
    Heard,          // what was said in a recording: `reply.heard` (may be empty)
    Failed,         // `error`: offline, timeout, bad-key, no-model, bad-reply, bad-clip, ...
  } kind = Kind::Failed;
  hal::BrainReply reply;
  std::vector<uint8_t> wav;  // a 16-bit PCM WAV
  std::string error;
};

class BrainClient {
 public:
  static constexpr int kConnectMs = 1500;
  static constexpr int kAskMs = 30000;
  static constexpr int kClipMs = 10000;
  static constexpr int kLineMs = 60000;  // one line of an argument, however many times the brain says "wait"
  static constexpr size_t kMaxClipBytes = 1536 * 1024;

  ~BrainClient() { stop(); }
  // `rememberFile`: where the brain's last working address is kept, for when its name won't
  // resolve ("" keeps none).
  void start(const std::string& host, int port, const std::string& key, const std::string& rememberFile = "");
  void stop();
  bool started() const { return running_.load(); }

  // Ask `who` (a cast id: "zero", "desk", "nano"...); `body` is facts, a blank line, the question
  // (hal::askBody). The answer arrives through poll(). HAL Zero's comes with its audio already
  // downloaded; another voiced HAL downloads his own (see hal_livefeed.h); one with no voice has none.
  void ask(const std::string& who, const std::string& body);
  // Start an argument about the topic in `body`; then ask for its lines one at a time, from 0.
  void debate(const std::string& body, int turns);
  // The same for a chat nobody asked for: `body` is the facts (and a subject, or none: the brain
  // then picks one), `first` who opens ("zero" or "desk"). Its lines are collected the same way.
  void chat(const std::string& body, int turns, const std::string& first);
  // A talk among the HALs named in `cast` ("zero,desk,nano": hal::castQuery): `kind` is argue,
  // chat or roll (a roll call: one line each). The brain decides how long it runs and who opens.
  void talk(const std::string& kind, const std::string& cast, const std::string& body);
  void debateLine(const std::string& id, int n);  // waits while the brain is still writing it (60 s at most)
  void stopDebate(const std::string& id);         // tell the brain to stop writing; no event comes back
  void hear(std::vector<uint8_t> wav);            // a 16 kHz mono recording: what was said?
  void cancel();                             // drop whatever is in flight or waiting
  bool busy() const { return pending_.load() > 0; }
  void poll(std::vector<BrainEvent>* out);   // never blocks
  std::string address();                     // the brain's IPv4 address once it has answered, else ""

 private:
  struct Job {
    enum class Kind { Ask, Debate, DebateLine, StopDebate, Hear } kind = Kind::Ask;
    int generation = 0;
    std::string who, body, id;  // body also carries a recording's bytes (Hear)
    std::string extra;          // Debate: more of the request's query ("&kind=chat&first=desk")
    int n = 0;  // DebateLine: which line; Debate: how many turns (0: the brain decides)
  };
  void push(Job job);
  void run();
  BrainEvent doAsk(const Job& job);
  BrainEvent doDebate(const Job& job);
  BrainEvent doDebateLine(const Job& job);
  BrainEvent doHear(const Job& job);
  // Fills `event.wav` from /clip/<token>; returns "" or an error word.
  std::string fetchClip(int generation, const std::string& token, std::vector<uint8_t>* wav);
  // One request for the job of that `generation`; returns "" and fills `out`, or an error word
  // ("cancelled" within a tenth of a second of cancel() or stop(), wherever it had got to).
  std::string http(int generation, const std::string& method, const std::string& path, const std::string& contentType,
                   const std::string& body, int timeoutMs, size_t maxBytes, hal::HttpResponse* out);

  std::string host_, key_;
  int port_ = 0;
  std::thread worker_;
  std::atomic<bool> running_{false};
  std::atomic<int> generation_{0};  // bumped by cancel(): older results are dropped
  std::atomic<int> pending_{0};
  std::mutex mu_;
  std::condition_variable wake_;
  std::deque<Job> jobs_;
  std::deque<BrainEvent> done_;
  std::string address_;
  std::string rememberFile_, remembered_;  // the last address that worked, kept across restarts (worker thread only)
  bool byMemory_ = false;                  // the name isn't resolving just now (for the log, once each way)
};
