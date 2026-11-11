# Film-Style Screens Implementation Plan (v0.5)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Three new console screens on both HALs (`cards`, `wall`, `unit`) in the style of the
film's displays, driven by each HAL's real readings, with their own spoken lines; release v0.5.0.

**Architecture:** Pure, tested logic in `lib/hal_core` (`hal_readings`, `hal_wire`, a gravity
accessor on `MotionDetector`); three draw functions in `lib/hal_ui` registered `Owner::Both` in
the screen catalogue; each HAL's glue owns a persistent list of readings updated every 500 ms.

**Tech stack:** C++17, PlatformIO, Unity, M5GFX 0.3.0; Python voice tools.

**Spec:** `docs/superpowers/specs/2026-11-11-film-style-screens-design.md` (read it first).

## Global Constraints

- Original designs only: no copies of the film's frames, codes-and-layout sets or graphics.
- CoreS3: no new large buffers, no per-frame heap allocation in the draw path (readings are
  passed by pointer and updated in place); sensors read only while `sensors`, `cards`, `wall`
  or `unit` is showing.
- Screen ids `cards`, `wall`, `unit` are also the voice folders `/hal/voice/screens/<id>/`.
- Builds: `~/.platformio/penv/bin/pio run -e cores3|sim`, `tools/cardputer.sh build`. Tests:
  `pio test -e native` (163 existing stay green), `tools/.venv/bin/python -m unittest tools/test_make_voice.py`.
- CoreS3 card: only `/hal/`; never print `wifi_password`. Never type passwords.
- Commits end with `Co-Authored-By: Claude Fable 5.2 <noreply@anthropic.com>`; push after each task.
- The repo is public and other people follow `docs/SETUP.md`: anything that changes set-up (a
  tool, download, config key, script or device step) updates that guide in the same commit.
  Supported: macOS (tested) and Linux; Windows isn't covered. No private details in the repo.

## Review Focus

1. **No readings yet** (first 500 ms after a screen opens, or `readings == nullptr`): every
   screen draws `NO DATA` / `N/A`, never reads past the list. Owner: Task 3 (previews
   `*-no-data`).
2. **Fewer readings than tiles** (HAL Zero has 11, the wall wants 8 or 12): empty tiles are
   drawn blank. Owner: Task 3.
3. **Gravity before HAL settles, zero vectors, or exactly opposite** (unit upside down): the
   wireframe never shows NaN geometry. Owner: Task 2 (`test_rotation_degenerate`).
4. **Long values** (`NOT RESPONDING` on a wall tile): clipped to the tile, never drawn over
   the neighbour. Owner: Task 3 (preview with the DSK alarm).
5. **Trace growth:** `logReading` never grows past its cap over hours. Owner: Task 1
   (`test_log_caps_trace`).

---

### Task 1: Readings (`hal_readings`)

**Files:** create `lib/hal_core/src/hal_readings.{h,cpp}`, `test/test_readings/test_main.cpp`.
**Produces:** `hal::Reading`, `hal::logReading`, and the fifteen builders exactly as in spec §1.

- [ ] Write the failing tests: one per builder covering the format and each alarm boundary
  from the spec's table (e.g. `powerReading(19,false).alarm`, `!powerReading(19,true).alarm`,
  `powerReading(20,false)` no alarm, value `"87 % CHG"`; `tempReading(70)` no alarm, `71` alarm;
  `motionReading(120)` no, `121` yes; `tiltReading(60)` yes; `cpuReading(95)` no, `96` yes;
  `memoryReading(102, 320)` → `"102 KB"`, `memoryReading(3072, 8192)` → `"3 MB"`, free < 10 %
  alarms; `linkReading(false,false)` `"OFF"` no alarm, `(true,false)` `"NO CARRIER"` alarm,
  `(true,true)` `"CONNECTED"`; `netReading(0)` `"OFF"`; `clockReading(73620)` `"20:27"`;
  `uptimeReading(765)` `"00:12:45"`, `uptimeReading(94200)` `"1D 02:10"`; `peerReading` for
  each state; invalid inputs give `valid == false` and no alarm), plus `test_log_caps_trace`
  (50 logs with cap 40 keep 40, newest last) and `test_log_copies_fields`.
- [ ] Run `pio test -e native -f test_readings`: fails to compile (header missing).
- [ ] Implement.
- [ ] `pio test -e native`: all pass.
- [ ] Commit `Readings for the film-style screens`.

### Task 2: Wireframe maths and gravity

**Files:** create `lib/hal_core/src/hal_wire.{h,cpp}`, `test/test_wire/test_main.cpp`; modify
`lib/hal_core/src/hal_motion.{h,cpp}`, `test/test_motion/test_main.cpp`.
**Produces:** `hal::Vec3 {x,y,z}`, `hal::Mat3 {float m[3][3]}`, `Mat3 rotationBetween(Vec3 from, Vec3 to)`,
`Mat3 rotationYawPitch(float yawDeg, float pitchDeg)`, `Mat3 mul(const Mat3&, const Mat3&)`,
`Vec3 apply(const Mat3&, Vec3)`, `struct Point2 {float x, y;}`,
`Point2 project(Vec3 v, float scale, float cx, float cy, float distance)`;
`bool MotionDetector::gravity(float now[3], float home[3]) const`.

- [ ] Failing tests: `test_rotation_identity` (same direction → identity within 1e-4),
  `test_rotation_maps_from_to_to` (three vector pairs), `test_rotation_is_orthonormal`,
  `test_rotation_degenerate` (zero vector → identity; exactly opposite → still maps from to
  to, no NaN), `test_yaw_pitch` (yaw 90 takes +x to −z), `test_project` (origin → centre;
  nearer z → farther from centre); motion: `test_gravity_accessor` (false before rest; after
  2.5 s still at (−1,0,0) both are ≈ that; after a 30° tilt `now` moves, `home` stays).
- [ ] Run: fails to compile.
- [ ] Implement (Rodrigues' formula; `project`: `f = distance / (distance - v.z)`).
- [ ] `pio test -e native`: all pass.
- [ ] Commit `Wireframe maths and gravity for the unit screen`.

### Task 3: The three screens and previews

**Files:** create `lib/hal_ui/src/screens/{cards,wall,unit}.cpp`; modify `screens.h`
(`ScreenData` per spec §3, declarations), `consoles.cpp` (three `Owner::Both` entries after
`globe`), `src/linux/main.cpp` (`--shots` variants for both layouts).
**Consumes:** Tasks 1–2. **Produces:** `screens::cards|wall|unit(M5Canvas&, const hal::Layout&, uint32_t, const ScreenData&)`.

- [ ] Implement per spec §4.
- [ ] Previews with sample readings for both layouts: `cards` (a normal card and an alarm
  card), `cards-no-data`, `wall` (with the DSK `NOT RESPONDING` alarm; HAL Zero's with 11
  readings), `wall-no-data`, `unit` level and tilted 30°, `unit` before settling, both models.
- [ ] Check every PNG by eye (nothing overlapping or off-screen), run all tests and builds.
- [ ] Send the previews to David; carry on (changes are folded in afterwards).
- [ ] Commit `Film-style screens: status cards, monitor wall, unit diagnostic`.

### Task 4: Real data on both HALs

**Files:** modify `src/main.cpp`, `src/linux/main.cpp`.

- [ ] Desk: a persistent `std::vector<hal::Reading>` (spec §5) refreshed with `logReading`
  every 500 ms while `sensors|cards|wall|unit` shows; `ScreenData.readings`, `parts`,
  `unitModel`, gravity from `motion.gravity`.
- [ ] HAL Zero: the same from `SysMon`, the motion sensor, the link, `peerWatch`; count lines
  spoken; refresh every 500 ms.
- [ ] `kHalVersion` → `0.5.0`.
- [ ] Verify: tests, three builds; sim `--show cards|wall|unit`; flash the CoreS3 and step
  with `N`, screenshot each; tilt check on `unit`; deploy to the Cardputer, `--show` + `shot`.
- [ ] Commit `Both HALs feed their real readings to the film-style screens`.

### Task 5: Lines

- [ ] Write 5 lines per screen per HAL (`content/lines/screens/{cards,wall,unit}.txt`,
  `content/new/lines/screens/…`); send to David; **wait for approval**.
- [ ] Render (`--only screens/<id>`, and `--model new`), copy the desk's to the card (only
  `/hal/voice/screens/`), push HAL Zero's (dry run first: zero deletions).
- [ ] Commit `Spoken lines for the film-style screens`.

### Task 6: Review and release v0.5.0

- [ ] Fresh whole-branch review (most capable model) with this plan's Review Focus; fix
  Critical/Important test-first; ledger the rest.
- [ ] README (the three screens), CHANGELOG `[0.5.0]`, TIMELOG checked with
  `python3 ~/.claude/scripts/claude-hours.py HAL9000`; fast-forward `main`; tag `v0.5.0`; push.
