# AI Brain: Typed Chat with HAL Zero Implementation Plan (v0.6, part 2)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Type a question on HAL Zero and hear him answer it with the AI brain: first in the
simulator against the real brain, then on the Cardputer. The pure logic for everything that
follows (the desk HAL's clips, arguments, speaking) lands here too, tested.

**Architecture:** Pure, tested units in `lib/hal_core` (link v3 messages, live conversations in
`Conductor`, a text box, a key map, HTTP request/reply text, the facts block, settings). HAL
Zero's glue gains a worker-thread `BrainClient`, an ask box and playing a clip from memory. A
chat answer is a live conversation of one line, so tap-to-interrupt and the idle rules need no
new code.

**Tech Stack:** C++17, PlatformIO, Unity; SDL2 and POSIX sockets on HAL Zero; the brain server
from part 1.

**Spec:** `docs/superpowers/specs/2026-11-11-ai-brain-design.md` (sections 1, 7, 8, 9).

## Global Constraints

- Tests: `pio test -e native` (194 existing stay green). Builds: `~/.platformio/penv/bin/pio run -e cores3|sim`,
  `tools/cardputer.sh build` (the sim and Cardputer builds clear each other: rebuild before use).
- Link lines stay ≤ 128 bytes and the parser stays strict: anything it doesn't know is `Invalid`.
  A v2 peer must keep working: v3 messages are only sent to a peer whose `HELLO` said 3 or more.
- Limits, exactly: question ≤ 120 characters (the text box); reply text ≤ 150 printable ASCII
  characters; clip token 16–32 lower-case hex over the link, exactly 32 from the brain; FETCH
  address a private IPv4 (10/8, 172.16/12, 192.168/16), port 1024–65535; live conversations ≤ 99
  lines; wait for the next live line ≤ 30 s; brain timeouts: connect 1.5 s, ask 30 s.
- HAL Zero's eye, keys and link pings never wait on the network: the brain is only talked to
  from `BrainClient`'s worker thread.
- `brain_key` is never logged or drawn. The CoreS3 card and `wifi_password` rules stand.
- New spoken lines (the "brain offline" set) are written for David to approve before rendering.
- The repo is public: `docs/SETUP.md` gains pairing and chat in the task that makes them work.
- Commits end with `Co-Authored-By: Claude Fable 5.2 <noreply@anthropic.com>`; push after each task.

## Review Focus

1. **The brain goes away mid-question** (computer asleep, Wi-Fi drop, wrong key, 503): HAL Zero
   says his offline line within the timeout and takes the next question. Owner: Task 5
   (`BrainClient` timeouts; sim check with the brain stopped) and Task 3 (`test_reply_errors`).
2. **A reply that isn't what the spec promises** (no `text`, 400 characters, control or
   non-ASCII bytes, a `clip` that isn't 32 hex, a huge `ms`, a body without line ends):
   rejected whole, never shown or fetched. Owner: Task 3 (`test_reply_rejects`).
3. **A live conversation that stalls** (the next line never arrives, or arrives after an
   interrupt): ends by itself after 30 s; a late `append` is refused. Owner: Task 2
   (`test_live_times_out`, `test_append_after_end`).
4. **Hostile link messages** (`FETCH` to a public address or port 80, an over-long token, a
   `SENSORS` line with a key twice): `Invalid`. Owner: Task 1 (`test_fetch_rejects`,
   `test_sensors_rejects`).
5. **Typing edge cases** (121st character, Backspace on empty, a key with no character, Shift
   held across keys, Space while the box is open): the box never overflows and Space types a
   space instead of tapping HAL. Owner: Task 3 (`test_textbox_*`, `test_keymap_*`), Task 5.

---

### Task 1: Link v3 messages (`hal_link`)

**Files:** modify `lib/hal_core/src/hal_link.{h,cpp}`, `test/test_link/test_main.cpp`.

**Interfaces — produces:**
- `kLinkVersion = 3`; `MsgType::Fetch, Ready, Sense, Sensors`.
- `Message` gains `std::string host; int port = 0; std::string token; PeerSensors sensors;`.
- `struct PeerSensors { int light = -1; int prox = 0; int tilt = 0; int bat = -1; bool chg = false; int temp = 0; };`
  (light −1..100, prox 0..65535, tilt 0..180, bat −1..100, temp −40..125).
- `bool isPrivateIpv4(const std::string& dotted);`
- Wire forms: `FETCH <n> <ipv4> <port> <token>` · `READY <n>` · `SENSE` ·
  `SENSORS light=<v> prox=<v> tilt=<v> bat=<v> chg=<0|1> temp=<v>` (all six keys, any order, each once).

- [ ] **Step 1: failing tests:** `test_v3_round_trip` (each new message formats and parses back,
  e.g. `FETCH 2 192.168.1.20 9100 0123456789abcdef0123456789abcdef`,
  `SENSORS light=67 prox=19 tilt=3 bat=37 chg=1 temp=42`, negatives `light=-1 bat=-1 temp=-5`);
  `test_private_ipv4` (true: `10.0.0.1`, `172.16.0.14`, `172.31.255.254`, `192.168.1.20`; false:
  `8.8.8.8`, `172.32.0.1`, `192.169.1.1`, `127.0.0.1`, `169.254.1.1`, `10.0.0`, `10.0.0.256`,
  `010.0.0.1`, `10.0.0.1.5`, `a.b.c.d`, empty); `test_fetch_rejects` (public address, port 1023
  and 65536, token of 15 and 33 characters, upper-case hex, `n` 0 and 100, a missing or extra
  word); `test_sensors_rejects` (a key missing, twice, unknown, out of range, `chg=2`);
  `test_longest_v3_lines_fit` (the longest FETCH and SENSORS are ≤ 128 bytes);
  `test_hello_three` (`HELLO NEW 3` parses; `kLinkVersion == 3`). Update the test that pinned version 2.
- [ ] **Step 2:** fail. **Step 3:** implement. **Step 4:** `pio test -e native` passes.
- [ ] **Step 5:** commit `Link v3: FETCH, READY, SENSE and SENSORS`.

### Task 2: Live conversations (`Conductor`)

**Files:** modify `lib/hal_core/src/hal_conductor.{h,cpp}`, `test/test_conductor/test_main.cpp`.

**Interfaces — produces:**
- `static constexpr uint32_t kWaitLineMs = 30000;`
- `void startLive(uint32_t nowMs);` — a conversation with id `live` and no lines yet.
- `int append(Speaker who, const std::string& text);` — the new line's number (1-based); 0 when
  the conductor isn't live, has been closed or ended, or already has 99 lines.
- `void close();` — no more lines will come; it ends `done` after the last one.
- `bool live() const;`
- Unchanged: `tick`, `onRemote`, `onLocalDone`, `onDisconnected`, `interrupt`, and every
  scripted-conversation test.

- [ ] **Step 1: failing tests:**
  - `test_live_one_line` — `startLive(1000)`; `tick(1100)` does nothing; `append(New, "Hi")` is 1;
    `close()`; `tick(1200)` → `playLocal`, `localLine == 1` (the first line doesn't wait for the
    gap); `onLocalDone(5000)` → `finished`, reason `done`, and an `END done` to send.
  - `test_live_waits_for_the_next_line` — after line 1 ends at 5000 with nothing appended and not
    closed: not finished; `append(Old, "…")` at 5100 is 2; `tick(5399)` nothing; `tick(5400)` →
    `SAY live 2` (the 400 ms gap counts from the end of line 1); a line appended at 9000 instead
    is said on the next tick.
  - `test_live_old_line_flow` — `SAY live 2` → `PLAYING 2 3000` → `DONE 2` → waits again; `close()`
    → `tick` finishes `done`.
  - `test_live_times_out` — waiting 30 000 ms with no line and no close: `finished`, reason
    `timeout`, `gloat == false`, an `END timeout` to send.
  - `test_append_after_end` — after `interrupt`, after `close()`, and on a conductor that was
    never started: `append` is 0 and nothing changes.
  - `test_live_is_capped` — the 100th `append` is 0.
  - `test_live_interrupt_and_offline` — `interrupt` mid-line: `END interrupted`; `onDisconnected`
    while an Old line is awaited: reason `offline`, `gloat == true`.
  - `test_scripted_is_not_live` — after `start(dialogue)`, `live()` is false and `append` is 0.
- [ ] **Step 2:** fail. **Step 3:** implement with one new state, `WaitLine`, and separate
  "earliest next line" and "give up" times. **Step 4:** pass.
- [ ] **Step 5:** commit `Conductor: live conversations whose lines arrive as they go`.

### Task 3: Typing, HTTP text and facts

**Files:** create `lib/hal_core/src/hal_textbox.h`, `hal_keymap.{h,cpp}`, `hal_http.{h,cpp}`,
`hal_facts.{h,cpp}`, `test/test_chat/test_main.cpp`.

**Interfaces — produces:**
- `class TextBox { public: explicit TextBox(size_t max = 120); bool insert(char c); bool backspace(); void clear(); const std::string& text() const; bool empty() const; };`
  (`insert` takes printable ASCII 32–126 only and refuses when full; a leading space is refused).
- `char keyToChar(int linuxKeyCode, bool shift);` — US layout for the Linux input codes of
  letters, digits, Space and `- = [ ] ; ' , . / \` `` ` ``; 0 for anything else.
- `std::string httpRequest(const std::string& method, const std::string& host, int port, const std::string& path, const std::string& key, const std::string& contentType, const std::string& body);`
  (HTTP/1.0, `Host`, `Content-Length` always on POST, `X-HAL-Key` only when `key` isn't empty,
  `Connection: close`; `host`, `path`, `key` and `contentType` containing CR, LF or a space → empty string).
- `struct HttpResponse { int status = 0; std::string contentType; std::string body; };`
  `bool parseHttpResponse(const std::string& raw, HttpResponse* out);` (status line, headers to the
  blank line, the rest is the body; honours `Content-Length` when present; false if malformed).
- `struct BrainReply { bool ok = false; std::string error; std::string text, clip, show, heard, who, debate; int ms = 0; bool last = false; bool wait = false; bool end = false; };`
  `BrainReply parseBrainReply(const HttpResponse& r);` — 200: fields validated (text 1–150
  printable ASCII; clip exactly 32 hex; show `[a-z]{1,16}`; ms 0–600000; who `zero|desk`; debate
  16 hex; last `0|1`); any invalid field makes the whole reply `!ok` with error `bad-reply`.
  202 → `wait`; 204 → `end`; other statuses → `!ok` with the `error=` code (or `http-<status>`).
- `std::string factsFromReadings(const std::vector<Reading>& readings);` — one `name: value` line
  per valid reading, name in lower case (`processor: 8 %`); invalid readings are left out.
- `std::string deskFacts(const PeerStatus* status, const PeerSensors* sensors);` — the desk HAL's
  lines (`uptime`, `free memory`, `lines spoken`, `mode`, `wifi`, `firmware`, `light`, `battery`,
  `tilt`, `chip temperature`); either pointer may be null.

- [ ] **Step 1: failing tests:** `test_textbox_types_and_erases`, `test_textbox_stops_at_120`,
  `test_textbox_refuses_unprintable_and_leading_space`; `test_keymap_letters_digits_space`
  (code 30 → `a`, with shift `A`; 2 → `1`, shift `!`; 57 → space), `test_keymap_punctuation`
  (53 → `/`, shift `?`; 40 → `'`, shift `"`), `test_keymap_unknown_is_zero` (389, 383, 28, 14, 1);
  `test_http_request` (exact bytes for a POST with a key and a GET without),
  `test_http_request_refuses_injection` (CR/LF or a space in host, path or key → empty);
  `test_http_response` (status, content type, body; `Content-Length` shorter than the body
  truncates; longer → false; no blank line → false; status 99 or 600 → false);
  `test_reply_ok` (ask, hear, debate start, debate line with `last=1`), `test_reply_wait_and_end`,
  `test_reply_errors` (401 `error=bad-key` → error `bad-key`; 503 with no body → `http-503`),
  `test_reply_rejects` (each invalid field listed above; a reply with `text` but no `clip`);
  `test_facts_from_readings`, `test_desk_facts` (with both, one and neither pointer).
- [ ] **Step 2:** fail. **Step 3:** implement. **Step 4:** pass.
- [ ] **Step 5:** commit `Typing, brain requests and replies, and the facts HAL Zero sends`.

### Task 4: Settings

**Files:** modify `lib/hal_core/src/hal_config.{h,cpp}`, `test/test_config/test_main.cpp`,
`tools/cardputer/config.txt` (commented examples).

**Interfaces — produces:** `Config` gains `bool brain = false; std::string brainHost; int brainPort = 9100; std::string brainKey;`
from `brain=on|off`, `brain_host`, `brain_port` (1024–65535), `brain_key` (exactly 32 lower-case
hex). `bool brainReady(const Config&)` — on, with a host and a valid key.

- [ ] **Step 1: failing tests:** `test_brain_settings` (all four parse; defaults off/9100);
  `test_brain_bad_values_warn` (a key of 31 characters or with upper case, port 80, a host with
  a space: default kept and a warning naming the key, never its value);
  `test_brain_ready` (on without a key or host is not ready).
- [ ] **Step 2:** fail. **Step 3:** implement. **Step 4:** pass. **Step 5:** commit `Settings for the AI brain`.

### Task 5: Typed chat in the simulator

**Files:** create `src/linux/brain_client.{h,cpp}`; modify `src/linux/main.cpp`,
`src/linux/sdl_io.{h,cpp}`, `src/linux/fb_io.{h,cpp}`, `src/linux/sdl_audio.{h,cpp}`,
`src/linux/hud.{h,cpp}`; create `content/new/lines/brain_offline.txt` (drafts for David).

**Interfaces — consumes** Tasks 1–4. **Produces:**
- `KeyInput` gains `std::string typed` (characters typed since the last poll) from SDL text
  input and, on the Cardputer, from `keyToChar` with Shift tracked.
- `SdlAudio::playBytes(const std::vector<uint8_t>& wav) -> bool`.
- `BrainClient`: `start(host, port, key)`, `stop()`, `ask(who, facts, question)`,
  `fetch(token)`, `cancel()`, `poll(std::vector<BrainEvent>*)` (never blocks), `address()` (the
  brain's IPv4 once resolved). `BrainEvent { enum Kind { Reply, Clip, Failed } kind; BrainReply reply; std::vector<uint8_t> wav; std::string error; }`.
- Ask box: Enter (eye showing, `brainReady`) opens `> _` in place of the command strip; typing
  edits; Enter sends; Esc cancels. While it is open Space is a character, not HAL's finger.
- HUD: `BRAIN THINKING` while waiting; `BRAIN OFFLINE` for 5 s after a failure.
- An answer plays as a live conversation of one line (`startLive`, `append(New, text)`, `close`),
  with the subtitle and, when the reply has `show`, that screen once he starts speaking.

- [ ] **Step 1:** implement the glue (no unit tests: it is I/O; the logic it calls is tested).
- [ ] **Step 2:** all tests and the three builds pass.
- [ ] **Step 3: simulator check against the real brain** (`tools/brain.sh start`, a temporary
  `--root` whose config has `brain=on`, `brain_host=127.0.0.1`): a typed question is answered
  aloud with its subtitle; "show me your monitor wall" brings the wall up; Space during the
  answer stops it; with the brain stopped the offline path runs within 2 s and the next question
  works once it is back. Screenshots (`--shots`) of the ask box and the thinking HUD for David.
- [ ] **Step 4:** commit `HAL Zero: typed chat with the AI brain (simulator)`.

### Task 6: On the Cardputer

- [ ] David approves the offline lines; render and push them (dry run first: zero deletions).
- [ ] Key check with David: `evtest` on the Cardputer with HAL Zero stopped, David pressing
  Enter, Backspace, Shift, Fn, Sym, Ctrl, Alt, the digits and punctuation once each; fix
  `keyToChar` and `fb_io` to match what the keyboard sends; unknown keys stay logged.
- [ ] `tools/brain.sh pair` (David runs it or says go: it writes the key into HAL Zero's
  settings), restart both, typed chat on the device; brain stopped → offline line.
- [ ] `docs/SETUP.md` step 9 gains pairing and chatting on the Cardputer (and the firewall
  prompt on macOS); README; CHANGELOG. Commit `HAL Zero: typed chat on the Cardputer`; push.
