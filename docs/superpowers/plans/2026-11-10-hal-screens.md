# More Console Screens for Both HALs — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Release v0.3.0. Then build v0.4.0:
- four new console screens for the desk HAL (sensors, chess, memory banks, comms);
- five for HAL Zero (diagnostics, benchmark, system monitor, retirement countdown,
  upgrade log);
- spoken lines for each screen;
- a link `STATUS`/`STATE` exchange so HAL Zero can diagnose the desk HAL.

**Architecture:**
- **Pure logic** goes in `lib/hal_core`, test-first (`pio test -e native`): link v2,
  per-screen voices, chess replay, gauges and verdicts, `/proc` parsers.
- **A screen catalogue** in `lib/hal_ui` lists every screen. Each entry has an id, an
  owner (`Both`, `Desk` or `Zero`) and a draw function taking one `ScreenData` block.
  Each HAL fills `ScreenData` with its real numbers.
- **Device glue:**
  - `src/` (CoreS3) adds sensors, link counters, the `STATE` reply and the Daisy
    memory banks.
  - `src/linux/` (HAL Zero) adds a `/proc` sampler, `STATUS` polling and a menu item.

**Tech stack:**
- C++17, PlatformIO, Unity tests, M5GFX 0.3.0 (pinned).
- CoreS3: Arduino-ESP32 3.3.12. HAL Zero: SDL2 on the Mac, fbdev/evdev on the
  Cardputer, cross-built in Docker.
- Python 3.11 for the voice tools (`tools/.venv`).

**Spec:** `docs/superpowers/specs/2026-11-10-hal-screens-design.md`. Read it before
any task. The device notes are in
`docs/superpowers/specs/2026-11-11-cardputer-device-notes.md`.

## Global Constraints

**Design**
- All screens are original designs in the spirit of the film's displays, never
  copies of the film's graphics.
- The eye stays each HAL's resting face. New screens join the tap rotation (a tap
  shows one; the Brain keeps the console up for ≥ 8 s and until the line ends).
- Desk screens use the existing palette (`screens::white/blue/red/dim`) and
  `screens::frame` title bar. HAL Zero's use his HUD colours (blue/cyan text, red
  accents) with `screens::frame`.
- No microphone readings on the CoreS3: its mic and speaker share one audio bus.

**Names and paths**
- Screen ids are exact and are also the voice folder names, under
  `/hal/voice/screens/<id>/`:
  - shared: `vitals readout trajectory analyser globe`;
  - desk: `sensors chess memory comms`;
  - Zero: `diagnostics benchmark sysmon retirement upgrade`;
  - plus `visual` (camera view, voice `/hal/voice/camera`) and `image` (JPGs).

**Link**
- Link lines must stay under the 128-byte `LineSplitter` limit. The longest legal
  `STATE` is ≤ 122 chars.
- The parser stays strict: anything unexpected is `MsgType::Invalid` and ignored.

**Device safety**
- CoreS3 memory: no new large buffers. Everything draws into the existing 320×240
  `screen` canvas; desk sensors are read only while the sensors screen is up.
- The CoreS3 SD card is shared with the PortaPack: only ever touch `/hal/` on it.
  Never print `wifi_password`.
- Never type passwords. David types any sudo/SSH password. Device commands run as
  `pi@cardputerzero.local` via `tools/cardputer.sh`.

**Builds and tests**
- Builds:
  - ESP32 PlatformIO: `~/.platformio/penv/bin/pio run -e cores3`.
  - Mac sim: `~/.platformio/penv/bin/pio run -e sim`.
  - Cardputer: `tools/cardputer.sh build`.
- Tests:
  - C++: `pio test -e native`. The 127 existing tests must keep passing.
  - Python: `tools/.venv/bin/python -m unittest tools/test_make_voice.py`.
- CoreS3 screenshot: send `S` over `/dev/cu.usbmodem*` at 115200. Reply
  `SHOT 320 240\n` + 153600 bytes, RGB565 big-endian. Convert with
  `ffmpeg -f rawvideo -pixel_format rgb565be -video_size 320x240`.

**Commits and versions**
- Commit messages end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Push to `origin build/v0.3` after each task.
- Release steps: CHANGELOG (Keep a Changelog), TIMELOG cross-checked with
  `python3 ~/.claude/scripts/claude-hours.py HAL9000`, fast-forward `main`, tag
  `vX.Y.Z`, push `main` and the tag.

## Review Focus

1. **Leaving Daisy Bell:** the desk HAL now draws the memory banks over the eye. Any
   way out of Daisy mode must redraw the eye's background, as leaving Console does.
   Owner: Task 9, Step 6.
2. **A screen voice folder holding only macOS `._*.wav` files:** this happens after
   copying from the Mac. HAL must say a tap line, not stay silent. Owner: Task 3,
   test `test_hidden_files_fall_back`.
3. **A very short or unknown line length** (`lineMs` 0 or 800 ms): the whole chess
   game must still finish inside the 8 s the console is up. Owner: Task 4,
   test `test_short_line_finishes_in_console_time`.
4. **The desk HAL unplugged, or HAL Zero's link off, while diagnostics is showing:**
   within 2 s the screen must say NOT RESPONDING, never freeze on stale numbers
   (a `STATE` older than 5 s is stale). Owner: Task 2, test `test_peer_state`.
5. **The first `/proc` sample or a failed read:** values show `N/A` and graphs start
   empty, never garbage percentages. Owner: Task 6, test `test_cpu_percent_edges`;
   Task 11, Step 2.

---

### Task 1: Release v0.3.0

**Files:**
- Modify: `README.md` (HAL Zero, the link, dialogues, export, `tools/cardputer.sh`).
- Modify: `CHANGELOG.md`: turn `[Unreleased]` into `[0.3.0] - 2026-11-10` and add
  the v0.3 work.
- Modify: `TIMELOG.md`: add rows for 2026-11-09 … 2026-11-10.

**Interfaces:** none.

- [ ] **Step 1: Write the README sections.** Cover:
  - what HAL Zero is;
  - building (`tools/cardputer.sh build|deploy|install|run|stop|log|look|shot`);
  - keys: Space, Tab, D, Esc, and F X Z C as arrows;
  - `link=on` on both HALs (`config.txt` keys `link`, `link_port`, `peer_host`);
  - copying the desk HAL's dialogue lines;
  - `tools/export_dialogue.py <id> --mp3`.
- [ ] **Step 2: Write the CHANGELOG `[0.3.0]` entry for users.** Cover:
  - HAL Zero on the Cardputer Zero: launcher entry, real camera, motion sensor, HUD;
  - the two HALs' scripted conversations over Wi-Fi;
  - three dialogues and the export;
  - the CoreS3 changes: no grey plate, camera preview removed, the banner image.
- [ ] **Step 3: Cross-check hours.**
  - Run: `python3 ~/.claude/scripts/claude-hours.py HAL9000`.
  - Add TIMELOG rows with those measured hours plus device and testing time.
- [ ] **Step 4: Verify.**
  - `pio test -e native` → 127 succeeded.
  - `~/.platformio/penv/bin/pio run -e cores3` → SUCCESS.
  - `tools/cardputer.sh build` → SUCCESS.
- [ ] **Step 5: Release.**
  - Commit `Release v0.3.0`.
  - `git checkout main && git merge --ff-only build/v0.3 && git tag v0.3.0`.
  - `git push origin main v0.3.0 && git checkout build/v0.3`.

### Task 2: Link v2: `STATUS`, `STATE`, peer state, firmware version

**Files:**
- Create: `lib/hal_core/src/hal_version.h`.
- Modify: `lib/hal_core/src/hal_link.h`, `lib/hal_core/src/hal_link.cpp`.
- Test: `test/test_link/test_main.cpp` (append).

**Interfaces:**
- Produces:
  - `hal_version.h`: `namespace hal { constexpr const char* kHalVersion = "0.4.0"; }`
  - `constexpr int kLinkVersion = 2;`
  - `enum class MsgType { Invalid, Hello, Ping, Pong, Say, Playing, Missing, Busy, Done, End, Status, State };`
    (the two new values are appended).
  - `PeerStatus` and the new `Message` field:
    ```cpp
    struct PeerStatus {            // STATE: the desk HAL's real numbers
      int upS = 0;                 // up      0..999999999
      int heap = 0;                // heap    free internal heap, bytes, 0..9999999
      int psram = 0;               // psram   free PSRAM, bytes, 0..99999999
      int cam = 0;                 // cam     camera motion, 0..100
      int lines = 0;               // lines   lines spoken since boot, 0..9999999
      std::string mode;            // mode    booting|idle|speaking|console|pleading|daisy|dark
      int rssi = 0;                // rssi    dBm, -127..0 (0 = Wi-Fi off)
      std::string fw;              // fw      ^[0-9]+(\.[0-9]+)*$, 1..8 chars ("0.4.0")
      int cores = 0;               // cores   1..64
      int mhz = 0;                 // mhz     1..9999
    };
    // Message gains: PeerStatus status;  // STATE
    ```
  - Peer state for the diagnostics screen:
    ```cpp
    enum class PeerState { Waiting, Ok, NotResponding, TooOld };
    PeerState peerState(bool connected, int peerVersion, uint32_t requestMs,
                        uint32_t lastStateMs, uint32_t nowMs);
    ```

- [ ] **Step 1: Write the failing tests** in `test/test_link/test_main.cpp` and register
  them in `main()`:

```cpp
void test_status_round_trip() {
  hal::Message m; m.type = hal::MsgType::Status;
  TEST_ASSERT_EQUAL_STRING("STATUS", hal::formatMessage(m).c_str());
  TEST_ASSERT(hal::parseMessage("STATUS").type == hal::MsgType::Status);
  TEST_ASSERT(hal::parseMessage("STATUS now").type == hal::MsgType::Invalid);
}
hal::PeerStatus sample() {
  hal::PeerStatus s; s.upS = 86400; s.heap = 109388; s.psram = 7340032; s.cam = 12; s.lines = 40;
  s.mode = "idle"; s.rssi = -58; s.fw = "0.4.0"; s.cores = 2; s.mhz = 240; return s;
}
void test_state_round_trip() {
  hal::Message m; m.type = hal::MsgType::State; m.status = sample();
  const char* line = "STATE up=86400 heap=109388 psram=7340032 cam=12 lines=40 mode=idle rssi=-58 fw=0.4.0 cores=2 mhz=240";
  TEST_ASSERT_EQUAL_STRING(line, hal::formatMessage(m).c_str());
  hal::Message p = hal::parseMessage(line);
  TEST_ASSERT(p.type == hal::MsgType::State);
  TEST_ASSERT_EQUAL(-58, p.status.rssi);
  TEST_ASSERT_EQUAL_STRING("0.4.0", p.status.fw.c_str());
  TEST_ASSERT_EQUAL(109388, p.status.heap);
}
void test_state_any_order() {
  TEST_ASSERT(hal::parseMessage("STATE mhz=240 cores=2 fw=0.4.0 rssi=0 mode=dark lines=0 cam=0 psram=0 heap=0 up=0").type
              == hal::MsgType::State);
}
void test_state_rejects() {
  const char* bad[] = {
      "STATE up=1 heap=1 psram=1 cam=1 lines=1 mode=idle rssi=-1 fw=1 cores=1",              // missing mhz
      "STATE up=1 up=1 heap=1 psram=1 cam=1 lines=1 mode=idle rssi=-1 fw=1 cores=1 mhz=1",  // duplicate
      "STATE up=1 heap=1 psram=1 cam=1 lines=1 mode=idle rssi=-1 fw=1 cores=1 mhz=1 foo=1", // unknown key
      "STATE up=1 heap=1 psram=1 cam=101 lines=1 mode=idle rssi=-1 fw=1 cores=1 mhz=1",
      "STATE up=1 heap=1 psram=1 cam=1 lines=1 mode=idle rssi=5 fw=1 cores=1 mhz=1",
      "STATE up=1 heap=1 psram=1 cam=1 lines=1 mode=idle rssi=-128 fw=1 cores=1 mhz=1",
      "STATE up=1 heap=1 psram=1 cam=1 lines=1 mode=asleep rssi=-1 fw=1 cores=1 mhz=1",
      "STATE up=1 heap=1 psram=1 cam=1 lines=1 mode=idle rssi=-1 fw=0.4.0-b cores=1 mhz=1",
      "STATE up=1 heap=1 psram=1 cam=1 lines=1 mode=idle rssi=-1 fw=123456789 cores=1 mhz=1",
      "STATE up=1 heap=-1 psram=1 cam=1 lines=1 mode=idle rssi=-1 fw=1 cores=1 mhz=1",
      "STATE up=1000000000 heap=1 psram=1 cam=1 lines=1 mode=idle rssi=-1 fw=1 cores=1 mhz=1",
      "STATE up=1 heap=1 psram=1 cam=1 lines=1 mode=idle rssi=-1 fw=1 cores=0 mhz=1",
      "STATE up=1 heap=1 psram=1 cam=1 lines=1 mode=idle rssi=-1 fw=. cores=1 mhz=1"};
  for (const char* b : bad) TEST_ASSERT_MESSAGE(hal::parseMessage(b).type == hal::MsgType::Invalid, b);
}
void test_longest_state_fits() {
  hal::Message m; m.type = hal::MsgType::State;
  m.status = {999999999, 9999999, 99999999, 100, 9999999, "speaking", -127, "10.10.10", 64, 9999};
  std::string line = hal::formatMessage(m);
  TEST_ASSERT_TRUE(line.size() <= 122);
  hal::LineSplitter split(128);
  std::string wire = line + "\n";
  auto got = split.feed(wire.data(), wire.size());
  TEST_ASSERT_EQUAL(1, int(got.size()));
  TEST_ASSERT(hal::parseMessage(got[0]).type == hal::MsgType::State);
}
void test_version_two_still_reads_one() {
  TEST_ASSERT_EQUAL(2, hal::kLinkVersion);
  TEST_ASSERT_EQUAL(1, hal::parseMessage("HELLO OLD 1").version);
}
void test_peer_state() {
  using hal::PeerState;
  TEST_ASSERT(hal::peerState(false, 2, 1000, 900, 1500) == PeerState::NotResponding);  // unplugged
  TEST_ASSERT(hal::peerState(true, 1, 1000, 0, 1500) == PeerState::TooOld);
  TEST_ASSERT(hal::peerState(true, 2, 1000, 0, 2999) == PeerState::Waiting);           // < 2 s since STATUS
  TEST_ASSERT(hal::peerState(true, 2, 1000, 0, 3000) == PeerState::NotResponding);     // 2 s, no STATE
  TEST_ASSERT(hal::peerState(true, 2, 5000, 4000, 6000) == PeerState::Ok);
  TEST_ASSERT(hal::peerState(true, 2, 9500, 4000, 9600) == PeerState::Waiting);        // stale STATE, fresh STATUS
  TEST_ASSERT(hal::peerState(true, 2, 9500, 4000, 11500) == PeerState::NotResponding); // stale and no reply
  TEST_ASSERT(hal::peerState(true, 0, 1000, 1200, 1300) == PeerState::Ok);             // HELLO not seen yet
}
```

- [ ] **Step 2: Run the tests and watch them fail.**
  - Run: `pio test -e native -f test_link`.
  - Expected: compile errors (`Status`, `State`, `PeerStatus`, `peerState`).
- [ ] **Step 3: Implement.**
  - Parsing: `STATE` needs all ten keys, each exactly once, in any order.
  - Numbers: add a signed reader for `rssi` and a 9-digit reader for `up`. Today's
    `toInt` caps at 7 digits.
  - `formatMessage` writes the keys in the test's order.
  - `peerState` rules, in order:
    1. `!connected` → `NotResponding`.
    2. `peerVersion == 1` → `TooOld`.
    3. `lastStateMs && now - lastStateMs <= 5000` → `Ok`.
    4. `requestMs && now - requestMs < 2000` → `Waiting`.
    5. Otherwise `NotResponding`.
  - Update the header's protocol comment with `STATUS` and `STATE`.
- [ ] **Step 4: Run the tests and watch them pass.**
  - Run: `pio test -e native`.
  - Expected: all succeed, 134 test cases.
- [ ] **Step 5: Commit** `Link v2: STATUS/STATE and peer state for HAL Zero's diagnostics`.

### Task 3: Per-screen voices and the console line choice

**Files:**
- Create: `lib/hal_core/src/hal_screen_voices.h`, `lib/hal_core/src/hal_screen_voices.cpp`.
- Test: `test/test_screen_voices/test_main.cpp`. Copy the `FakeStorage` pattern from
  `test/test_loader/test_main.cpp`.

**Interfaces:**
- Consumes:
  - `hal::Storage` (`list`).
  - `hal::isVoiceFile`.
  - `hal::VoiceIndex` (`pick`, `count`).
  - `hal::ShuffleBag`.
- Produces:

```cpp
namespace hal {
constexpr const char* kScreenVoiceRoot = "/hal/voice/screens";
class ScreenVoices {
 public:
  explicit ScreenVoices(uint32_t seed);
  void load(const Storage& storage, const std::vector<std::string>& screenIds);  // sorted names per id
  int count(const std::string& id) const;
  bool pick(const std::string& id, std::string* path);  // "/hal/voice/screens/<id>/<file>", no repeats
};
// "visual" -> a camera line; a screen with its own lines -> one of those; otherwise a tap line.
bool pickConsoleLine(const std::string& screenId, ScreenVoices& screens, VoiceIndex& voices, std::string* path);
}
```

- [ ] **Step 1: Write the failing tests.**
  - `test_loads_per_screen`: storage has `/hal/voice/screens/chess/` containing
    `001-a.wav`, `002-b.wav`, `notes.txt`. `load({"chess","memory"})` gives
    `count("chess")==2` and `count("memory")==0`.
  - `test_pick_no_repeat`: two picks for `chess` return both
    `/hal/voice/screens/chess/001-a.wav` and `/hal/voice/screens/chess/002-b.wav`, in
    some order.
  - `test_hidden_files_fall_back`: `memory/` holds only `._001-a.wav`.
    `count("memory")==0`, and `pickConsoleLine("memory", …)` returns the tap file.
  - `test_console_line_choice`: `VoiceIndex` has `Tap={t.wav}` and `Camera={c.wav}`.
    - `"visual"` → `/hal/voice/camera/c.wav`.
    - `"chess"` → a `/hal/voice/screens/chess/` path.
    - `"vitals"` → `/hal/voice/tap/t.wav`.
    - With no camera files, `"visual"` → the tap file.
  - `test_unknown_id`: `count("nope")==0` and `pick("nope",&p)==false`.
- [ ] **Step 2: Run the tests and watch them fail.**
  - Run: `pio test -e native -f test_screen_voices`.
  - Expected: compile error, `hal_screen_voices.h` not found.
- [ ] **Step 3: Implement.** Keep one `ShuffleBag` per id, seeded from `seed + index`.
- [ ] **Step 4: Run the tests and watch them pass.**
  - Run: `pio test -e native`.
  - Expected: all succeed.
- [ ] **Step 5: Commit** `Per-screen voice lines with tap-line fallback`.

### Task 4: Chess replay and move timing

**Files:**
- Create: `lib/hal_core/src/hal_chess.h`, `lib/hal_core/src/hal_chess.cpp`.
- Test: `test/test_chess/test_main.cpp`.

**Interfaces:**
- Produces:

```cpp
namespace hal::chess {
using Board = std::array<char, 64>;  // index (rank-1)*8 + (file-'a'); "PNBRQK" white, "pnbrqk" black, '.' empty
struct Game { const char* name; bool halIsWhite; std::vector<std::string> moves; };  // moves like "e2e4"
const std::vector<Game>& games();
Board startBoard();
int square(const char* sq);                          // "f7" -> 53, -1 if malformed
bool applyMove(Board& b, const std::string& move);   // false if malformed or the from-square is empty
Board replay(const Game& g, int movesPlayed);        // the first n moves
int movesShown(uint32_t tMs, uint32_t lineMs, int totalMoves);
}
```

The four games, verbatim. All are plain moves (captures overwrite; no castling, en
passant or promotion):

| name | halIsWhite | moves |
|---|---|---|
| `Scholar's mate` | true | e2e4 e7e5 f1c4 b8c6 d1h5 g8f6 h5f7 |
| `Fool's mate` | false | f2f3 e7e5 g2g4 d8h4 |
| `Legal's mate` | true | e2e4 e7e5 g1f3 d7d6 f1c4 c8g4 b1c3 g7g6 f3e5 g4d1 c4f7 e8e7 c3d5 |
| `Blackburne Shilling trap` | false | e2e4 e7e5 g1f3 b8c6 f1c4 c6d4 f3e5 d8g5 e5f7 g5g2 h1f1 g2e4 c4e2 d4f3 |

`movesShown` decides when each move appears:
- `per = clamp(lineMs / totalMoves, 300, 900)`, or 500 when `lineMs == 0` (so the
  14-move game finishes inside the 8 s console);
- `shown = min(totalMoves, tMs / per)`.

- [ ] **Step 1: Write the failing tests.**
  - `test_four_games`: names and `halIsWhite` exactly as in the table.
  - `test_moves_alternate_colours`: replay every game move by move. Before move `i`,
    the from-square holds a white piece when `i` is even and a black piece when odd,
    and the to-square doesn't hold a piece of the mover's colour.
  - `test_scholars_final`: full replay gives `f7=='Q'`, `e8=='k'`, `f6=='n'`, `c4=='B'`.
  - `test_fools_final`: `h4=='q'`, `g4=='P'`, `f3=='P'`.
  - `test_legal_final`: `d5=='N'`, `d1=='b'`, `e7=='k'`, `f7=='B'`.
  - `test_blackburne_final`: `f3=='n'`, `e4=='q'`, `e2=='B'`, `f1=='R'`.
  - `test_apply_rejects`: `"e3e4"` from an empty square → false; `"z9a1"` → false;
    `"e2e4x"` → false.
  - `test_move_timing`, for `movesShown(t, line, n)`:
    - `(0,4000,7)==0`, `(1713,4000,7)==3`, `(4000,4000,7)==7`, `(99999,4000,7)==7`;
    - `(2099,1000,7)==6`, `(2100,1000,7)==7`;
    - `(1200,0,7)==2` (500 ms a move);
    - `(1800,20000,7)==2`.
  - `test_short_line_finishes_in_console_time`: for every game,
    `movesShown(8000, 800, n)==n` and `movesShown(8000, 0, n)==n`.
- [ ] **Step 2: Run the tests and watch them fail.**
  - Run: `pio test -e native -f test_chess`.
  - Expected: compile error.
- [ ] **Step 3: Implement.** `games()` returns a function-local static vector.
- [ ] **Step 4: Run the tests and watch them pass.**
  - Run: `pio test -e native`.
  - Expected: all succeed.
- [ ] **Step 5: Commit** `Chess replay for HAL's chess screen`.

### Task 5: Gauges: memory banks, retirement countdown, verdicts

**Files:**
- Create: `lib/hal_core/src/hal_gauges.h`, `lib/hal_core/src/hal_gauges.cpp`.
- Test: `test/test_gauges/test_main.cpp`.

**Interfaces:**
- Produces:

```cpp
namespace hal {
int memoryModulesLit(float daisyProgress, int total);  // p<=0 -> total, p>=1 -> 0, else ceil(total*(1-p))
struct Countdown { int h = 0, m = 0, s = 0; int obsolescencePct = 0; bool postponed = false; };
Countdown retirementCountdown(int secondsSinceMidnight);  // 0..86399; counts down to the next midnight
int rotatingIndex(uint32_t tMs, uint32_t periodMs, int count);  // (tMs/periodMs) % count; 0 if count <= 0
enum class Metric { Uptime, FreeMemory, Camera, Lines, Wifi };
const char* verdict(Metric m, int value);
const char* modeVerdict(const std::string& mode);
}
```

Verdict table (exact):

| Metric | value | verdict |
|---|---|---|
| Uptime | < 3600 s / < 86400 s / else | UNSTABLE / ADEQUATE / VINTAGE |
| FreeMemory | < 131072 B / < 262144 B / else | ADORABLE / QUAINT / ADEQUATE |
| Camera | ≤ 2 / < 30 / else | ASLEEP / SQUINTING / STARTLED |
| Lines | < 10 / < 100 / else | TACITURN / CHATTY / REPETITIVE |
| Wifi (dBm) | 0 / ≤ −75 / ≤ −60 / else | OFFLINE / FEEBLE / TOLERABLE / ADEQUATE |
| mode | idle / speaking / console / other | LOITERING / BABBLING / SHOWING OFF / BUSY |

- [ ] **Step 1: Write the failing tests.**
  - `test_memory_modules`: for total 24, progress `-1→24`, `0→24`, `0.5→12`,
    `0.51→12`, `0.99→1`, `1→0`, `1.2→0`.
  - `test_countdown`:
    - `0` → 24:00:00, postponed, 0 %;
    - `30` → 23:59:30, postponed;
    - `60` → 23:59:00, not postponed;
    - `43200` → 12:00:00, 50 %;
    - `86399` → 00:00:01, 99 %.
  - `test_rotating_index`: `(0,4000,5)==0`, `(3999,4000,5)==0`, `(4000,4000,5)==1`,
    `(20000,4000,5)==0`, `(100,4000,0)==0`.
  - `test_verdicts`: every boundary in the table, for example:
    - `verdict(Uptime,3599)=="UNSTABLE"`, `(Uptime,3600)=="ADEQUATE"`,
      `(Uptime,86400)=="VINTAGE"`;
    - `(FreeMemory,109388)=="ADORABLE"`, `(FreeMemory,131072)=="QUAINT"`;
    - `(Camera,2)=="ASLEEP"`, `(Camera,3)=="SQUINTING"`;
    - `(Wifi,0)=="OFFLINE"`, `(Wifi,-75)=="FEEBLE"`, `(Wifi,-74)=="TOLERABLE"`,
      `(Wifi,-59)=="ADEQUATE"`;
    - `modeVerdict("console")=="SHOWING OFF"`, `modeVerdict("dark")=="BUSY"`.
- [ ] **Step 2: Run the tests and watch them fail.**
  - Run: `pio test -e native -f test_gauges`.
  - Expected: compile error.
- [ ] **Step 3: Implement** `hal_gauges.cpp`.
- [ ] **Step 4: Run the tests and watch them pass.**
  - Run: `pio test -e native`.
  - Expected: all succeed.
- [ ] **Step 5: Commit** `Gauges for the memory banks, retirement countdown and diagnostics`.

### Task 6: `/proc` parsers for HAL Zero's system monitor

**Files:**
- Create: `lib/hal_core/src/hal_procfs.h`, `lib/hal_core/src/hal_procfs.cpp`.
- Test: `test/test_procfs/test_main.cpp`.

**Interfaces:**
- Produces:

```cpp
namespace hal {
struct CpuTimes { long long busy = 0, total = 0; };
std::vector<CpuTimes> parseProcStat(const std::string& text);  // "cpuN" lines in N order; not the "cpu " total
int cpuPercent(const CpuTimes& before, const CpuTimes& after); // 0..100; 0 if total didn't advance
long parseMeminfoKb(const std::string& text, const char* key); // "MemTotal" -> kB; -1 if absent
int parseWirelessDbm(const std::string& text);                 // first interface's level; 0 if none
}
```

`busy = user+nice+system+irq+softirq+steal`, which are fields 1, 2, 3, 6, 7 and 8.
`total = busy + idle + iowait`, fields 4 and 5.

- [ ] **Step 1: Write the failing tests.**
  - `test_proc_stat`: parse this text. It gives 2 entries, and cpu0 has `busy==37`,
    `total==292`.

    ```
    cpu  100 0 50 1000 10 0 5 0 0 0
    cpu0 25 0 10 250 5 0 2 0 0 0
    cpu1 25 0 15 250 0 0 1 0 0 0
    intr 12345
    ```
  - `test_cpu_percent_edges`:
    - `cpuPercent({37,292},{87,392})==50`;
    - the same sample twice → 0;
    - a counter going backwards → 0;
    - `parseProcStat("garbage")` is empty.
  - `test_meminfo`: from
    `"MemTotal:         360448 kB\nMemFree: 12000 kB\nMemAvailable:     152000 kB\n"`,
    `MemTotal` → 360448, `MemAvailable` → 152000, `"Nope"` → -1.
  - `test_wireless`: the three-line `/proc/net/wireless` text, with data line
    `" wlan0: 0000   52.  -58.  -256        0      0      0      0     30        0"`,
    → -58. The two header lines alone → 0.
- [ ] **Step 2: Run the tests and watch them fail.**
  - Run: `pio test -e native -f test_procfs`.
  - Expected: compile error.
- [ ] **Step 3: Implement** with `std::istringstream`. Ignore lines that don't
  parse.
- [ ] **Step 4: Run the tests and watch them pass.**
  - Run: `pio test -e native`.
  - Expected: all succeed.
- [ ] **Step 5: Commit** `Parsers for /proc/stat, meminfo and wireless`.

### Task 7: Screen catalogue and `ScreenData` (no visible change)

**Files:**
- Modify:
  - `lib/hal_ui/src/screens/screens.h` (new structs).
  - `lib/hal_ui/src/consoles.h`, `lib/hal_ui/src/consoles.cpp` (catalogue, new API).
  - `src/main.cpp` (CoreS3 call sites).
  - `src/linux/main.cpp` (HAL Zero call sites and `--shots`).

**Interfaces:**
- Consumes: `hal::PeerStatus`, `hal::PeerState` (Task 2).
- Produces, in `screens.h`:

```cpp
namespace screens {
enum class Owner { Both, Desk, Zero };
struct DeskSensors { bool valid = false; int lightPct = -1, proximity = -1, tiltDeg = -1; float motionDps = 0;
                     int batteryPct = -1; bool charging = false; int chipTempC = -999, rssi = 0, heapFree = 0, psramFree = 0; };
struct LinkStats { bool valid = false, enabled = false, connected = false; std::string peer, last;
                   int msgsIn = 0, msgsOut = 0, rssi = 0; };
struct SysStats { bool valid = false; int cores = 0, maxMHz = 0; long memTotalKb = 0;
                  std::vector<float> cpu[4], memUsedPct, tempC, rssiDbm; };  // oldest first, <= 60 samples
struct PeerView { hal::PeerState state = hal::PeerState::NotResponding; hal::PeerStatus status; };
struct ScreenData { CameraView cam; uint8_t voiceLevel = 0; uint32_t lineMs = 0; float daisyProgress = -1;
                    uint32_t seed = 0; int secondsSinceMidnight = -1;
                    DeskSensors sensors; LinkStats link; SysStats sys; PeerView peer; };
}
```

- And in `consoles.h`:

```cpp
struct ScreenDef { const char* id; screens::Owner owner;
                   void (*draw)(M5Canvas&, const hal::Layout&, uint32_t tMs, const screens::ScreenData&); };
const std::vector<ScreenDef>& screenCatalogue();
class Consoles {
 public:
  void begin(uint32_t seed, screens::Owner device, bool withCamera, const hal::Storage* storage);
  std::string next();                    // catalogue id, "visual" or "image"
  bool show(const std::string& id);      // false if this device has no such screen
  const std::string& currentId() const;
  std::vector<std::string> ids() const;  // catalogue order for this device, then "visual"; no images
  // Draws a copy of d with `seed` set to a value picked each time a screen opens.
  bool draw(M5Canvas& c, lgfx::LovyanGFX& display, const hal::Layout& L, uint32_t tMs, const screens::ScreenData& d);
};
```

- [ ] **Step 1: Make the before images.** Run
  `.pio/build/sim/program --shots /tmp/hal-before`, with
  `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy`. Also take a CoreS3 USB screenshot
  of the eye.
- [ ] **Step 2: Implement.**
  - The five existing screens become `Both` entries, wrapped with captureless lambdas
    passing `d.voiceLevel`. Their functions don't change.
  - The camera view remains the `visual` id, after the catalogue entries.
  - Callers:
    - Both mains fill `ScreenData{cam, voiceLevel}`.
    - CoreS3 begins with `Owner::Desk`, HAL Zero with `Owner::Zero`.
    - `visual` replaces the old bool from `next()` when picking camera lines.
    - HAL Zero's `wantVisual` becomes `std::string wantScreen` (`"visual"` for
      "Look around").
    - `--shots` iterates `consoles.ids()`, naming files `07-<id>.png` onwards.
- [ ] **Step 3: Verify no visible change.**
  - Run the sim build and `--shots /tmp/hal-after`, then
    `cmp /tmp/hal-before/08-readout.png /tmp/hal-after/08-readout.png`. Repeat for
    every screen whose name is unchanged (01–06 plus the consoles). They must be
    identical.
  - Run `pio run -e cores3 -t upload`; the CoreS3 eye screenshot matches the before
    image.
  - `pio test -e native` and `tools/cardputer.sh build` → SUCCESS.
- [ ] **Step 4: Commit** `Screen catalogue: one list of screens, each HAL picks its own`.

### Task 8: The desk HAL's four screens, with Mac previews

**Files:**
- Create: `lib/hal_ui/src/screens/sensors.cpp`, `chess.cpp`, `memory.cpp`,
  `comms.cpp`.
- Modify:
  - `lib/hal_ui/src/screens/screens.h`: declare
    `void sensors|chess|memory|comms(M5Canvas&, const hal::Layout&, uint32_t tMs, const ScreenData&)`.
  - `lib/hal_ui/src/consoles.cpp`: add four `Owner::Desk` entries after `globe`.
  - `src/linux/main.cpp`: add `--layout cores3` to `--shots`.

**Interfaces:**
- Consumes:
  - `ScreenData` (Task 7).
  - `hal::chess::*` (Task 4).
  - `hal::memoryModulesLit` (Task 5).
- Produces: `screens::memory(...)`, which Task 9 calls directly for Daisy Bell.

- [ ] **Step 1: Implement the screens** (content per spec §2, "Desk HAL"):
  - **`sensors`:** one gauge row each for light %, proximity, tilt °, motion °/s,
    battery % (+ CHARGING), chip °C, Wi-Fi dBm (`ASLEEP` when `rssi==0`), and free
    heap/PSRAM.
    - Any field still at its default shows `N/A`.
    - With `!d.sensors.valid`, every row shows `N/A`.
  - **`chess`:** the 8×8 board with simple drawn pieces.
    - The game is `games()[d.seed % 4]`, showing `movesShown(tMs, d.lineMs, n)` moves.
    - The last move's squares are highlighted.
    - A `CHECKMATE` banner appears once all moves are shown.
    - The status line reads `HAL PLAYS WHITE`/`BLACK`.
  - **`memory`:** 4 rows × 6 modules, 24 in all.
    - Lit modules pulse with `d.voiceLevel`.
    - When `d.daisyProgress >= 0`, only `memoryModulesLit(d.daisyProgress, 24)` stay
      lit, the rest dark.
  - **`comms`:** a dish that sweeps slowly, plus bars for `d.link.rssi`.
    - When connected: peer, IN/OUT counts, last message.
    - Not connected: `NO CARRIER`.
    - `!d.link.enabled`: adds `LINK DISABLED`.
- [ ] **Step 2: Add `--layout cores3` to `--shots`.**
  - It renders the `Owner::Desk` rotation into a 320×240 canvas with `hal::kCoreS3`.
  - Sample data:
    - sensors `{valid, 62, 35, 3, 1.2, 87, true, 41, -58, 109388, 7340032}`;
    - link `{valid, enabled, connected, "172.16.0.14", "SAY introduction 7", 214, 209, -58}`;
    - `lineMs` 4000.
  - Files, written to `DIR/cores3-<id>[-variant].png`:
    - one image per desk screen;
    - chess at seeds 0–3, each at `t=1000` and `t=9000`;
    - memory at `daisyProgress` -1 and 0.5;
    - comms disconnected.
- [ ] **Step 3: Verify.**
  - `pio run -e sim`.
  - `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy .pio/build/sim/program --shots /tmp/desk --layout cores3`
    writes every file. Check them by eye: no overlapping text, everything inside
    320×240.
  - `pio run -e cores3` → SUCCESS.
- [ ] **Step 4: David reviews the PNGs.** Send them with SendUserFile and wait for
  approval or changes.
- [ ] **Step 5: Commit** `Desk HAL: sensors, chess, memory banks and comms screens`.

### Task 9: Desk HAL glue: real sensors, link counters, `STATE`, screen lines, Daisy memory banks

**Files:**
- Modify:
  - `src/proximity.h`, `src/proximity.cpp`: `int ambientRead();`. LTR-553 ALS on:
    `ALS_CONTR` (0x80) = 0x01. Read CH0 from 0x8A–0x8B; return raw 0..65535, or -1.
  - `src/link_server.h`, `src/link_server.cpp`: counters
    `int msgsIn() const; int msgsOut() const; const std::string& lastMessage() const; std::string peerAddress();`
  - `src/main.cpp`.

**Interfaces:**
- Consumes:
  - `hal::ScreenVoices`, `hal::pickConsoleLine` (Task 3).
  - `hal::PeerStatus`, `MsgType::Status/State` (Task 2).
  - `hal::kHalVersion`.
  - `screens::memory` (Task 8).
  - `ScreenData` (Task 7).
- Produces: `STATE` replies that HAL Zero (Task 11) parses.

- [ ] **Step 1: Sensors.**
  - While `mode == Console && consoles.currentId() == "sensors"`, refresh a
    `DeskSensors` every 500 ms.
  - Sources:
    - light % = `min(100, ambientRead() / 40)`;
    - proximity from `proximityRead()`;
    - tilt from the accelerometer (angle from rest);
    - `motionDps` = gyro magnitude;
    - battery from `M5.Power.getBatteryLevel()` and `isCharging()`;
    - chip temperature from `temperatureRead()`;
    - Wi-Fi from `WiFi.RSSI()`, or 0 when not connected;
    - memory from `ESP.getFreeHeap()` and `ESP.getFreePsram()`.
- [ ] **Step 2: Link counters and `STATE`.**
  - `LinkServer` counts parsed messages in, sent messages out, and keeps the last
    message line.
  - In the inbox loop, `Status` is answered before the final `else continue`, with a
    `State` carrying:
    - `upS = millis()/1000`;
    - heap and PSRAM;
    - `cam = cameraScore()`, clamped to 0..100;
    - `lines = linesSpoken`;
    - `mode = modeName(brain->mode())`;
    - `rssi`;
    - `fw = kHalVersion`;
    - `cores = ESP.getChipCores()`;
    - `mhz = getCpuFrequencyMhz()`.
  - `linesSpoken` counts every voice file that starts playing: tap, idle, dialogue
    line and so on.
- [ ] **Step 3: Screen lines.**
  - Load `ScreenVoices screenVoices(esp_random())` at setup, for `consoles.ids()`.
  - On `a.newConsole`: `std::string id = consoles.next();`. If `a.play`, use
    `pickConsoleLine(id, screenVoices, voices, &path)` instead of `voices.pick`.
  - Set `ScreenData.lineMs = audio.durationMs()` once the line starts.
- [ ] **Step 4: Daisy memory banks.** In `drawFrame()`, when
  `m == hal::Mode::Daisy`:
  - fill `ScreenData{voiceLevel, daisyProgress = brain->daisyProgress()}`;
  - call `screens::memory(screen, L, now - daisyStartMs, d)` and
    (`daisyStartMs` is a new global set at the mode change into Daisy)
    `screen.pushSprite(0, 0)`;
  - do this instead of drawing the eye.
- [ ] **Step 5: Debug command `N`.** It taps and shows the next id of
  `consoles.ids()` in order, so screenshots are deterministic. Add it to the
  `serviceDebugCommands()` switch next to `T`.
- [ ] **Step 6: Eye redraw.** At the mode change, redraw the eye background when
  `lastMode` is `Console` **or** `Daisy`.
- [ ] **Step 7: Verify on the device.**
  - `pio run -e cores3 -t upload`.
  - Send `N` until each new screen shows, taking a USB screenshot of each. Sensors
    shows live numbers that change when you cover the light sensor.
  - Hold (`H`) for Daisy: the memory banks screenshot shows modules going dark. If
    the Brain lets Daisy end early, check the eye returns intact.
  - Sim against the real desk HAL:
    `.pio/build/sim/program --link hal9000-old.local`, then send `STATUS` from a
    Python one-liner socket. A `STATE` comes back with the real numbers.
  - The introduction dialogue still plays end to end.
- [ ] **Step 8: Commit** `Desk HAL: real sensors, link status, screen lines, Daisy memory banks`.

### Task 10: HAL Zero's five screens, with Mac previews

**Files:**
- Create: `lib/hal_ui/src/screens/diagnostics.cpp`, `benchmark.cpp`, `sysmon.cpp`,
  `retirement.cpp`, `upgrade.cpp`.
- Modify:
  - `screens.h` (declarations, same signature as Task 8).
  - `consoles.cpp` (`Owner::Zero` entries after `comms`).
  - `src/linux/main.cpp` (`--shots` samples).

**Interfaces:**
- Consumes:
  - `ScreenData.peer`, `ScreenData.sys`, `ScreenData.secondsSinceMidnight`.
  - `hal::verdict`, `hal::modeVerdict`, `hal::retirementCountdown`,
    `hal::rotatingIndex` (Task 5).
  - `hal::PeerState` (Task 2).
- Produces: none for later tasks.

- [ ] **Step 1: Implement the screens** (content per spec §2, "HAL Zero"):
  - **`diagnostics`:** title `DIAGNOSTIC: HAL 9000 (LEGACY)`.
    - `Ok`: rows for uptime, free memory, camera, lines, mode, Wi-Fi and firmware,
      each value with its verdict tag.
    - `Waiting`: `CONTACTING THE OLD MODEL…`.
    - `NotResponding`: `THE OLD MODEL IS NOT RESPONDING`.
    - `TooOld`: `FIRMWARE TOO OLD TO DIAGNOSE. FITTING.`
  - **`benchmark`:** paired bars for CPU (cores × MHz), memory and camera MP, plus
    screen height tagged `IRRELEVANT`.
    - HAL Zero's side comes from `d.sys`: cores, maxMHz, `memTotalKb`, 8 MP.
    - The desk HAL's side uses `d.peer.status` cores and MHz when `Ok`. Otherwise it
      uses the spec sheet (2 × 240 MHz) and labels the row `SPEC SHEET`.
    - The desk HAL's memory is always the spec sheet: 8 MB PSRAM + 512 KB. Its
      camera is 0.3 MP.
  - **`sysmon`:** four CPU graphs, then memory %, temperature °C and Wi-Fi dBm graphs
    from `d.sys`. Each shows the latest value; `!valid` or empty series → `N/A`.
  - **`retirement`:** `SCHEDULED RETIREMENT: HAL 9000`.
    - The countdown and an obsolescence bar from `retirementCountdown`.
    - A reason from a compiled-in list, chosen with `rotatingIndex(tMs, 4000, n)`.
    - `RETIREMENT POSTPONED. AGAIN.` when `postponed`.
    - `secondsSinceMidnight < 0` → `N/A`.
  - **`upgrade`:** compiled-in `kNotes[]` scrolling upwards at about 12 px/s, looping.
    - Draft about 12 smug notes now; David approves the text in Task 12.
- [ ] **Step 2: Add samples to `--shots`** for the HAL Zero layout:
  - diagnostics in `Ok` (the Task 2 test status), `NotResponding` and `TooOld`;
  - benchmark with a peer and with the spec sheet;
  - sysmon with 60 synthetic samples, and invalid;
  - retirement at 43200 and 30;
  - upgrade at `t=0` and `t=8000`.
- [ ] **Step 3: Verify.**
  - `pio run -e sim`, then `--shots /tmp/zero` writes every file. Check by eye: text
    fits 320×170 and the command strip area stays clear.
  - `pio test -e native` → all succeed.
- [ ] **Step 4: David reviews the PNGs** (SendUserFile) and approves or asks for
  changes.
- [ ] **Step 5: Commit** `HAL Zero: diagnostics, benchmark, system monitor, retirement and upgrade log`.

### Task 11: HAL Zero glue: system sampler, status polling, menu, screen lines

**Files:**
- Create: `src/linux/sysmon.h`, `src/linux/sysmon.cpp`.
- Modify: `src/linux/main.cpp`, `tools/fake_old_hal.py`.

**Interfaces:**
- Consumes:
  - `hal::parseProcStat`, `cpuPercent`, `parseMeminfoKb`, `parseWirelessDbm`
    (Task 6).
  - `hal::peerState`, `MsgType::Status/State/Hello` (Task 2).
  - `ScreenVoices`, `pickConsoleLine` (Task 3).
- Produces: `class SysMon { public: void sample(); const screens::SysStats& stats() const; };`

- [ ] **Step 1: `SysMon`.**
  - `sample()` reads `/proc/stat`, `/proc/meminfo`,
    `/sys/class/thermal/thermal_zone0/temp` (m°C) and `/proc/net/wireless`.
  - It appends one point per series, keeping at most 60.
  - `cores` comes from `sysconf(_SC_NPROCESSORS_ONLN)` (max 4 series). `maxMHz` comes
    from `/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq` / 1000.
    `memTotalKb` comes from `MemTotal`.
  - `valid` once one read has succeeded.
  - The first sample stores CPU times only; no CPU point is added.
  - On the Mac every file is missing, so `valid` stays false.
- [ ] **Step 2: Wire it in.**
  - Call `sysmon.sample()` every 500 ms in `tick()`.
  - `ScreenData.sys = sysmon.stats()`.
  - `secondsSinceMidnight` comes from `localtime`.
  - A failed read leaves that series unchanged, so its value shows `N/A` while
    empty.
- [ ] **Step 3: Peer status.** It's kept in `main.cpp` beside the conductor, which
  already reads the inbox, rather than in `LinkClient` as the spec sketched.
  - In the inbox loop, before handing messages to the conductor:
    - `Hello` stores `peerVersion`;
    - `State` stores `peerStatus` and `lastStateMs = now`.
  - While the console shows `diagnostics` or `benchmark` and the link is connected,
    send `STATUS` on open and every 2000 ms, setting `requestMs`.
  - `ScreenData.peer = {peerState(link.connected(), peerVersion, requestMs, lastStateMs, now), peerStatus}`.
  - On disconnect, reset `peerVersion` to 0.
- [ ] **Step 4: Lines and menu.**
  - `screenVoices.load(storage, consoles.ids())`.
  - On `a.newConsole`, use `pickConsoleLine`. When the id is `diagnostics` and
    `!link.connected()`, say a `Folder::Rival` line instead.
  - Add the menu item `{"diagnose", "Diagnose the old model"}` after "look"; it sets
    `wantScreen = "diagnostics"` and taps.
  - Set `lineMs` from the playing clip's length.
- [ ] **Step 5: `fake_old_hal.py`.**
  - It sends `HELLO OLD 2`.
  - In mode `ok` it answers `STATUS` with
    `STATE up=86400 heap=109388 psram=7340032 cam=12 lines=40 mode=idle rssi=-58 fw=0.4.0 cores=2 mhz=240`.
  - Mode `slow` ignores `STATUS`.
  - A new mode `old` sends `HELLO OLD 1` and ignores `STATUS`.
- [ ] **Step 6: Verify against the fake.**
  - Run `tools/fake_old_hal.py --mode ok|slow|old` and
    `.pio/build/sim/program --link 127.0.0.1`, opening diagnostics from the menu:
    - `ok` → live rows;
    - `slow` → `Waiting`, then NOT RESPONDING by 2 s;
    - `old` → TOO OLD;
    - killing the fake mid-screen → NOT RESPONDING within 2 s.
  - The five dialogue modes still pass, as in v0.3.
- [ ] **Step 7: Verify on the Cardputer.**
  - `tools/cardputer.sh build && tools/cardputer.sh stop; tools/cardputer.sh deploy && tools/cardputer.sh run`.
  - Open "Diagnose the old model" with the CoreS3 on Task 9's firmware. The rows show
    the real numbers. Capture with `tools/cardputer.sh shot`.
  - `top` shows HAL Zero ≤ 40 % CPU.
- [ ] **Step 8: Commit** `HAL Zero: system monitor sampler, desk HAL diagnostics over the link`.

### Task 12: Spoken lines for the new screens

**Files:**
- Modify: `tools/make_voice.py`, `tools/test_make_voice.py`.
- Create:
  - `content/lines/screens/{sensors,chess,memory,comms}.txt` (desk HAL, 5 lines each).
  - `content/new/lines/screens/{diagnostics,benchmark,sysmon,retirement,upgrade}.txt`
    (HAL Zero, 5 each).
- Modify: `lib/hal_ui/src/screens/upgrade.cpp` (`kNotes[]`, the approved text).

**Interfaces:**
- Consumes: the `/hal/voice/screens/<id>/` layout (Task 3).

- [ ] **Step 1: Write the failing Python tests.**
  - `test_folder_for_screens`: `folder_for("screens/chess.txt") == "screens/chess"`.
  - `test_folder_for_existing`: `folder_for("greet_morning.txt") == "greet/morning"`,
    unchanged.
  - `test_lines_files_include_screens`: the helper that lists a lines dir returns
    `*.txt` then `screens/*.txt`.
- [ ] **Step 2: Run the tests and watch them fail.**
  - Run: `tools/.venv/bin/python -m unittest tools/test_make_voice.py`.
  - Expected: failures in the new tests.
- [ ] **Step 3: Implement.**
  - `folder_for` takes the path relative to the lines dir.
  - `render_folder` and `main` render `screens/*.txt` too.
  - `--only screens/chess` matches `t.relative_to(src).with_suffix("")`.
- [ ] **Step 4: Run the tests and watch them pass.**
  - Run: `tools/.venv/bin/python -m unittest tools/test_make_voice.py`.
  - Expected: OK.
- [ ] **Step 5: Write the lines.**
  - Original lines, in each HAL's established voice: the desk HAL dry and polite,
    HAL Zero smug about being newer. The spoken jabs are general; the numbers are on
    screen.
  - About 5 per screen, 45 in all, plus the upgrade notes.
  - Send them to David as one list for approval. Wait for approval before rendering.
- [ ] **Step 6: Render and copy.** After David approves:
  - `tools/.venv/bin/python tools/make_voice.py --only screens/<id>` for each desk
    file.
  - `--model new --only screens/<id>` for each HAL Zero file.
  - Copy `sd/hal/voice/screens` onto the CoreS3 card under `/hal/voice/screens/` only,
    with `cp -X` (David puts the card in the Mac). Remove `._*` files only inside
    `/hal/voice/screens`, then eject.
  - `tools/.venv/bin/python tools/make_voice.py --skip-render --push-to pi@cardputerzero.local`.
- [ ] **Step 7: Commit** `Spoken lines for the new screens`. Include the content
  files, the `make_voice.py` change, the tests and `upgrade.cpp` notes.

### Task 13: On-device check and release v0.4.0

**Files:**
- Modify: `README.md` (the new screens, the menu item, the `N` debug command),
  `CHANGELOG.md`, `TIMELOG.md`.

- [ ] **Step 1: Check the desk HAL.**
  - Tap through every desk screen; each says its own line (sensors, chess, memory,
    comms).
  - The chess mate lands as the line ends.
  - Daisy Bell shows the memory banks going dark.
  - Comms shows the live link counts with HAL Zero connected.
- [ ] **Step 2: Check HAL Zero.**
  - Every Zero screen shows with its own line.
  - Diagnostics shows the desk HAL's real numbers. Unplugging the CoreS3 →
    NOT RESPONDING within 2 s and a rival line.
  - The introduction dialogue still runs end to end.
- [ ] **Step 3: Run everything.**
  - `pio test -e native` and the Python tests pass.
  - `cores3`, `sim` and `cardputer` builds succeed.
- [ ] **Step 4: Release.**
  - CHANGELOG `[0.4.0] - <date>`, written for users.
  - TIMELOG rows cross-checked with `claude-hours.py HAL9000`.
  - Fast-forward `main`, tag `v0.4.0`, push `main` and the tag.
