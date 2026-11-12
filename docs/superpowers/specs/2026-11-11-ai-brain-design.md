# The AI brain — design (v0.6)

Date: 2026-11-11 · Owner: David · Status: approved (plan mode, 2026-11-10), written up after v0.5.0
Builds on: the link and `Conductor` (v0.3), the screen catalogue and link v2 (v0.4), the
film-style screens (v0.5)

## Purpose

David wants "a fun, interactive way to deal with real AI". His choices:

- **Brain:** runs on his own computer (a local model), with Claude or ChatGPT as an optional
  add-on.
- **Talking:** type on the Cardputer, or hold a key and speak to HAL Zero.
- **Features:** chat with either HAL; the two HALs argue live about a topic; answers that use
  real facts (the HALs' own readings).

**Constraints:**

- Neither device can run a model: the Cardputer has 352 MB of RAM, the CoreS3 is a
  microcontroller. A small server on the computer does the thinking, speaking and listening.
- Every generated line is original: no film quotes, and nothing but speech and a screen id ever
  comes back to a device.
- No voice cloning tools. The voice is the same Piper model the rest of the project uses.
- Cloud keys are typed by David into a git-ignored file; nothing in the repo or the logs holds
  or prints a key.
- The repo is public: the set-up goes into `docs/SETUP.md` in the same change, for macOS
  (tested) and Linux.

**Out of scope:** running a model on a device; wake words (push-to-talk only); remembering
conversations across restarts; the desk HAL talking to the brain by himself.

## How it fits together

```
                 text + facts  ┌──────────────────────────────┐
  HAL Zero  ─────────────────▶ │ brain server (the computer)  │ ──▶ local model (Ollama)
 (Cardputer) ◀── text, clip id │  persona · voice · listening │     or Claude / ChatGPT
      │  ▲                     └──────────────────────────────┘
      │  └── GET /clip/<id> (his own lines)        ▲
      │ link: FETCH n <address> <port> <id>        │ GET /clip/<id> (the desk HAL's lines)
      ▼                                            │
  desk HAL (CoreS3) ───────────────────────────────┘
```

- **HAL Zero asks the questions.** He is the only one who sends anything to the brain.
- **Audio is fetched, not pushed.** A reply is text plus a short-lived clip id. Each HAL
  downloads its own clips, so the link between the HALs stays small text messages: pings keep
  flowing and a tap can interrupt at any moment.
- **One mechanism for everything spoken:** a "live dialogue" whose lines arrive as they are
  generated. A chat answer is a live dialogue of one line; an argument is one of many.

## 1. The brain server (`brain/`, Python, port 9100)

Standard library only, run with the voice tool's environment (`tools/.venv`, which has Piper).

**Requests.** `Content-Length` is required on every POST. Every request except `GET /clip/…`
and `GET /health` must carry the shared key in `X-HAL-Key`.

| Request | Body | Reply (200) |
|---|---|---|
| `GET /health` | | `ok=1` |
| `POST /ask?who=zero\|desk` | facts, a blank line, the question (text, ≤ 4 KB) | `text=`, `clip=`, `ms=`, and `show=` when a screen was asked for |
| `POST /hear` | a WAV: PCM 16-bit mono 16 kHz, ≤ 1 MB | `heard=` (may be empty) |
| `POST /debate?turns=N` | facts, a blank line, the topic (≤ 4 KB); N is 2–12, default 8 | `debate=<id>` |
| `GET /debate/<id>/<n>` | | line n: `who=`, `text=`, `clip=`, `ms=`, `last=0\|1` |
| `POST /debate/<id>/stop` | (empty) | `ok=1` |
| `GET /clip/<token>` | | the WAV (`audio/wav`), 22 050 Hz mono 16-bit like every HAL clip |

- **Replies** are `text/plain`, one `key=value` per line, so the C++ side needs no JSON parser.
  Values never contain a line break. Clients ignore keys they don't know.
- **Errors** are an HTTP status and one line, `error=<code>`: 401 `bad-key`, 400 `bad-request`
  or `bad-audio`, 404 `not-found`, 411 `no-length`, 413 `too-large`, 503 `no-model`, `no-voice`
  or `no-ears`.
- **`GET /debate/<id>/<n>`** waits up to 20 s for line n. Still not ready: 202 with `wait=1`
  (ask again). Past the last line: 204. Line 0 is HAL Zero's, then they alternate.
- **Clip tokens** are 32 random hex characters, good for 10 minutes. No key is needed to fetch
  one, so the desk HAL needs no settings.
- **Listening address:** with a key set, all interfaces; with no key, this computer only
  (`127.0.0.1`), so an unpaired server is never open to the network.
- The server runs only while David has started it (`tools/brain.sh start`); it is not a
  login item.

## 2. Personas, facts and screens on request (`brain/persona.py`)

- **HAL 9000** (`who=desk`): calm, polite, dry. **HAL Zero** (`who=zero`): the smug pocket-sized
  upgrade who looks down on "the old model". Both call the user Dave.
- **Rules in every prompt:** 1–2 short sentences, at most **150 characters** (what the
  subtitles hold), plain text, no film quotes, stay in character.
- **Facts** ("things you know right now") come from HAL Zero with each request, one
  `name: value` per line: the time, his readings, the link, and for the desk HAL his status and
  sensors. The prompt says to use a fact only when the question is about it (the local model
  otherwise recites them in every answer).
- **Screens on request:** when Dave asks to see something, the reply may end with `[SHOW id]`.
  The server strips it and returns `show=<id>` only if the id is one of that HAL's screens:
  - both: `vitals readout trajectory analyser globe cards wall unit visual`
  - desk: `sensors chess memory comms`
  - zero: `diagnostics benchmark sysmon retirement upgrade`
- **The cleaner** makes whatever the model says speakable: drops thinking blocks, markdown,
  emoji, stage directions and surrounding quotes; maps curly quotes and dashes to plain ones and
  drops any other non-ASCII; collapses spaces; cuts to 150 characters at the last sentence end
  (or the last word, closed with a full stop). Nothing left: a stock line,
  "I have nothing to say to that, Dave."

## 3. Voice and listening

- **Voice** (`brain/voice.py`): reuses `tools/make_voice.py` (`load_voice`, `say`, the desk
  treatment, HAL Zero's treatment A). The model stays loaded; one line is rendered at a time.
- **Listening** (`brain/stt.py`): `whisper-cli` with `brain/models/ggml-small.en.bin` (the
  488 MB download David approved). Bracketed noises such as `[BLANK_AUDIO]` are removed.
- **Clips** (`brain/clips.py`): kept in memory, at most 64 or 40 MB, oldest dropped first.

## 4. Memory and arguments

- **Chat memory** (`brain/memory.py`): the last 12 messages per HAL, forgotten after 10 idle
  minutes and when the server stops.
- **Arguments** (`brain/debate.py`): the topic and the lines so far go to each HAL in turn
  ("reply to his last point"); the final line is told it is the last word. Lines are generated
  at most one ahead of the line the device has asked for, so an interrupted argument wastes
  one line. A debate nobody asks about for 2 minutes stops; it is forgotten after 10.

## 5. Models (`brain/providers/`)

- **`ollama`** (default): `qwen3.6:35b-a3b` on `127.0.0.1:11434`, thinking off, at most 80
  tokens, kept loaded for 30 minutes and warmed when the server starts. Measured 2026-11-11:
  0.4–1.0 s per reply warm, about 13 s for the first one after loading.
- **`claude`**, **`openai`** (optional, v0.6's last step): only when `brain/.env` has that
  key and names the provider. What leaves the house then is the question and the facts,
  never audio.
- One generation runs at a time; a second request waits.

## 6. Settings and tools

- **`brain/.env`** (git-ignored; `brain/env.example` is the template): `HAL_BRAIN_KEY`,
  `HAL_BRAIN_PORT` (9100), `HAL_BRAIN_PROVIDER` (ollama), `HAL_BRAIN_MODEL`, and the optional
  cloud keys.
- **`tools/brain.sh`:** `start`, `stop`, `status`, `ask WHO "QUESTION"` (prints the reply and
  plays it on the computer), `pair [user@host]` (makes the shared key if there is none and
  writes `brain=on`, `brain_host`, `brain_port`, `brain_key` into HAL Zero's `~/hal/config.txt`
  over SSH, without printing the key).

## 7. The devices (planned separately)

- **Pure logic first** (`lib/hal_core`): live dialogues in `Conductor` (`startLive`, `append`,
  `close`, a `WaitLine` state with a 30 s backstop); link v3 (`FETCH n ipv4 port token` →
  `READY n` / `MISSING n`, `SAY live n`, `SENSE` → `SENSORS …`; private addresses only, port
  ≥ 1024, token 16–32 hex); a key map, a text box (120 characters), an HTTP request builder and
  `key=value` parser; settings `brain`, `brain_host`, `brain_port`, `brain_key`.
- **Typed chat with HAL Zero:** Enter opens `> _`; a worker thread talks to the brain; the
  answer plays from memory with a subtitle; `BRAIN THINKING` / `BRAIN OFFLINE` on the HUD.
- **The desk HAL speaks AI lines:** he downloads the clip himself in small slices so the eye
  keeps moving, and plays it from PSRAM.
- **Arguments:** "Make them argue…" in HAL Zero's menu; he conducts, adding each line once it
  is playable. A tap on either HAL still stops both.
- **Speaking:** hold a key to talk (not Space); 16 kHz mono, 15 s at most; the screen shows
  what he heard, then the answer plays.

## 8. When things go wrong

- **Brain off, computer asleep, wrong key, timeout:** HAL Zero plays a pre-rendered line from
  `/hal/voice/brain/` and shows `BRAIN OFFLINE`. Typed chat never blocks his eye or keys.
- **The model is down or slow:** the server answers 503 within 25 s and stays up.
- **The model says something unusable:** the cleaner's stock line.
- **Nothing was heard:** `heard=` is empty and HAL Zero says he didn't catch that.

## 9. Privacy and safety

- An open port on the computer: the shared key on everything that costs anything, size caps
  on every body, and only speech and a whitelisted screen id ever come back.
- The key and any cloud keys live in `brain/.env` (git-ignored, mode 600) and are never logged.
- The desk HAL fetches from an address a link peer names: private addresses only, a strict
  token, a size cap, and it must parse as a WAV. The link is off by default.
- Local model: nothing leaves the house. Cloud option: the question and the facts do.

## 10. Testing

- `brain/test_*.py` (unittest, no model, voice or network needed: fakes stand in): the
  cleaner, the `[SHOW]` whitelist, request parsing, the key check and size caps, clip expiry,
  chat memory, debate turn order, one-ahead generation, stopping and ending.
- On the computer: `tools/brain.sh ask zero "…"` plays an in-character reply; a spoken clip
  made by the voice goes through `/hear` and comes back as the same words.
- Devices: the simulator chats with the real brain and argues against `fake_old_hal.py` before
  either device is touched.

## 11. Build order

1. **The brain server** (this spec's sections 1–6; plan `2026-11-11-ai-brain-server.md`).
2. **The devices:** pure logic, typed chat, the desk HAL's clips, arguments, speaking (own plan).
3. **The cloud option and the release** (v0.6.0).
