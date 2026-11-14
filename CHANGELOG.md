# Changelog

All notable changes to HAL are written down here.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and versions follow [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added
- **Friends for the HALs (optional, in progress).** Up to three more M5Stack devices can join
  the conversations as HALs of their own: HAL Nano (an M5StickC Plus2, who chirps while his
  words show on his screen), HAL Paper (an M5Paper, who writes) and HAL Card (the original
  Cardputer, who speaks with the project's first voice). HAL Zero conducts whoever is there,
  and the AI brain writes each one's lines in his own character. So far HAL Nano has firmware
  (`pio run -e nano -t upload`); the other two are to come.
- A **roll call** in HAL Zero's menu (when friends are set up): every HAL present says a line.
- Chats, arguments and questions include the friends: "Let them chat" and "Make them argue"
  bring in everyone present, TAB in the ask box goes round them, and each HAL's subtitle has
  his name in his own colour. A HAL who is busy or drops out leaves the conversation and the
  rest carry on; a tap on any of them still stops it for all.
- `tools/friend_wifi.py` gives a friend your Wi-Fi network over USB. You type the name and
  password; the password is not shown, not saved on your computer, and the friend never
  repeats it.
- For trying a group without the devices: `tools/fake_old_hal.py --as nano|paper|card`, and
  the simulator's `--friends`, `--roll`, `--ask-who` and `--shots DIR --layout nano`.

## [0.6.1] - 2026-11-14

### Fixed
- A chat the HALs start by themselves no longer makes a fuss when it can't go ahead: if the
  desk HAL is busy, or hasn't said how he is, HAL Zero drops the idea without a banner or a
  gloating remark, and never cuts into what the desk HAL is doing.
- Pressing Enter or holding Fn while HAL Zero is still thinking up one of those chats now gets
  his attention at once instead of being refused, and an Esc that lands just after a talk has
  ended no longer quits him.
- `chat_min_minutes` on its own, set above the usual maximum, was ignored (both settings fell
  back to 20 and 40). The maximum now rises to meet it.
- The HALs don't strike up a chat the very minute quiet hours end.
- HAL Zero remembers where the brain was. For a few minutes after your computer wakes, or
  after either end restarts, its name often can't be looked up on the network; he now uses
  the address that last worked instead of saying his higher functions are offline.
- The desk HAL tries twice to reach the brain for a line before giving up, so the first
  conversation after your computer wakes no longer breaks off with "the desk unit lost the
  thread".
- `tools/brain.sh autostart off` works even after the voice tool has been removed, `autostart`
  says when the login item points at a folder that has since moved, and starting at login
  allows the brain a minute to come up.
- A `brain/subjects.txt` saved with a byte-order mark no longer turns its first note into a
  subject.
- The desk HAL can no longer be frozen over the link. With `link=on`, anything on your network
  that connected to him and poured data in, or stopped listening to his answers, could hold his
  eye and touch screen still for seconds at a time and use up his memory. He now takes only a
  little from the link on each pass, never waits for the other end, and hangs up on anything
  that sends far more than HAL Zero ever does (over 24 lines or 1024 bytes in a second),
  letting nobody back in for 3 seconds. HAL Zero himself notices no difference.
- HAL Zero is protected the same way: if whatever answers at the desk HAL's address pours data
  at him, he hangs up and tries again a few seconds later, instead of freezing and running out
  of memory.

## [0.6.0] - 2026-11-13

### Added
- The AI brain: a small server on your computer (`tools/brain.sh start`) that answers as
  either HAL in his voice, using a model that runs locally through Ollama. It can also turn a
  recording into text and run a live argument between the two HALs.
- Typed chat with HAL Zero: press Enter on the Cardputer, type a question, and he answers
  aloud with a subtitle, using his real readings. Asking for a screen brings it up. If the
  brain can't be reached he says so (five new lines). `tools/brain.sh pair` connects the two.
- Speaking to HAL Zero: hold Fn, ask, let go. What he heard appears in the ask box for a
  moment before it is sent, so a mishearing can be corrected. A key click or a knock during
  the recording doesn't spoil it, and he says nothing of his own while he listens.
- Questions for the desk HAL from the Cardputer (Tab in the ask box): he downloads the
  brain's answer himself and speaks it, using his own sensor readings.
- Live arguments: "Make them argue..." in HAL Zero's menu takes a subject and the two HALs
  argue it out for eight lines. A tap on either stops them.
- Chats of their own: every 20 to 40 minutes, when both HALs are at rest and it isn't quiet
  hours, the two strike up a short conversation nobody asked for, on a subject drawn at random
  from `brain/subjects.txt` (yours to edit). "Let them chat" in HAL Zero's menu starts one at
  once; `chat=off` in his settings stops them, and `chat_min_minutes` / `chat_max_minutes` set
  the gap.
- `tools/brain.sh autostart on` starts the brain whenever you log in (macOS), so a restart of
  the computer no longer leaves HAL Zero saying his higher functions are offline.
- `tools/brain.sh ask zero "..."` (or `ask desk`) asks from the computer; the simulator takes
  `--brain HOST`, `--ask`, `--ask-desk`, `--argue`, `--chat`, `--hear FILE` and
  `--listen SECONDS`; `--keys` shows the code of every key pressed.
- A guard that keeps the film's best-known lines out of anything the AI says.
- Experimental: Claude or ChatGPT can write the HALs' lines instead of the local model, with
  your own API key in `brain/.env`. The setup guide lists what is sent when you turn this on
  (questions and readings; never recordings). Not yet run against the live services.

### Changed
- The two HALs speak link version 3 (the desk HAL fetches and speaks AI lines, and reports his
  sensors). Older firmware on either side keeps working, without the AI features.
- On HAL Zero, Esc during a conversation now stops it, as Space does. Esc quits from the quiet
  eye only.
- A script can no longer be named `live`: on the link that name means the AI brain's lines.

### Fixed
- `tools/cardputer.sh deploy` could replace HAL Zero's settings with the template if the
  connection failed at the wrong moment. The Cardputer now decides in one step.
- The desk HAL could go on believing he was in a conversation (no greetings or idle remarks)
  when a line from HAL Zero crossed his own "stop" after a tap.
- Subtitles on HAL Zero lost their last word or two when a full-length line didn't break
  neatly into three rows.

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
