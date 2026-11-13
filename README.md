# HAL 9000 desk companion

Firmware for an **M5Stack CoreS3** that turns it into a HAL 9000 companion for the
house: a glowing red eye that breathes, console screens when you tap it, a greeting
when you wave at him, complaints when you shake or move him, a press-and-hold shutdown that ends
in "Daisy Bell", and the occasional unprompted remark. Quiet hours keep him silent
at night.

HAL's voice is generated on your computer with [Piper](https://github.com/OHF-Voice/piper1-gpl)
and stored on the microSD card. The voice is
[HAL-9000-Piper-TTS](https://huggingface.co/campwill/HAL-9000-Piper-TTS), trained on
HAL's dialogue from the film and used as trained. It runs on your computer's CPU: no Ollama
or other AI server needed.
Every line is original, apart from the 1892 "Daisy Bell" chorus.

Since v0.3 he has a rival: **HAL Zero**, a newer, smugger HAL on the
**M5Stack Cardputer Zero** (a Raspberry Pi CM0 running Debian). The two talk to each
other over Wi-Fi in scripted conversations: see [HAL Zero](#hal-zero) below.

## See it

[![HAL ZERO on YouTube](https://img.youtube.com/vi/nb-oDZrERco/hqdefault.jpg)](https://youtu.be/nb-oDZrERco)

[HAL ZERO](https://youtu.be/nb-oDZrERco), on YouTube.

## Setting it up

**[docs/SETUP.md](docs/SETUP.md)** is the step-by-step guide: what you need, making HAL's
voice, preparing the SD card, flashing the CoreS3, and (optionally) installing HAL Zero on
a Cardputer Zero and linking the two. It is tested on macOS; Linux differences are noted
but not yet tried on a Linux PC; Windows isn't covered.

## What's in here

| Path | What it is |
|---|---|
| `src/` | Firmware glue for the CoreS3: screen, speaker, sensors, SD card, Wi-Fi time, link |
| `src/linux/` | HAL Zero: the Cardputer Zero program (also runs in a window on your computer) |
| `lib/hal_core/` | HAL's logic, with no hardware code, tested on your computer |
| `lib/hal_ui/` | The eye and console screens, shared by both HALs |
| `test/` | Unit tests for `lib/hal_core` (`pio test -e native`) |
| `content/lines/`, `content/new/lines/` | Everything the desk HAL and HAL Zero say, one line per row |
| `content/dialogues/` | The two HALs' scripted conversations |
| `tools/make_voice.py` | Turns the lines into WAV files laid out for the SD card and the Cardputer |
| `tools/export_dialogue.py` | A conversation as one stereo file plus subtitles, for posting |
| `tools/cardputer.sh` | Builds HAL Zero (Docker) and runs it on the Cardputer over SSH |
| `tools/fake_old_hal.py` | A stand-in desk HAL for testing HAL Zero without the CoreS3 |
| `brain/`, `tools/brain.sh` | The AI brain: a small server on your computer that answers as either HAL |
| `sd-template/hal/config.txt` | Settings file template for the SD card |
| `docs/SETUP.md` | Setting everything up from scratch |
| `docs/superpowers/` | Design specs and implementation plans |

## Using HAL

| Do this | HAL does |
|---|---|
| Tap the screen | A console screen and a joke; tap again for another. Some screens bring their own lines: the sensor array (his real light, motion, battery, temperature, Wi-Fi and memory readings), a chess game he always wins, his memory banks, and the comms antenna to HAL Zero |
| Wave a hand just above the top edge (by the camera) | Greets you for the time of day, at most every 10 minutes |
| Shake, pick up or tip him over | Complains, at most every 20 seconds |
| Hold the screen for 3 seconds | Pleads with you; let go and he's relieved, keep holding and he sings "Daisy Bell" while his memory banks go dark one by one, then he goes dark and restarts |
| Leave him alone | An unprompted remark every 20–60 minutes |
| Walk into his camera's view | Greets you like a wave (the room has to be still for 20 s first) |
| Tap until Visual Analysis comes up | A live camera view with a targeting overlay, and a line about what he "sees" |

From 22:00 to 07:30 he dims, stops greeting and talking on his own, and answers
taps and shakes at half volume. The proximity sensor sits behind the glass and only
sees a hand a few centimetres away, which is why greetings need a wave or the camera.

**Live status screens.** Three of the console screens are built the way the film's computer
displays work, and show HAL's real readings. Both HALs have them, each with his own lines:

- **Status cards:** one full-screen colour card per subsystem (power, light, motion,
  temperature, Wi-Fi, memory, link, camera and more) with a big three-letter code, the live
  value and a trace of recent values. A subsystem in alarm (battery under 20 % and not
  charging, chip over 70 °C, the link down, memory nearly full) gets a red, blinking card.
- **Monitor wall:** twelve live tiles at once; one at a time opens its graph.
- **Unit diagnostic:** a wireframe of the device itself that tilts when you tilt him, with
  each part (camera, speaker, motion sensor and so on) ticked or flagged.

**The camera.** HAL watches through it, and it shows on the Visual Analysis screen. Frames stay in HAL's memory:
nothing is saved or sent anywhere. Point him at the room, upright or leaning back a
little; lying flat he only sees the ceiling. Moving HAL never counts as someone
arriving. Settings in `config.txt`: `camera=on|off` and
`camera_motion_threshold` (default 20, meaning 4% of the picture has to change;
lower notices smaller movements).

## HAL Zero

HAL Zero is the same HAL brain, voices system and console screens, rebuilt for the
Cardputer Zero's 320×170 screen with a HUD: CPU temperature, Wi-Fi, link status,
the real camera (IMX219) and motion sensor (BMI270). He runs as a launcher app.

Building, installing and running him is in [docs/SETUP.md](docs/SETUP.md#6-hal-zero-on-the-cardputer-zero-optional).
He starts from M5Stack's launcher, which pauses while he has the screen and comes back
when he quits.

**Keys:** Space is HAL's "finger" (tap or hold, like the CoreS3's screen), Tab or
up/down opens the menu, D lists the conversations, Enter picks, Esc goes back (and
quits from the eye). With the AI brain paired, Enter on the eye screen opens a box to
type him a question, and holding Fn lets you say it instead. The arrows are F (up), X (down), Z (left) and C (right), with or
without Fn.

**His screens.** Besides the shared console screens and Visual Analysis, HAL Zero has
five of his own, each with its own lines: **diagnostics** of the desk HAL over the link
(his real uptime, memory, camera, lines spoken, mode, Wi-Fi and firmware, each with a
verdict; also in the menu as "Diagnose the old model"), a **benchmark** of the two,
a **system monitor** of the Cardputer itself, a **retirement countdown** for the old
model, and an **upgrade log**. `tools/cardputer.sh run --show diagnostics` opens one
straight after booting. His status cards and monitor wall show the Cardputer's own
readings (processor, memory, temperature, tilt) plus one for the desk HAL: whether the
old model is answering.

**His files** live in `~/hal` on the Cardputer, laid out like `/hal` on the card:
`config.txt`, `voice/`, `dialogues/`. Render his voice with
`tools/make_voice.py --model new` and copy it over with `--push-to pi@cardputerzero.local`.

## The two HALs talking

Set-up (turning the link on and copying each HAL's half of the conversations) is in
[docs/SETUP.md](docs/SETUP.md#7-let-the-two-hals-talk-optional). Then, on the Cardputer,
press D and pick a conversation. Tapping either HAL mid-way stops both, and HAL Zero
gloats; if the desk HAL doesn't answer, he says so. HAL Zero finds the desk HAL as
`hal9000-old.local` on the same Wi-Fi; `peer_host` and `link_port` in his settings change
that.

To post one: `tools/.venv/bin/python tools/export_dialogue.py introduction --mp3`
writes `export/dialogues/introduction.wav`, `.mp3` and `.srt` (desk HAL on the left,
HAL Zero on the right, timed like the live run). The voice comes from a model
trained on the film's audio, so whether to post it publicly is your call.

## The AI brain

Chat with either HAL, and make the two argue live about anything. A small program on your
computer does the thinking with an AI model that runs locally ([Ollama](https://ollama.com)),
speaks in each HAL's voice, and hands each HAL his lines. Every answer is cleaned down to one
short spoken line, and the film's famous lines are filtered out: the HALs only do original
material.

From the Cardputer:

- **Ask HAL Zero:** press **Enter**, type a question, press Enter again. He sends it to the
  brain along with his real readings (time, temperature, Wi-Fi, the link) and answers aloud
  with a subtitle. Ask him to show you a screen and he brings it up.
- **Or say it:** hold **Fn**, speak, let go. What he heard shows in the box for a moment
  (any key lets you correct it) and is then sent.
- **Ask the desk HAL:** press Tab in the box (or pick "Ask the desk unit..." in the menu). HAL
  Zero collects the desk HAL's own sensor readings, and the desk HAL downloads and speaks his
  answer.
- **Make them argue:** "Make them argue..." in the menu takes a subject, and the two go at it
  for eight lines, each written while the one before is being spoken.

From the computer, `tools/brain.sh ask zero "..."` and `ask desk "..."` ask either HAL.

Nothing you type or say leaves your computer with the local model. Claude or ChatGPT can
write the lines instead if you add your own API key: the questions and the HALs' readings then
go to that company, and recordings of your voice still never do.

Set-up is step 9 of [docs/SETUP.md](docs/SETUP.md#9-the-ai-brain-optional).

## Licence

The code and the original lines in this repository are under the [MIT licence](LICENSE).

This is an unofficial fan project. It is not affiliated with or endorsed by the makers or
owners of *2001: A Space Odyssey*. It contains no audio, images or dialogue from the film:
the screens and every line are original (apart from the 1892 song "Daisy Bell"). The HAL
voice is a separate third-party model that the voice tool downloads; it is not part of this
repository or covered by its licence.

