# More console screens for both HALs — design (v0.4)

Date: 2026-11-10 · Owner: David · Status: approved in chat, awaiting spec review
Builds on: `2026-11-06-hal9000-design.md` (console screens, Brain) and the v0.3 plan
(`~/.claude/plans/i-am-now-the-memoized-thunder.md`: HAL Zero, the link, dialogues)

## Purpose

Make both HALs look busier and funnier on video (Reddit, and M5Stack's attention):

- The **desk HAL** (CoreS3) gets four new console screens.
- **HAL Zero** (Cardputer Zero) gets five of his own. One of them diagnoses the desk
  HAL over the link, using its real status.
- Each new screen comes with its own spoken lines, in the right HAL's voice.

**What David asked for:**
- The eye stays each HAL's resting face. New screens join the tap rotation.
- The desk HAL gets sensors, chess, memory banks and comms.
- HAL Zero gets diagnostics, benchmark, system monitor, retirement countdown and
  upgrade log.
- The remarks are spoken and shown on screen.
- One shared screen catalogue (approach 1 of 3).

**What we assumed (approved in chat):**
- Today's five shared screens and the camera view stay on both HALs.
- During Daisy Bell, the desk HAL's memory banks go dark one by one.
- The spoken jabs are general; the real numbers are on the screen.
- v0.3.0 is released first; this work is v0.4.0.

**Constraints:**
- All screens are original designs in the spirit of the film's displays, not copies.
- No microphone readings: the CoreS3's mic and speaker share one audio bus, and HAL
  is speaking while a screen is up.
- The CoreS3's SD card is shared with the PortaPack: only `/hal/` is touched.

**Out of scope:** live AI replies; replacing the Cardputer launcher; new dialogues;
an always-on screens mode (David chose "eye home").

## 1. Screen catalogue (`lib/hal_ui`)

Every built-in screen is one entry:

```cpp
enum class Owner { Both, Desk, Zero };
struct ScreenDef {
  const char* id;     // "chess"; also its voice folder /hal/voice/screens/<id>/
  Owner owner;
  void (*draw)(M5Canvas&, const hal::Layout&, uint32_t tMs, const ScreenData&);
};
```

- **`Consoles::begin(seed, Owner device, withCamera, storage)`** builds the device's
  rotation: the `Both` entries, its own entries, the camera view (when there's a
  camera) and any `/hal/screens/*.jpg`.
  - The shuffle bag still shows every screen before any repeats.
- **`next()`** returns the chosen screen's id, so callers can pick its line.
  - The camera view keeps the id `visual` and its existing `/hal/voice/camera` lines.
  - JPG screens return `image`.
- **`show(id)`** jumps to a screen, for `--shots` and HAL Zero's menu.
- **`ScreenData`** replaces today's `CameraView` argument. It carries:
  - `cam` (as today), `voiceLevel`, `lineMs` (length of the line being said),
    `daisyProgress`;
  - optional blocks, each with a `valid` flag: `DeskSensors`, `LinkStats`,
    `SysStats`, `PeerStatus`.
  - Each HAL fills only what it has; a screen whose block isn't valid shows "N/A" or
    its offline state.
- **Rotations:**
  - Desk HAL: vitals, readout, trajectory, analyser, globe (today); sensors, chess,
    memory, comms (new); the camera view; your images.
  - HAL Zero: the same five; diagnostics, benchmark, sysmon, retirement, upgrade
    (new); the camera view.

## 2. The screens

All screens use the existing title bar and status line (`screens::frame`). HAL Zero's
use his HUD colours (blue and cyan text, red accents). Sizes come from `Layout`, so
every screen also renders in either layout for previews.

### Desk HAL

- **Sensor array** (`sensors`): gauges for room light (the LTR-553's light channel,
  switched on next to proximity), proximity, tilt and motion (BMI270), battery % and
  charging (AXP2101 via `M5.Power`), chip temperature, Wi-Fi signal and free memory
  (internal and PSRAM).
  - Read every 500 ms, only while the screen is up.
  - Wi-Fi off (link off, after the time sync) shows `ASLEEP`.
- **Chess** (`chess`): an 8×8 board with simple drawn pieces.
  - One of several well-known short games, HAL always on the winning side: Scholar's
    mate, Fool's mate (HAL as Black), Légal's mate, the Blackburne Shilling trap.
  - Moves are spread over HAL's line, 300–900 ms each, and the mating move lands as
    the line ends, with a CHECKMATE banner.
  - The games are fixed move lists, with no chess engine and no castling, en passant
    or promotion; replay is tested.
- **Memory banks** (`memory`): rows of logic modules glowing and pulsing with HAL's
  voice level.
  - During Daisy Bell, the desk HAL shows this screen instead of the eye. Modules go
    dark one by one as `daisyProgress` rises; at the end the screen goes black, as
    the eye does today.
- **Comms antenna** (`comms`): a dish slowly tracking.
  - It shows the real Wi-Fi signal and the link to HAL Zero: connected or not, peer
    address, messages in and out, and the last message (e.g. `SAY introduction 7`).
  - With no link it shows `NO CARRIER`; with `link=off` it adds `LINK DISABLED`.

### HAL Zero

- **Diagnostics** (`diagnostics`): "DIAGNOSTIC: HAL 9000 (LEGACY)".
  - Rows from `PeerStatus`: uptime, free memory, camera motion, lines spoken, mode,
    Wi-Fi signal, firmware version.
  - Each row has a verdict tag (VINTAGE, ADORABLE, ADEQUATE…) from a tested
    threshold table.
  - HAL Zero sends `STATUS` when the screen opens, then every 2 s.
  - No `STATE` within 2 s: THE OLD MODEL IS NOT RESPONDING, plus a `rival` line.
  - A desk HAL whose `HELLO` says link version 1: FIRMWARE TOO OLD TO DIAGNOSE. FITTING.
  - Also in HAL Zero's menu, as "Diagnose the old model".
- **Benchmark** (`benchmark`): paired bars, HAL Zero against HAL 9000.
  - Rows: CPU cores × MHz, memory, camera megapixels.
  - HAL Zero's side is read from Linux: cores, max MHz from cpufreq, `MemTotal`. The
    camera is the IMX219's 8 MP.
  - The desk HAL's side comes from `PeerStatus` (cores, MHz, PSRAM). Without it, the
    spec sheet is used (2 × 240 MHz, 8 MB PSRAM, 0.3 MP) and labelled `SPEC SHEET`.
  - The screen-height row (170 vs 240), which HAL Zero loses, is tagged `IRRELEVANT`.
- **System monitor** (`sysmon`): scrolling graphs, sampled every 500 ms all the time
  (60 samples), so history is there when the screen opens.
  - Each of the 4 CPU cores (`/proc/stat`), available memory (`/proc/meminfo`),
    temperature (`thermal_zone0`) and Wi-Fi signal (`/proc/net/wireless`).
- **Retirement countdown** (`retirement`): "SCHEDULED RETIREMENT: HAL 9000".
  - HH:MM:SS to local midnight, an obsolescence bar for the share of the day gone,
    and a reason that changes every 4 s.
  - In the first minute after midnight: RETIREMENT POSTPONED. AGAIN.
- **Upgrade log** (`upgrade`): smug release notes scrolling upwards.
  - The notes are compiled in. David approves their text with the voice lines.

## 3. Lines for each screen (`hal_core`)

- **`ScreenVoices`** (new, pure, tested): lists `/hal/voice/screens/<id>/*.wav`
  through `Storage` (same file rules as `VoiceIndex`). It keeps a shuffle bag per
  screen, with `count(id)` and `next(id)`.
- **On a tap**, the Brain still asks for a line (`Folder::Tap`). Each HAL's glue
  then picks the line:
  - `visual`: a camera line, as today;
  - a screen with lines: one of its lines;
  - otherwise: the tap line.
- **Content:**
  - `content/lines/screens/<id>.txt` holds the desk HAL's lines;
    `content/new/lines/screens/<id>.txt` holds HAL Zero's. About 5 per screen,
    roughly 45 in all.
  - David approves the text, then `make_voice.py` renders each set in its HAL's voice
    into `screens/<id>/`.
  - The desk set goes to the CoreS3 card (only `/hal/`), HAL Zero's to `~/hal` on
    the Cardputer.

## 4. Link: status (`hal_link`, version 2)

- **`STATUS`** (HAL Zero → desk HAL): no arguments.
- **`STATE up=<s> heap=<bytes> psram=<bytes> cam=<0-100> lines=<n> mode=<name> rssi=<dBm> fw=<x.y.z> cores=<n> mhz=<n>`**
  (desk HAL → HAL Zero), sent at once, even mid-dialogue.
- **Parser:** strict, like today.
  - Only these keys, each at most once; numbers in range; `mode` from the Brain's
    mode names; `fw` digits and dots only. The whole line fits the 128-byte limit.
  - Anything else is `Invalid` and ignored.
- `kLinkVersion` becomes 2, and both sides keep the peer's `HELLO` version.
  `fake_old_hal.py` learns `STATUS`.
- **Desk HAL:**
  - Counts lines spoken since boot (`markPlayed`).
  - `LinkServer` keeps the comms counters: connected, peer address, messages in and
    out, last message.
- **HAL Zero:** `LinkClient` exposes the last `PeerStatus`, its age, and the peer's
  version.

## 5. Error handling

- **Unreachable or old desk HAL:** the diagnostics offline and too-old states above.
  Benchmark falls back to the spec sheet.
- **A sensor or `/proc` read fails:** that value shows `N/A`; the screen still draws.
- **A screen with no voice folder or no lines:** the usual tap line.
- **Memory:** the CoreS3 draws everything into the existing console canvas, with no
  new large buffers. Desk sensors are read only while the screen is up.

## 6. Testing

**Test-first, in `pio test -e native`:**
- `STATUS` and `STATE` format and parse, including rejecting unknown keys, values
  out of range and over-long lines.
- `ScreenVoices`: per-screen bags and the tap fallback.
- Chess replay: each game reaches its stored final position, and the last move is
  flagged as mate.
- The move timing helper.
- Memory banks lit against Daisy progress.
- Retirement countdown text at given times, including midnight.
- Diagnostic verdict thresholds.
- `/proc/stat`, `/proc/meminfo` and `/proc/net/wireless` parsers, fed sample text.

**Previews:**
- `hal9000 --shots DIR` renders every HAL Zero screen. `--layout cores3` renders the
  desk HAL's set at 320×240, so David can review all new screens on the Mac.
- Then the CoreS3 USB screenshot and `tools/cardputer.sh shot`.

**Link:**
- Sim and Cardputer against `fake_old_hal.py`: ok, slow (no `STATE`), and an old
  version.
- Then the real pair: diagnostics shows the desk HAL's live numbers, and mid-dialogue
  `STATUS` doesn't disturb the conversation.

## 7. Build order

1. Release **v0.3.0**: README, CHANGELOG, TIMELOG (checked with `claude-hours.py`), tag.
2. Core: `ScreenVoices`, link v2 status, chess replay, Daisy and countdown helpers,
   verdicts, `/proc` parsers (tests first).
3. Catalogue and `ScreenData` in `hal_ui`; existing screens moved over unchanged
   (CoreS3 screenshots match before and after).
4. Desk HAL: sensors, chess, memory banks (+ Daisy), comms; previews → David →
   flash.
5. HAL Zero: diagnostics, benchmark, sysmon, retirement, upgrade; previews → David →
   deploy.
6. Lines: written, David approves (with the upgrade notes), rendered, copied to both
   HALs.
7. Check on both devices; CHANGELOG, TIMELOG; release **v0.4.0**.
