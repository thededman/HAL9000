# Film-style screens with real data — design (v0.5)

Date: 2026-11-11 · Owner: David · Status: approved (plan mode, 2026-11-10)
Builds on: `2026-11-10-hal-screens-design.md` (screen catalogue, `ScreenData`, per-screen lines)

## Purpose

David wants screens like the film's computer displays. The film's frames are copyrighted, so
these are **original screens built the way those displays work** (flat colour cards, bold
three-letter codes, line graphics), showing **each HAL's real data**. The AI brain (v0.6) is a
separate design: `~/.claude/plans/i-am-now-the-memoized-thunder.md`, Part B.

**Constraints:** original codes, layouts and graphics (no copies of the film's frames); no new
large buffers on the CoreS3 (everything draws into the existing 8-bit console canvas, no
per-frame heap churn); the desk HAL reads its sensors only while a screen that shows them is
up; the CoreS3's SD card is shared with the PortaPack (only `/hal/` is touched).

## 1. Readings (`lib/hal_core/src/hal_readings.*`, pure, tested)

```cpp
struct Reading {
  std::string code, name, value;  // "PWR", "POWER", "87 %"
  float level = -1;               // 0..1 for bars and traces; < 0 = none
  bool alarm = false;
  bool valid = false;             // false: shows N/A, never alarms
  std::vector<float> trace;       // recent levels, oldest first (kept by logReading)
};
void logReading(Reading& slot, const Reading& fresh, size_t cap = 40);  // copy + append level
```

Builders format the value and decide the alarm:

| Builder | Code | Value | Alarm |
|---|---|---|---|
| `powerReading(pct, charging)` | PWR | `87 %` / `87 % CHG` | < 20 % and not charging |
| `lightReading(pct)` | LUX | `38 %` | never |
| `proximityReading(raw)` | PRX | `35` (level raw/1000) | never |
| `motionReading(dps)` | MOT | `12 DEG/S` (level dps/250) | > 120 |
| `tiltReading(deg)` | TLT | `3 DEG` (level deg/90) | ≥ 60 |
| `tempReading(c)` | TMP | `48 C` (level (c−20)/60) | > 70 |
| `netReading(rssi)` | NET | `-46 DBM` / `OFF` (rssi 0) | never |
| `memoryReading(freeKb, totalKb)` | MEM | `102 KB` or `3 MB` free (level used share) | free < 10 % |
| `linkReading(enabled, connected)` | LNK | `OFF` / `NO CARRIER` / `CONNECTED` | enabled and not connected |
| `cameraReading(present, score)` | CAM | `OFFLINE` / `MOTION 12` | never |
| `audioReading(level255, lines)` | AUD | `40 LINES` (level = voice) | never |
| `clockReading(secOfDay)` | CLK | `20:27` (level = share of day) | never |
| `uptimeReading(seconds)` | UPT | `00:12:45` / `1D 02:10` | never |
| `cpuReading(pct)` | CPU | `41 %` | > 95 |
| `peerReading(state, mode)` | DSK | mode in capitals / `NOT RESPONDING` / `TOO OLD` / `WAITING` | NotResponding |

Out-of-range inputs (`pct < 0`, `c <= -999`, `secOfDay < 0`, `totalKb <= 0`) give `valid = false`.

## 2. Wireframe maths (`lib/hal_core/src/hal_wire.*`, pure, tested)

`Vec3`, `Mat3`, `rotationBetween(from, to)` (the smallest rotation taking one direction onto
the other; identity for zero or parallel vectors; any perpendicular axis for opposite ones),
`rotationYawPitch(yawDeg, pitchDeg)`, `mul`, `apply`, and `project(v, scale, cx, cy, distance)`
(simple perspective). `MotionDetector::gravity(now[3], home[3])` exposes the smoothed gravity
and the home pose (false before HAL first settles).

## 3. `ScreenData` additions

```cpp
const std::vector<hal::Reading>* readings = nullptr;  // owned by the HAL's glue, updated in place
struct UnitPart { const char* code; bool ok; };
std::vector<UnitPart> parts;                           // for the unit screen
enum class UnitModel { CoreS3, Cardputer } unitModel;
float gravity[3], gravityHome[3]; bool gravityValid = false;
```

## 4. The screens (`Owner::Both`, ids are also the voice folders)

- **`cards`** (full-bleed, no title bar): one flat-colour card per reading, hard cut every
  1.6 s. Big bold code (`FreeSansBold24pt7b`, scaled), the name, the live value large, a trace
  of recent levels, and position dots. Colours cycle through a fixed palette (blue, green,
  amber, violet, white, teal); an alarm card is red with a blinking border. No readings: `NO DATA`.
- **`wall`** ("MONITOR WALL"): a 4×3 grid of tiles on the CoreS3, 4×2 on HAL Zero, each with
  code and value on a flat colour; alarm tiles blink red. One tile at a time (every 2 s) is
  selected and shows its trace as a mini graph instead of the value.
- **`unit`** ("UNIT DIAGNOSTIC"): a wireframe of the device (a slab with its screen and lens
  for the CoreS3; a wider slab with screen and keyboard grid for the Cardputer) in a 3/4 view
  that **tilts by the rotation from the home pose to gravity now**. Callouts with leader lines
  name the parts, each `OK` or `FAULT`. Footer: `ATTITUDE n DEG` and `ALL SYSTEMS NOMINAL` or
  `n FAULTS`. Before HAL settles the model sits in the default view.

## 5. Each HAL's data

- **Desk:** PWR LUX PRX MOT TMP NET MEM LNK CAM AUD CLK UPT. Read every 500 ms while
  `sensors`, `cards`, `wall` or `unit` is showing (the existing `readDeskSensors` and
  `linkStats`). Parts: CAM, SPK, IMU, PRX, SD, NET, LNK.
- **HAL Zero:** CPU MEM TMP NET LNK CAM TLT AUD CLK UPT DSK from `SysMon`, the motion sensor,
  the link and the desk HAL's `PeerStatus`, updated every 500 ms. Parts: CAM, SPK, IMU, KBD,
  NET, LNK. On the Mac the `/proc` readings are `N/A`.

## 6. Lines, previews, testing

- 5 lines per screen per HAL (30) in `content/lines/screens/` and `content/new/lines/screens/`;
  David approves the text before rendering (`make_voice.py --only screens/<id>`).
- `--shots` renders all three for HAL Zero, `--shots --layout cores3` for the desk, with sample
  readings including an alarm card and a tilted unit. David reviews them before flashing.
- Tests first in `pio test -e native`: every builder's formatting and alarm boundary,
  `logReading`'s cap, the wire maths, `MotionDetector::gravity`.
- On the devices: USB screenshots of each screen on the CoreS3 (`N` steps through them) and
  `tools/cardputer.sh run --show <id>` + `shot` on the Cardputer; tilt the CoreS3 and check
  the wireframe follows.
- Release v0.5.0 after a fresh whole-branch review: CHANGELOG, TIMELOG, tag.
