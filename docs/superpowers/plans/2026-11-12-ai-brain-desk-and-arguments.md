# AI Brain: The Desk HAL's Lines and Live Arguments Implementation Plan (v0.6, part 3)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The desk HAL speaks lines written by the AI brain, so Dave can ask him questions from
the Cardputer and set the two HALs arguing live about anything.

**Architecture:** HAL Zero stays the only one who talks to the brain. For a desk line he sends
`FETCH n <address> <port> <token>` over the link; the desk HAL downloads the clip himself, says
`READY n`, and plays it on `SAY live n`. A small pure unit, `LiveFeed`, turns lines arriving from
the brain into `FETCH` messages and `Conductor::append` calls in the right order. An argument is a
live conversation of many lines; a question to the desk HAL is one of one line.

**Tech Stack:** C++17, PlatformIO, Unity; POSIX sockets on HAL Zero; `WiFiClient` and PSRAM on the
CoreS3; `tools/fake_old_hal.py` (asyncio) for testing without the CoreS3.

**Spec:** `docs/superpowers/specs/2026-11-11-ai-brain-design.md` (sections 1, 7, 8, 9).

## Global Constraints

- Tests: `pio test -e native` (229 existing stay green), brain tests (125). Builds: cores3, sim,
  cardputer (the sim and Cardputer builds clear each other).
- Link v3 messages only go to a peer whose `HELLO` said 3 or more; with older desk firmware HAL
  Zero says so on screen and nothing is sent.
- The desk HAL fetches only what `FETCH` names after the parser has accepted it (private IPv4,
  port ≥ 1024, hex token), at most 1 536 000 bytes, and only plays it if it parses as a 16-bit
  PCM WAV. A fetch is abandoned after 8 s without progress or when `END` arrives.
- CoreS3: clips live in PSRAM, two at most (playing and next); the eye keeps moving while a clip
  downloads (no blocking reads longer than about 5 ms per loop pass); `Audio` never frees a buffer
  the speaker is still reading.
- Timeouts, exactly: `READY` within 8 s of `FETCH`; `SENSORS` within 1 s of `SENSE` (the question
  goes ahead without them); the brain's long-poll is repeated while it answers `wait`, up to 60 s
  per line.
- A tap on either HAL still stops everything: HAL Zero cancels the brain request, tells the brain
  to stop the argument, and sends `END`.
- `brain_key` is never logged or sent over the link. No private details in the repo.
- `docs/SETUP.md` gains what changes for a newcomer in the task that makes it work.
- Commits end with `Co-Authored-By: Claude Fable 5.2 <noreply@anthropic.com>`; push after each task.

## Review Focus

1. **The desk HAL can't get the clip** (brain stopped between the answer and the fetch, wrong
   address, a truncated download): `MISSING n`, HAL Zero ends the conversation with a banner, and
   the next question works. Owner: Task 1 (`test_feed_missing`, `test_feed_ready_timeout`), Task 4.
2. **Replies out of order or late** (a `READY` for an earlier line, a line arriving after a tap):
   ignored; nothing is appended to a conversation that has ended. Owner: Task 1
   (`test_feed_ignores_stale`), with `Conductor::append` already refusing after an end.
3. **An old desk HAL** (link v2): no `FETCH` is sent; HAL Zero says the desk unit is too old to
   think. Owner: Task 3 (checked against `fake_old_hal.py --mode v2`).
4. **A hostile or broken clip on the CoreS3** (not a WAV, longer than its `Content-Length`, a
   server that trickles): never played, memory freed, `MISSING n`. Owner: Task 4.
5. **An interrupt mid-argument** (tap while a line is being written, fetched or spoken): both
   stop within a second, the brain stops writing, and nothing plays afterwards. Owner: Task 3
   (simulator against the fake desk HAL), Task 5 (the real pair).

---

### Task 1: `LiveFeed` (pure)

**Files:** create `lib/hal_core/src/hal_livefeed.{h,cpp}`, `test/test_livefeed/test_main.cpp`.

**Interfaces — produces:**

```cpp
struct FeedOut {
  bool send = false;       // send `msg` to the desk HAL (a FETCH)
  Message msg;
  bool append = false;     // the line is playable: Conductor::append(who, text)
  Speaker who = Speaker::New;
  std::string text;
  bool close = false;      // that was the last line: Conductor::close()
  bool failed = false;     // the conversation can't go on
  std::string reason;      // "missing", "timeout"
};

class LiveFeed {
 public:
  static constexpr uint32_t kReadyMs = 8000;
  void start();                                  // a new conversation; line numbers restart at 1
  bool busy() const;                             // a line is on its way to being playable
  // The next line from the brain. HAL Zero's needs its audio (onLocalClip); the desk HAL's is
  // fetched by him: this returns the FETCH to send.
  FeedOut onLine(Speaker who, const std::string& text, bool last, const std::string& host, int port,
                 const std::string& token, uint32_t nowMs);
  FeedOut onLocalClip();                         // HAL Zero's audio for the pending line is here
  FeedOut onRemote(const Message& m);            // READY n -> append; MISSING n -> failed
  FeedOut tick(uint32_t nowMs);                  // no READY within 8 s -> failed "timeout"
  int pendingLine() const;                       // its number (1-based), 0 if none
};
```

- [ ] **Step 1: failing tests:** `test_feed_local_line` (onLine(New) gives nothing to send;
  `busy()`; `onLocalClip()` → `append` with the text; not busy); `test_feed_desk_line` (onLine(Old,
  …, "192.168.1.20", 9100, token) → `send` with `FETCH 1 192.168.1.20 9100 <token>`; `READY 1` →
  `append` Old); `test_feed_numbers_count_up` (the third line fetches as `FETCH 3 …`);
  `test_feed_last_closes` (the last line's append carries `close`); `test_feed_missing` (`MISSING 1`
  → `failed`, reason `missing`); `test_feed_ready_timeout` (tick at 8000 ms nothing, 8001 →
  `failed` `timeout`); `test_feed_ignores_stale` (`READY 2` while line 3 is pending, `READY` with
  nothing pending, `onLocalClip` for a desk line, a second `onLine` while busy: all ignored);
  `test_feed_bad_fetch_details` (an address the link parser would refuse → `failed` at once, no
  send); `test_feed_restart` (after `start()` numbers begin at 1 and nothing is pending).
- [ ] **Step 2:** fail. **Step 3:** implement. **Step 4:** pass. **Step 5:** commit `LiveFeed: lines from the brain become FETCH and append, in order`.

### Task 2: The stand-in desk HAL speaks v3

**Files:** modify `tools/fake_old_hal.py`; create `tools/test_fake_old_hal.py`.

- Says `HELLO OLD 3` (mode `v2` says 2 and ignores the new messages; `old` still says 1).
- `FETCH n ip port token`: really downloads `http://ip:port/clip/token`, checks it is a WAV, keeps
  its length, answers `READY n` (or `MISSING n`). `SAY live n`: `PLAYING n <its real ms>`, then
  `DONE n`. `SENSE`: a sample `SENSORS` line. `END`: forgets its clips. Mode `nofetch` answers
  every `FETCH` with `MISSING`; mode `slowfetch` never answers one.
- [ ] Tests for its pure helpers (`parse_fetch`, `wav_ms`); then commit `fake_old_hal: link v3`.

### Task 3: HAL Zero asks the desk HAL and runs arguments (simulator)

**Files:** modify `src/linux/brain_client.{h,cpp}`, `src/linux/main.cpp`, `src/linux/hud.{h,cpp}`.

- `BrainClient` jobs: `ask(who, body)` (as now, but the audio is only downloaded for `zero`),
  `debate(body, turns)`, `debateLine(id, n)` (repeats while the brain says `wait`), `stopDebate(id)`.
  Events: `Answer`, `DebateStarted`, `Line` (reply, audio when it is HAL Zero's), `DebateOver`, `Failed`.
- Ask box: Tab switches between `ASK HAL ZERO` and `ASK THE DESK UNIT`. For the desk unit HAL
  Zero first sends `SENSE` (and `STATUS`), waits up to 1 s, and sends the desk HAL's facts.
- Menu: `Make them argue...` opens the box as `WHAT SHOULD THEY ARGUE ABOUT?`; 8 lines.
- Lines flow brain → `LiveFeed` → link/`Conductor`; subtitles as today, tagged `LIVE`.
- A tap: `Conductor::interrupt`, `BrainClient::cancel`, `stopDebate`.
- [ ] Simulator checks against `fake_old_hal.py` and the real brain: a question to the desk unit; an
  8-line argument; a tap mid-argument; `--mode v2`, `nofetch` and `slowfetch`; the brain stopped
  mid-argument. `--argue "TOPIC"` and `--ask-desk "QUESTION"` start them from the command line.
- [ ] Commit `HAL Zero: questions for the desk HAL and live arguments (simulator)`.

### Task 4: The desk HAL fetches and plays clips (CoreS3)

**Files:** create `src/clip_fetch.{h,cpp}`; modify `src/audio.{h,cpp}`, `src/main.cpp`.

- `ClipFetch`: `begin(host, port, token)`, `poll()` (a few ms per call) → `Idle | Fetching | Done |
  Failed`, `take(uint8_t** data, size_t* len)`; `abort()`. Buffer from PSRAM, sized by
  `Content-Length` (≤ 1 536 000), 8 s without progress fails.
- `Audio::playBuffer(uint8_t* data, size_t len)` takes ownership; the buffer that is playing is
  only freed once the speaker has stopped.
- `src/main.cpp`: `FETCH` → fetch → `READY`/`MISSING`; `SAY live n` → play; `SENSE` → `SENSORS`
  (fresh sensor read); `END` stops audio and aborts a fetch; `HELLO OLD 3`.
- [ ] Build; flash; with the simulator or the Cardputer: a desk answer, an argument, a tap; listen
  for stutter while a clip downloads during speech; heap and PSRAM before and after 20 lines.
- [ ] Commit `Desk HAL: fetches and speaks the AI brain's lines`.

### Task 5: The real pair

- [ ] Deploy HAL Zero; on the Cardputer: ask the desk unit, make them argue, tap to interrupt on
  each HAL; brain stopped mid-argument. Screenshots; `docs/SETUP.md`, README, CHANGELOG. Commit; push.
