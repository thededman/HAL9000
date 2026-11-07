# HAL 9000 camera — design (v0.2)

Date: 2026-11-06 · Owner: David · Status: approved in chat, awaiting spec review
Builds on: `2026-11-06-hal9000-design.md` (v0.1)

## Purpose

Use the CoreS3's front camera so HAL visibly watches the room: a tiny live preview
beside the eye, real walk-up greetings when someone steps into view, and a "Visual
Analysis" console screen where HAL comments on what he sees with new camera lines.
Everything stays on the device: frames live only in RAM, nothing is stored or sent.

Out of scope for v0.2: cloud vision ("See"), which waits on David's choice between a
Mac-side server (spoken in Alan's voice) and on-device text.

## Spike findings (branch `spike/camera`, 2026-11-06)

- GC0308 via `esp_camera`, pins from M5Stack's M5CoreS3 driver (`pin_xclk = -1`,
  SCCB on 12/11, D0–D7 39,40,41,42,15,16,48,47, VSYNC 46, HREF 38, PCLK 45).
- Init sequence: `M5.In_I2C.release()` → `esp_camera_init` → `M5.In_I2C.begin()`.
  Touch, IMU, RTC and proximity keep working (checked on the device, touch by David).
  The Arduino I2C layer logs a harmless "bus already acquired" error once at init.
- QQVGA (160×120) RGB565, 2 frame buffers in PSRAM, `CAMERA_GRAB_LATEST`, polled at
  ~15 fps: eye stays smooth; free internal heap ~80 KB after init and Wi-Fi sync.
- Motion score (mean absolute grey difference, 80×60, consecutive frames): 2–6 for a
  still room, 77 when HAL was moved.
- Camera byte order matches M5GFX's sprite buffers (both byte-swapped RGB565).

## Behaviour

| Feature | When | What happens |
|---|---|---|
| Preview | Eye is showing, mode not Daisy/Dark, `camera_preview=on` | Live 80×60 view at (240,180), bottom-right, clear of the ring; follows the backlight (dim in quiet hours) |
| Arrival greeting | Idle, outside quiet hours, greet cooldown passed | Someone steps into view → the time-of-day greeting (same `greet/` folders and 10-minute cooldown as the wave; the wave still works) |
| Visual Analysis | A tap picks this console screen (it joins the shuffled rotation when the camera works) | 160×120 live view centred, red HUD: crosshair, a box on the moving area (or "SCANNING"), readouts (SUBJECT: DAVE, etc.); HAL plays a line from `camera/` instead of `tap/` |

### Arrival detection (pure logic, tested)

`hal::ArrivalDetector(threshold)` with `bool update(int score, uint32_t nowMs)`:
- **Settled:** score < threshold / 2 continuously for 20 s.
- **Arrival:** while settled, score ≥ threshold on 2 consecutive frames → returns true
  once, then the room must settle again (20 s) before the next arrival.
- Time comparisons are wrap-safe (`millis()` wraps after ~49.7 days).
- Light switching on counts as an arrival (intended).

## SD card and settings

- New folder `/hal/voice/camera/` (about 20 lines, `content/lines/camera.txt`,
  rendered with the same voice chain). Missing or empty → Visual Analysis falls back
  to a `tap/` line.
- New `config.txt` keys (defaults in brackets):
  - `camera=on|off` [on] — off: camera never starts; no preview, arrivals or Visual Analysis.
  - `camera_preview=on|off` [on]
  - `camera_motion_threshold=1..255` [20]
  - Booleans accept on/off, yes/no, true/false, 1/0 (any case); anything else → default + warning.

## Firmware structure

| Unit | Kind | Job |
|---|---|---|
| `hal::ArrivalDetector` (`hal_motion`) | pure, tested | Score stream → arrival events |
| `hal::Config` (`hal_config`) | pure, tested | The three new keys |
| `hal::Folder::Camera` (`hal_voice_index`) | pure, tested | `/hal/voice/camera` |
| `src/camera.{h,cpp}` | device | Init, ~15 fps polling, 80×60 preview sprite, motion score and moving-area box, latest 160×120 frame for the console |
| `src/screens/visual.cpp` | device | The Visual Analysis screen |
| `Consoles::next()` | device | Returns true when the Visual Analysis screen was picked |
| `main.cpp` | device | Feeds arrivals into the Brain's walk-up input (OR with the wave); swaps `Tap` for `Camera` when Visual Analysis is picked |

The Brain is unchanged: arrivals and waves share its existing walk-up input.

## Errors

- Camera init fails → logged; HAL behaves exactly like v0.1.
- No frame for a poll → skip it; the preview keeps its last image.

## Testing

- Native tests: ArrivalDetector (settle, arrival, single fire, re-settle, flicker
  ignored, wrap-safe), config keys and booleans, Camera folder path.
- On the device: preview and Visual Analysis screenshots; David walks into view →
  greeting; `camera=off` boots without the camera; touch/IMU/proximity/Wi-Fi still work;
  free heap logged after the Wi-Fi sync.

## Release

v0.2.0: CHANGELOG entry, TIMELOG line, tag, push to `thededman/HAL9000`.
