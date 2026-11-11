# Changelog

All notable changes to HAL are written down here.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and versions follow [Semantic Versioning](https://semver.org/).

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
