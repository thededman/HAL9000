# The HAL family (v0.7): three more HALs join the conversation

Approved by David on 2026-11-13. Builds on the AI brain
(`2026-11-11-ai-brain-design.md`). Everything here is optional: the desk HAL and HAL Zero
work exactly as before without any of the newcomers.

## 1. Who they are

| Id | Name | Device | How he "speaks" |
|---|---|---|---|
| `zero` | HAL Zero | Cardputer Zero | voice (conducts; the only one who talks to the brain) |
| `desk` | HAL 9000 | CoreS3 | voice |
| `nano` | HAL Nano | M5StickC Plus2, on a watch band | chirps, with his words on his screen |
| `paper` | HAL Paper | M5Paper v1.x (e-ink) | writes: his words appear large on his page |
| `card` | HAL Card | original Cardputer | voice: the project's first voice ("Alan", from v0.1) |

The cast is fixed and numbered in that order (0 to 4). Personas (David approves the wording):
HAL Nano is the newest and smallest, eager and quick, proud of going everywhere on Dave's
wrist. HAL Paper is serene and deadpan, keeps the minutes and corrects the others from the
record, and knows he writes rather than speaks. HAL Card is the scrappy original pocket model
that HAL Zero replaced, with a grudge about it.

What each device can do decides the rest:

| | Screen | Sound | Memory | Storage | Input |
|---|---|---|---|---|---|
| StickC Plus2 | 135×240 colour | passive buzzer | 2 MB PSRAM | none | 2 buttons, IMU |
| Cardputer | 240×135 colour | I2S speaker | no PSRAM (about 320 KB) | microSD | G0 button (M5Unified doesn't drive the keyboard) |
| M5Paper | 540×960 e-ink, 16 greys | none | 8 MB PSRAM | microSD | touch, wheel |

## 2. How the five fit together

- **HAL Zero conducts**, as today. He opens one link connection per HAL present: the desk HAL
  plus up to three friends (four fixed slots). Friends need no key and no brain address.
- **Every friend is a small link server** speaking the desk HAL's protocol plus two messages.
- **The brain writes for whoever is present**, one line at a time, each naming its speaker.
- **A line is "said"** by playing a downloaded clip (desk, card), by chirping while the words
  show (nano), or by writing the words and holding them for a reading time (paper). All answer
  `PLAYING n <ms>` then `DONE n`, so the conductor waits the same way for each.
- **Every screen follows along**: as each line begins HAL Zero tells every friend who is
  speaking and what they say (subtitles on the small screens, the transcript on HAL Paper).
- Without HAL Zero nothing is said: the friends show an eye (HAL Paper his last page). The v3
  desk HAL only learns that a group talk is on at his own first line; accepted for now.

## 3. The link, version 4

`kLinkVersion` stays 3 (the desk HAL sends it and is not changed). Friends say
`kLinkVersionFriends = 4`. Two messages, sent only to and by v4 peers:

```
IAM <id>               a friend, straight after his HELLO.  id: nano | paper | card
NOW <n> <who> <text>   HAL Zero, to every v4 peer as line n begins.
                       n 1..99; who: zero|desk|nano|paper|card; text: 1..150 printable ASCII
```

- A peer that sends no `IAM` is the desk HAL. The `peer_host` slot must be the desk HAL; each
  id is accepted once.
- `NOW` is at most 163 bytes, so a v4 friend's line limit is 256 (`LineSplitter` and
  `LinkGuard` take a `maxLine`). It is parsed from the raw line so spacing survives. A v3 peer
  is never sent one and would drop it harmlessly. HAL Zero's inbound limit stays 128 (nothing a
  friend sends is longer). The flood guard's budget (24 lines or 1024 bytes a second) leaves
  room: HAL Zero's worst burst to one friend is about 280 bytes.
- A text HAL gets `NOW n who text` then `SAY live n`; with no matching `NOW` he answers
  `MISSING n`. A voiced HAL gets `FETCH` → `READY` → `SAY live n` as today.
- A tap on any friend sends `END interrupted`, which ends the talk for everyone.

## 4. Shared logic (`lib/hal_core`)

- `hal_cast.h`: `kCastSize = 5`, `kEveryone = -1`, `kPair`, `castId`, `castName`,
  `castFromId`, `castVoiced` (zero, desk, card).
- `DialogueLine` gains `int peer = 1` (last field) and `speaker()`. No new script tags: scripts
  stay between the desk HAL and HAL Zero.
- `Conductor`: `start`/`startLive` take a `present` mask (default the pair); `append(int who,
  text)`; `onRemote(m, now, from = 1)`, `onDisconnected(now, from = 1)`; `leave(who, reason,
  now)`. `ConductorOut` gains `to` (a cast number or `kEveryone`), `started` (line n has just
  begun: the glue broadcasts `NOW`) and `left`. `PLAYING`, `DONE`, `MISSING` and `BUSY` count
  only from the line's owner. BUSY, a timeout or a hang-up makes that HAL leave: with two or
  more left the talk goes on (his lines are skipped), otherwise it ends as it does today.
  `END` from anyone ends it for all. With the defaults every existing test holds.
- `LiveFeed`: `onLine(int who, bool voiced, …)` (a text line is playable at once),
  `onRemote(m, from = 1)`, `onGone(who)`. `FeedOut` gains `to`, `peer`, `n`, `last`. A lost line
  gives its number back, so the feed and the conductor never disagree about numbering.
- `parseBrainReply` accepts text without a clip only when `who` is a text member.
  `castFacts` gives each member a share of the brain's 24-fact limit. Setting
  `friends=host,host,host`.
- Friend logic, pure: `hal_friend` (what a friend answers to each message), `hal_chirp` (a
  chirp pattern and its length from the words), `hal_minutes` (HAL Paper's transcript: wrapping
  and pages), `parseClipWav` (8- or 16-bit PCM; `parseWav` stays as pinned) with an 8-bit
  loudness envelope. Reading and chirp times have floors (about 2.5 s and 4 s) so short text
  lines never outrun the brain.

## 5. The brain

- A **cast registry**: id → name, persona, kind (`voice`/`text`), longest line (150 for zero
  and desk, 110 card, 100 paper, 80 nano), voice (model, treatment, clip format), own-words
  line. The two existing HALs' prompts stay byte-for-byte the same.
- **Conversations**: `POST /debate?cast=zero,desk,nano,…` (absent = the pair). A seedable
  `plan_turns`: random opener, never the same speaker twice running, whoever has spoken least
  goes next; about two lines each (4 to 10). With more than two speakers the talk so far goes
  to the model as one labelled transcript. Arguments give each extra HAL a side. Kind `roll`:
  one line each in cast order.
- **Voices**: `Treatment` gains a model, a sample rate and a codec. HAL Card is the Alan model
  with a treatment close to v0.1's, rendered unsigned 8-bit mono at 11,025 Hz (David's choice by
  ear). If a clip is larger than HAL Card can hold (96 KB: 98,304 bytes) the brain says a
  short stock line of his instead. Text members get no audio and no `clip` in the reply.
- `/ask?who=` accepts any cast id.

## 6. HAL Zero

- A four-slot peer table (one `LinkClient` each, with its own version, id, status, sensors,
  watch and liveness) in place of today's single set. The move is mechanical first: logs
  against every stand-in mode must match before and after.
- Sensing before a talk: "everyone present answered, or 1 s". Chats of their own, "Let them
  chat" and "Make them argue" include whoever is present; the ask box cycles through them;
  subtitles and banners name the speaker; "Roll call" in the menu.
- `askNextLine()` is unchanged: one line in the air, one ready, the brain one beyond. No HAL
  speaks twice running, so each device holds at most one line.

## 7. The friends' firmware

- One firmware in `src/friend/` (`common/` plus `nano/`, `card/`, `paper/`; no `#ifdef`s) and
  three environments: `nano` (`m5stick-c` with `board_build.variant = m5stack_stickc_plus2`
  and the 8 MB flash layout), `card` (`m5stack-stamps3`,
  `-DARDUINO_USB_CDC_ON_BOOT=1`), `paper` (`m5stack_paper`). They compile the desk HAL's
  `link_server.cpp`, `net_time.cpp` and `hal_log_arduino.cpp` in place; `LinkServer::begin`
  gains defaulted arguments (mDNS name, id, version, line limit). `cores3` changes by one line
  (`-<friend/>` in its source filter) and must build to the same size.
- **Screens** are new, simple drawing code in `lib/hal_ui` (an eye, the speaker's name, the
  words), with layouts for 135×240, 240×135 and the e-ink page, so the Mac renders previews
  (`--shots --layout nano|card|paper`) before anything is flashed.
- **Wi-Fi**: the settings text is kept in the device's flash and read by `hal::parseConfig`.
  `tools/friend_wifi.py` sets it over USB, prompting for the network name and password
  (hidden; never echoed, logged or stored in the repo). mDNS names `hal-nano`, `hal-card`,
  `hal-paper`.
- **HAL Nano**: eye and his words; chirps from the text; button A is his tap (when idle: a
  chirp); a sleep rule set by the battery check.
- **HAL Card**: eye and subtitles on an 8-bit screen buffer; one fixed clip buffer (96 KB,
  reserved at start before Wi-Fi: see section 8); a fetcher built from the pure pieces (`httpRequest`, `parseHttpHead`,
  `sameSubnet`); `M5.Speaker.playRaw` with 8-bit data; a `FETCH` while he is speaking answers
  `MISSING`; G0 is his tap. Keyboard later.
- **HAL Paper**: a 4-bit page in PSRAM (the same page the simulator renders). Each new
  transcript entry is pushed as its own rectangle in `epd_fast` (the mode is set before
  drawing: it decides how pixels are reduced); the grey eye, his own large line and page
  turns use `epd_quality`, with a full refresh every ten entries or so. `PLAYING` is sent
  before drawing (a refresh can hold the loop for half a second; the conductor waits two).
  Touch or the wheel is his tap (when idle: a full refresh).

## 8. Checks before any format is frozen (with David)

Done on 2026-11-13 with throwaway test programs (kept outside the repository):

- **Cardputer.** Flashes over USB with no button held; M5Unified recognises it; USB serial
  works with `-DARDUINO_USB_CDC_ON_BOOT=1`. Memory, with Wi-Fi started and a clip playing:

  | Clip buffer | Screen buffer | Free while speaking |
  |---|---|---|
  | 128 KB | 240×135, 16-bit (65 KB) | 36 KB: too little once Wi-Fi is joined and sockets are open |
  | **96 KB** | **240×135, 8-bit (32 KB)** | **102 KB** |

  So HAL Card gets a 96 KB clip buffer (reserved first, while memory is in one piece) and an
  8-bit screen buffer. A 79-character line in the Alan voice is 6.0 s: 66 KB at 11,025 Hz,
  48 KB at 8,000 Hz. 96 KB holds 8.7 s at 11,025 Hz or 12 s at 8,000 Hz. `playRaw` plays the
  unsigned 8-bit clip as it is.
- **StickC Plus2.** The installed board file `m5stick-c` names a pin folder that has been
  renamed: the env needs `board_build.variant = m5stack_stickc_plus2`. The test program fills
  87 % of the 4 MB layout's program slot, so the real build uses the 8 MB layout. Chirps at one
  short tone a letter take about 64 ms a character (37 characters in 2.4 s). Both buttons and
  the battery reading work. Still to do: drain over 30 minutes on battery with Wi-Fi on.
- **M5Paper.** A whole page in `epd_quality`: 0.67 s. One transcript entry (two lines of text)
  in `epd_fast`: 0.26 s, ten in a row all between 0.25 and 0.27 s. His own line, large, in
  `epd_quality`: 0.48 s. Touch works. All far inside the conductor's 2 s wait for `PLAYING`.
  Ghosting after ten fast entries: for David to judge.
- **HAL Card's voice**: David listened to both on the Cardputer and chose **11,025 Hz**
  (2026-11-13). At that rate his 96 KB holds 8.9 s, about 115 characters of the Alan voice, so
  his lines are capped at 110 characters and the brain's oversize fallback covers the rest.

## 9. Testing

- Native tests for everything in section 4; the brain's and tools' suites for section 5.
- `tools/fake_old_hal.py --as desk|nano|paper|card`, several copies on different ports; the
  simulator takes `--friends`. Scenarios: a five-way chat; a stand-in killed mid-chat; a busy
  HAL Paper; Space stops everyone; a 150-character line reaches text friends whole; the
  stand-in desk HAL is never sent `NOW`.
- Devices, with David: each friend alone with HAL Zero (seen, says a line, a tap stops it),
  then all five: a chat they start themselves, an argument, a roll call. A five-way, ten-line
  chat should take 75 to 90 seconds.

## 10. Build order

0. v0.6.1 and the link hardening (done 2026-11-13, awaiting the devices and the tag).
1. This write-up; the checks in section 8.
2. The brain's cast (in its own session).
3. Shared logic. 4. HAL Zero in the simulator, then on the Cardputer Zero.
5. The friends: HAL Nano, HAL Card, HAL Paper.
6. All five together, the guide, an independent review, and **one release, v0.7.0**.
