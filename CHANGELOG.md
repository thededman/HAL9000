# Changelog

All notable changes to HAL are written down here.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and versions follow [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added
- The AI brain: a small server on your computer (`tools/brain.sh start`) that answers as
  either HAL in his voice, using a model that runs locally through Ollama. It can also turn a
  recording into text and run a live argument between the two HALs.
- Typed chat with HAL Zero: press Enter on the Cardputer, type a question, and he answers
  aloud with a subtitle, using his real readings. Asking for a screen brings it up. If the
  brain can't be reached he says so (five new lines). `tools/brain.sh pair` connects the two.
- Questions for the desk HAL from the Cardputer (Tab in the ask box): he downloads the
  brain's answer himself and speaks it, using his own sensor readings.
- Live arguments: "Make them argue..." in HAL Zero's menu takes a subject and the two HALs
  argue it out for eight lines. A tap on either stops them.
- `tools/brain.sh ask zero "..."` (or `ask desk`) asks from the computer; the simulator takes
  `--brain HOST` and `--ask "QUESTION"`; `--keys` shows the code of every key pressed.
- A guard that keeps the film's best-known lines out of anything the AI says.

### Changed
- The two HALs speak link version 3 (the desk HAL fetches and speaks AI lines, and reports his
  sensors). Older firmware on either side keeps working, without the AI features.

## [0.5.0] - 2026-11-11

Film-style screens showing each HAL's real readings, and a setup guide for newcomers.

### Added
- Three film-style screens on both HALs, showing each HAL's real readings, each with its
  own lines:
  - **status cards:** one full-screen colour card per subsystem with its live value and
    a trace of recent values; a subsystem in alarm gets a red, blinking card;
  - a **monitor wall** of twelve live tiles;
  - a **unit diagnostic:** a wireframe of the device itself that tilts when you tilt
    him, with each part ticked or flagged.
- 30 new spoken lines (15 for each HAL).
- `docs/SETUP.md`: a step-by-step guide to setting everything up from scratch, for macOS
  and Linux.
- An MIT licence, and a note that this is an unofficial fan project.

### Changed
- `tools/cardputer.sh build` always builds for the Cardputer's arm64 chip, so it also
  works from Intel and AMD computers (emulated).
- The voice tool's audition plays through `aplay` or `paplay` on Linux.

### Fixed
- The desk HAL's uptime (also shown in HAL Zero's diagnostics) no longer starts again
  from zero after 49.7 days; the same for HAL Zero's own.

## [0.4.1] - 2026-11-10

Small fixes from the v0.4.0 review.

### Fixed
- HAL Zero's system monitor shows Wi-Fi as OFF (and a gap in the graph) when Wi-Fi
  drops, instead of repeating the last signal strength; the same for the temperature.
- The desk HAL's comms screen with `link=off` says NO CARRIER and adds LINK DISABLED;
  its signal waves no longer cross the labels.
- The desk HAL reads its light sensor and battery only while the sensor array is
  showing (the USB status log shows the last readings).
- `tools/cardputer.sh deploy` swaps the new program in only once it has fully copied.

## [0.4.0] - 2026-11-10

More screens on both HALs, and HAL Zero can diagnose the old model.

### Added
- Four new console screens on the desk HAL, each with its own lines: a **sensor array**
  with his real readings (room light, proximity, tilt, motion, battery, chip
  temperature, Wi-Fi, memory), a **chess** game he always wins (checkmate lands as he
  finishes his line), his **memory banks**, and a **comms** antenna showing the link to
  HAL Zero.
- During "Daisy Bell" the desk HAL's memory banks go dark one by one.
- Five new screens on HAL Zero, each with its own lines: **diagnostics** of the desk HAL
  over Wi-Fi with a verdict on every number (also in the menu as "Diagnose the old
  model"), a **benchmark** of the two, a **system monitor** of the Cardputer, a
  **retirement countdown** for the old model, and an **upgrade log**.
- 45 new spoken lines (20 for the desk HAL, 25 for HAL Zero).
- `hal9000 --show SCREEN` opens a screen once HAL Zero has booted; `--shots DIR
  --layout cores3` previews the desk HAL's screens on the Mac.

### Changed
- The two HALs speak link version 2. A v0.3 desk HAL still talks to HAL Zero; HAL Zero
  just reports its firmware as too old to diagnose.
- HAL Zero notices within seconds when the desk HAL loses power or goes quiet (and
  gloats about it), then reconnects when he's back. The desk HAL's comms screen stops
  showing HAL Zero once he's gone.
- The desk HAL's light sensor is read at a higher gain: it sits behind dark glass.

## [0.3.0] - 2026-11-10

HAL gets a rival.

### Added
- **HAL Zero**, a newer and smugger HAL for the M5Stack Cardputer Zero, launched from
  M5Stack's own launcher. Same eye and console screens, laid out for the 320×170
  screen, with a HUD (uptime, CPU temperature, Wi-Fi, link, camera, tilt, smugness), a
  boot sequence, a menu, and keyboard controls (F X Z C are the arrows).
- HAL Zero sees through the Cardputer's camera (Visual Analysis) and feels its motion
  sensor (shake and pick-up complaints, tilt on the HUD).
- **The two HALs talk to each other** over Wi-Fi: three scripted conversations ("The
  introduction", "Who's the better model", "Dave walks in"), each line in its own
  HAL's voice, in turn. Tap either HAL to stop them; HAL Zero gloats when the desk
  HAL doesn't answer. Turn it on with `link=on` on both.
- HAL Zero's own lines: every situation the desk HAL has, plus rival remarks.
- `tools/export_dialogue.py`: a conversation as one stereo WAV/MP3 with subtitles, for
  posting.

### Changed
- HAL sounds like HAL. All 105 lines are re-recorded in a voice trained on HAL 9000's
  dialogue from the film, replacing the "Alan" voice. Copy them onto the card with
  `tools/make_voice.py --copy-to /Volumes/HAL`.
- "Daisy Bell" now starts in HAL's normal voice, then each verse is slower and deeper.
- The banner above the eye no longer says "HAL 9000". Put your own image on the card as
  `/hal/banner.png` (or `.jpg`, up to 320×38) and HAL shows it there, centred; without
  one the space above the eye stays black (no grey plate).

### Removed
- The live camera view beside the eye. The camera now shows only on the Visual Analysis
  screen (tap until it comes up), and still notices people walking in.
  `camera_preview` in `config.txt` is ignored.

## [0.2.0] - 2026-11-08

HAL can see.

### Added
- A tiny live camera view beside the eye. Frames stay in HAL's memory: nothing is
  saved or sent anywhere. Hide it with `camera_preview=off`.
- HAL notices someone walking into view and greets them like a wave (the room has
  to be still for 20 seconds first; quiet hours still apply). Picking HAL up or
  tilting him never counts as someone arriving.
- A sixth console screen, "Visual Analysis": the live camera view with a targeting
  overlay, while HAL says one of 20 new lines about what he sees.
- Settings: `camera=on|off`, `camera_preview=on|off`, `camera_motion_threshold`.

## [0.1.0] - 2026-11-06

HAL's first night on the shelf.

### Added
- HAL's red eye on the CoreS3 screen: it breathes slowly and glows in time with his voice.
- Tap the screen for one of five console screens (crew vitals, systems readout,
  couch-to-kitchen trajectory, vocal analysis, surveillance globe) while HAL tells a joke.
  Tap again to skip to another.
- Wave a hand just above the top edge of the screen and HAL greets you for the
  time of day (morning, afternoon or evening).
- Shake him, pick him up or tip him over and he complains.
- Hold the screen for 3 seconds and he pleads with you. Let go and he's relieved;
  keep holding and he sings "Daisy Bell", slower and deeper, fades out, and comes
  back remembering everything.
- Unprompted remarks every 20 to 60 minutes.
- Quiet hours (22:00–07:30): dimmer screen, no greetings or remarks, half volume.
- 85 voice clips in a calm, slow synthetic voice (Piper "Alan"), all original lines
  apart from the 1892 "Daisy Bell" chorus. No line repeats until the rest have played.
- Settings in `/hal/config.txt` on the SD card: Wi-Fi, time zone, quiet hours,
  volume, wave sensitivity, cooldowns.
- Clock set over Wi-Fi at startup and every night at 03:00; Wi-Fi is off otherwise.
- Your own clips (`--mine-from`) and your own 320×240 JPGs (`/hal/screens/`) join
  the rotation.
- Without an SD card HAL still shows his eye and says he can't find his memory.
