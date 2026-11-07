# Changelog

All notable changes to HAL are written down here.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and versions follow [Semantic Versioning](https://semver.org/).

## [Unreleased]

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
