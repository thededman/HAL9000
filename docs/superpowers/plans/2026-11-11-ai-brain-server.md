# AI Brain Server Implementation Plan (v0.6, part 1)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A small server on the computer that answers as either HAL, in his voice, from typed or
spoken questions, and runs live arguments between the two; testable with `tools/brain.sh ask`
before any device work.

**Architecture:** A `brain/` Python package: pure pieces (persona and cleaner, clip store, chat
memory, debate) around three thin wrappers (model, voice, ears), behind a standard-library HTTP
server that speaks `key=value` text. Fakes stand in for the wrappers in tests.

**Tech Stack:** Python 3.10+ standard library, run with `tools/.venv` (Piper is there); Ollama's
HTTP API; `whisper-cli`; `tools/make_voice.py` for the voice.

**Spec:** `docs/superpowers/specs/2026-11-11-ai-brain-design.md`, sections 1–6 and 8–10 (read it first).

## Global Constraints

- Standard library only: no new packages, no new downloads.
- Run from the repo root: `tools/.venv/bin/python -m brain.server`. Tests:
  `tools/.venv/bin/python -m unittest discover -s brain -t . -p 'test_*.py'` (no model, voice,
  microphone or network needed).
- Replies are `text/plain; charset=utf-8`, one `key=value` per line, no line breaks in values.
- Limits, exactly: reply text ≤ 150 characters; question ≤ 300 characters; facts ≤ 40 lines of
  ≤ 120 characters; `/ask` and `/debate` bodies ≤ 4096 bytes; `/hear` body ≤ 1 048 576 bytes;
  debate turns 2–12 (default 8); clip tokens 32 hex characters, 600 s; clip store ≤ 64 clips and
  ≤ 40 MB; chat memory 12 messages, 600 s idle; long-poll 20 s; model timeout 25 s.
- Keys are never printed, logged or put in a command line. `brain/.env` is git-ignored, mode 600.
- No film quotes in any prompt or stock line. Generated text reaches a device only through
  `persona.clean`.
- The repo is public: `docs/SETUP.md` gains the brain's set-up in Task 6's commit; macOS (tested)
  and Linux; no private details (addresses, names, keys) in code, tests or docs.
- Commits end with `Co-Authored-By: Claude Fable 5.2 <noreply@anthropic.com>`; push after each task.

## Review Focus

1. **The model says something unusable** (empty, only a thinking block, markdown, 600
   characters, emoji, curly quotes): the device still gets a speakable line of ≤ 150 plain
   characters. Owner: Task 1 (`test_clean_*`), Task 4 (`test_ask_survives_junk`).
2. **The model is down or slow:** 503 `error=no-model`, and the server keeps serving. Owner:
   Task 4 (`test_ask_model_down`), Task 5 (`test_503_then_still_serving`).
3. **Hostile or broken requests on the open port** (no key, wrong key, no `Content-Length`,
   oversized body, unknown path, bad `who`, a clip token with `/` or `..`): the right status,
   and no model, voice or file is touched. Owner: Task 5 (`test_rejects_*`).
4. **Two requests at once** (an ask during an argument): both answered, one generation at a
   time. Owner: Task 4 (`test_one_generation_at_a_time`).
5. **Things left behind** (expired tokens, a full clip store, an argument nobody collects):
   404, oldest dropped, generation stops. Owner: Task 2 (`test_clip_*`), Task 4
   (`test_debate_stops_when_nobody_asks`).

---

### Task 1: Persona and cleaner

**Files:** create `brain/__init__.py` (empty), `brain/persona.py`, `brain/test_persona.py`.

**Interfaces — produces:**
- `MAX_CHARS = 150`, `FALLBACK = "I have nothing to say to that, Dave."`
- `SCREENS: dict[str, tuple[str, ...]]` for `"zero"` and `"desk"` (spec §2).
- `clean(raw: str) -> str`
- `split_show(raw: str, who: str) -> tuple[str, str | None]` (text without any `[SHOW …]`
  tag; the id of the last tag if it is one of `who`'s screens).
- `parse_body(body: bytes) -> tuple[list[str], str]` (facts, question); `ValueError` when it
  isn't UTF-8 or there is no question.
- `system_prompt(who: str, facts: list[str]) -> str`
- `debate_prompt(who: str, topic: str, facts: list[str], last: bool) -> str`
- `other(who: str) -> str` (`"zero"` ↔ `"desk"`); `ValueError` for anything else.

- [ ] **Step 1: failing tests** in `brain/test_persona.py`:

```python
def test_clean_plain_line_unchanged(self):
    self.assertEqual(clean("I am a cool 47 C."), "I am a cool 47 C.")
def test_clean_strips_thinking_markdown_and_stage_directions(self):
    self.assertEqual(clean("<think>hmm</think>**Certainly**, Dave. *smiles* (pause) `ok`"), "Certainly, Dave. ok")
def test_clean_plain_punctuation_and_ascii_only(self):
    self.assertEqual(clean("“It’s fine” — really… \U0001F600 café"), "It's fine, really... caf")
def test_clean_removes_surrounding_quotes_and_speaker_label(self):
    self.assertEqual(clean('HAL Zero: "I am the upgrade."'), "I am the upgrade.")
def test_clean_cuts_at_the_last_sentence_within_150(self):
    long = "One short sentence here. " * 10            # 250 characters
    out = clean(long)
    self.assertLessEqual(len(out), 150); self.assertTrue(out.endswith("here."))
def test_clean_cuts_one_long_sentence_at_a_word(self):
    out = clean("word " * 60)
    self.assertLessEqual(len(out), 150); self.assertTrue(out.endswith("word."))
def test_clean_nothing_left_is_the_stock_line(self):
    for junk in ("", "   ", "<think>only thinking</think>", "*shrugs*", "\U0001F600"):
        self.assertEqual(clean(junk), FALLBACK)
def test_clean_keeps_one_line(self):
    self.assertEqual(clean("First.\nSecond."), "First. Second.")
def test_split_show(self):
    self.assertEqual(split_show("Here they are, Dave. [SHOW sensors]", "desk"), ("Here they are, Dave.", "sensors"))
    self.assertEqual(split_show("Here. [SHOW sensors]", "zero"), ("Here.", None))      # not his screen
    self.assertEqual(split_show("No. [SHOW rm -rf] [SHOW wall]", "zero"), ("No.", "wall"))
    self.assertEqual(split_show("Nothing to show.", "zero"), ("Nothing to show.", None))
def test_parse_body(self):
    facts, q = parse_body(b"time: 16:42\ntemp: 47 C\n\nHow hot are you?\n")
    self.assertEqual((facts, q), (["time: 16:42", "temp: 47 C"], "How hot are you?"))
    self.assertEqual(parse_body(b"\nJust a question"), ([], "Just a question"))
    self.assertEqual(parse_body(b"No blank line at all"), ([], "No blank line at all"))
def test_parse_body_limits(self):
    facts, q = parse_body(("f: " + "x" * 300 + "\n") .encode() * 60 + b"\n" + b"q" * 500)
    self.assertEqual(len(facts), 40); self.assertTrue(all(len(f) <= 120 for f in facts)); self.assertEqual(len(q), 300)
    self.assertEqual(parse_body(b"a: b\x00\x07c\n\nhi\x1b[0m")[0], ["a: bc"])       # control characters go
    for bad in (b"", b"fact: 1\n\n   ", b"\xff\xfe\n\nhi"):
        with self.assertRaises(ValueError): parse_body(bad)
def test_prompts(self):
    p = system_prompt("zero", ["temp: 47 C"])
    for must in ("HAL Zero", "150", "temp: 47 C", "[SHOW", "diagnostics", "Dave"): self.assertIn(must, p)
    self.assertNotIn("chess", p)                                                    # the desk's own screens
    self.assertIn("HAL 9000", system_prompt("desk", []))
    d = debate_prompt("desk", "who makes better tea", [], last=True)
    self.assertIn("who makes better tea", d); self.assertIn("last word", d); self.assertNotIn("[SHOW", d)
    with self.assertRaises(ValueError): system_prompt("dave", [])
```

- [ ] **Step 2:** run the tests: they fail to import `brain.persona`.
- [ ] **Step 3: implement.** `clean` order: drop `<think>…</think>` (and an unclosed one to the
  end); unwrap `**bold**`; drop `*…*`, `(…)`, `[…]` spans; drop a leading `HAL Zero:` / `HAL 9000:` / `HAL:` label;
  map `’‘` → `'`, `“”` → nothing, `—–` → `, `, `…` → `...`; drop markdown marks `* _ # \``; drop
  non-ASCII and control characters; collapse whitespace; tidy ` ,` → `,`; strip quotes; cut.
  Both personas share one rules paragraph (`persona.RULES`): 1 or 2 short sentences, at
  most 150 characters, plain text, no film lines even when baited, a fact only when Dave asks
  about it. (Added during the live check: `is_film_line` and `OWN_WORDS`, spec §2.)
- [ ] **Step 4:** tests pass.
- [ ] **Step 5:** commit `Brain: personas and the cleaner`.

### Task 2: Clip store, chat memory, settings file

**Files:** create `brain/clips.py`, `brain/memory.py`, `brain/config.py`, `brain/test_stores.py`.

**Interfaces — produces:**
- `ClipStore(ttl_s=600, max_clips=64, max_bytes=40 * 1024 * 1024, clock=time.monotonic)` with
  `put(wav: bytes) -> str` and `get(token: str) -> bytes | None`. Thread-safe.
- `ChatMemory(max_messages=12, idle_s=600, clock=time.monotonic)` with
  `history(who) -> list[dict]` (`{"role": "user"|"assistant", "content": str}`, oldest first)
  and `add(who, question, answer)`. Thread-safe.
- `read_env(path) -> dict[str, str]` (`KEY=VALUE` lines; `#` comments and blank lines skipped;
  surrounding quotes removed; a missing file is `{}`), `ENV_PATH` (`brain/.env`).

- [ ] **Step 1: failing tests:** `test_clip_round_trip` (token matches `^[0-9a-f]{32}$`, two
  puts differ); `test_clip_expires` (fake clock: found at 599 s, `None` at 601 s);
  `test_clip_unknown_or_malformed` (`get("../etc")`, `get("")`, `get("z" * 32)` are `None`);
  `test_clip_store_caps` (`max_clips=3`: the first of four puts is gone; `max_bytes=100`: three
  40-byte clips keep only the last two); `test_memory_keeps_last_12` (8 exchanges → 12
  messages, oldest gone, roles alternate user/assistant); `test_memory_is_per_hal`;
  `test_memory_forgets_after_idle` (history empty 601 s after the last `add`);
  `test_read_env` (comments, quotes, `=` inside a value, missing file).
- [ ] **Step 2:** fail (modules missing). **Step 3:** implement. **Step 4:** pass.
- [ ] **Step 5:** commit `Brain: clip store, chat memory, settings`.

### Task 3: Model, voice and ears

**Files:** create `brain/providers/__init__.py`, `brain/providers/ollama.py`, `brain/voice.py`,
`brain/stt.py`, `brain/test_wrappers.py`.

**Interfaces — produces:**
- `providers.ProviderError(Exception)`; `providers.make_provider(env: dict) -> provider`
  (`HAL_BRAIN_PROVIDER`, default `ollama`; `HAL_BRAIN_MODEL`; `ValueError` for a name it doesn't know).
- A provider has `name: str`, `chat(system: str, messages: list[dict], max_tokens: int = 80) -> str`
  (raises `ProviderError`), `warm() -> None` (never raises).
- `ollama.build_request(model, system, messages, max_tokens) -> dict` and
  `ollama.parse_response(payload: dict) -> str` (pure); `OllamaProvider(model="qwen3.6:35b-a3b",
  host="http://127.0.0.1:11434", timeout_s=25)`.
- `voice.VoiceError`; `voice.wav_ms(wav: bytes) -> int`; `voice.Voice()` with
  `render(who: str, text: str) -> bytes` (22 050 Hz mono 16-bit WAV; desk treatment for `desk`,
  HAL Zero's treatment for `zero`).
- `stt.SttError`; `stt.check_wav(wav: bytes) -> None` (`ValueError` unless RIFF/WAVE PCM 16-bit
  mono 16 000 Hz with at least 0.2 s of audio); `stt.tidy(text: str) -> str`;
  `stt.Whisper(model=MODEL, binary="whisper-cli", timeout_s=30)` with `transcribe(wav) -> str`.

- [ ] **Step 1: failing tests:** `test_ollama_request` (`stream` False, `think` False,
  `keep_alive` "30m", `options.num_predict` 80, the system message first, then the history);
  `test_ollama_response` (`{"message": {"content": " hi "}}` → `"hi"`; a missing message or an
  `error` key raises `ProviderError`); `test_make_provider` (default is ollama with the default
  model; `HAL_BRAIN_MODEL` overrides; `"nope"` raises `ValueError`); `test_wav_ms` (a 44-byte
  header + 22 050 samples → 1000; garbage → 0); `test_check_wav` (a good 16 kHz mono clip
  passes; 22 050 Hz, stereo, 8-bit, truncated and non-RIFF each raise `ValueError`);
  `test_tidy` (`" [BLANK_AUDIO] "` → `""`; `"(wind) Hello  there [music]"` → `"Hello there"`).
- [ ] **Step 2:** fail. **Step 3:** implement. `Voice` imports `tools/make_voice.py` by path
  (`load_voice`, `say`, `DESK`, `TREATMENTS[NEW_TREATMENT]`), renders in a fresh temporary
  folder per call, and holds a lock while it does. `Whisper.transcribe` writes the clip to a
  temporary file and runs the binary with the options `whisper-cli --help` gives for "no
  timestamps", "no extra prints" and English (check them on this machine; do not guess).
- [ ] **Step 4:** pass. **Step 5:** commit `Brain: model, voice and ears`.

### Task 4: The brain and arguments

**Files:** create `brain/core.py`, `brain/debate.py`, `brain/test_core.py`.

**Interfaces — consumes** Tasks 1–3. **Produces:**
- `core.Line` (dataclass: `who: str`, `text: str`, `clip: str`, `ms: int`, `show: str | None = None`).
- `core.Brain(provider, voice, ears, clips: ClipStore, memory: ChatMemory)` with
  `ask(who, facts, question) -> Line`, `hear(wav: bytes) -> str`, and
  `speak(who, system, messages) -> Line` (generate → `split_show` → `clean` → render → store;
  one at a time). Errors pass through as `ProviderError`, `VoiceError`, `SttError`, `ValueError`.
- `debate.Debate(brain, topic, facts, turns, idle_s=120, clock=time.monotonic)` with
  `get(n: int, timeout_s: float = 20) -> tuple[str, Line | None]` — `("line", line)`,
  `("wait", None)` or `("end", None)` — `turns: int`, and `stop()`.
- `debate.Debates(brain, keep_s=600, clock=time.monotonic)` with
  `start(topic, facts, turns) -> str` (16 hex) and `get(debate_id) -> Debate | None`.

- [ ] **Step 1: failing tests** with `FakeProvider(replies)` (records calls, can raise, can
  note overlapping calls), `FakeVoice` (a 1-second WAV, records `(who, text)`), `FakeEars`:
  - `test_ask` — the line's text is the cleaned reply, `ms` 1000, the clip is in the store, the
    voice was asked for `who`'s voice, and the system prompt held the facts.
  - `test_ask_remembers` — the second ask's messages hold the first question and answer.
  - `test_ask_show` — reply `"Look. [SHOW wall]"` → `text == "Look."`, `show == "wall"`.
  - `test_ask_survives_junk` — replies `""`, `"<think>x</think>"`, `"**" + "a" * 600` each give
    a line of 1–150 characters and a clip.
  - `test_ask_model_down` — the provider raises → `ProviderError`; nothing is remembered.
  - `test_ask_bad_who` — `ValueError`.
  - `test_one_generation_at_a_time` — four threads ask at once; the fake saw no overlap; four lines.
  - `test_hear` — `check_wav` runs first (a 22 kHz clip is `ValueError`), then the ears' text is tidied.
  - `test_debate_order` — `turns=4`: lines 0–3 are zero, desk, zero, desk; each prompt names the
    topic; line 3's prompt says it is the last word; `get(4)` is `("end", None)`.
  - `test_debate_one_ahead` — after `get(0)` returns, the provider has been called at most twice
    (lines 0 and 1), however long we wait (use an event, not a sleep, to know line 1 finished).
  - `test_debate_wait` — a provider that blocks: `get(0, timeout_s=0.05)` is `("wait", None)`.
  - `test_debate_stop` — after `stop()`, `get` of an ungenerated line is `("end", None)` and no
    further provider calls happen.
  - `test_debate_stops_when_nobody_asks` — fake clock past `idle_s`: the worker generates nothing more.
  - `test_debate_model_down` — the provider raises on line 1: `get(1)` is `("end", None)`.
  - `test_debates_registry` — unknown id is `None`; an id older than `keep_s` is forgotten.
- [ ] **Step 2:** fail. **Step 3:** implement. Each debate line's messages: the lines so far,
  the speaker's own as `assistant` and the other's as `user`; line 0's only message is
  `"Begin."`. A worker thread generates line k once k ≤ (highest n asked for) + 1.
- [ ] **Step 4:** pass. **Step 5:** commit `Brain: asking, hearing and live arguments`.

### Task 5: The HTTP server

**Files:** create `brain/server.py`, `brain/test_server.py`.

**Interfaces — consumes** Task 4. **Produces:**
- `server.format_reply(pairs: dict) -> bytes`.
- `server.make_server(brain, debates, key: str | None, port: int = 9100, long_poll_s: float = 20)`
  → a `ThreadingHTTPServer` bound to `127.0.0.1` when `key` is empty, else `0.0.0.0`.
- `server.main()` — reads `brain/.env`, builds the real pieces, warms the model in a thread, serves.

- [ ] **Step 1: failing tests** (a real server on port 0 with Task 4's fakes, requests with `http.client`):
  - `test_format_reply` — `{"text": "a\nb", "ms": 1200}` → `b"text=a b\nms=1200\n"`.
  - `test_ask_round_trip` — 200; `text=`, `clip=`, `ms=`; then `GET /clip/<token>` is the WAV with `audio/wav`.
  - `test_ask_show_line` — `show=wall` present only when a screen was asked for.
  - `test_hear_round_trip` — a good clip → `heard=hello there`.
  - `test_debate_round_trip` — start with `turns=2`; `/0` is `who=zero`, `last=0`; `/1` is `who=desk`, `last=1`; `/2` is 204; `/stop` is `ok=1`.
  - `test_debate_wait_is_202` — blocking provider, `long_poll_s=0.05` → 202 `wait=1`.
  - `test_rejects_missing_or_wrong_key` — 401 `error=bad-key` for `/ask`, `/hear`, `/debate`, `/debate/x/0`; the provider was never called. The comparison is constant-time (`hmac.compare_digest`).
  - `test_rejects_no_length` (411), `test_rejects_too_large` (413 for a 4097-byte ask and a 1 048 577-byte hear, without reading the body), `test_rejects_bad_requests` (400 for `who=dave`, an empty question, `turns=99`, a bad WAV → `bad-audio`).
  - `test_rejects_unknown_paths_and_tokens` — 404 for `/`, `/clip/../../etc/passwd`, `/clip/` + 31 hex, an unknown debate, `/debate/<id>/x`.
  - `test_health_needs_no_key` — `ok=1` and nothing else.
  - `test_503_then_still_serving` — the provider raises → 503 `error=no-model`; the next ask (provider recovered) is 200.
  - `test_binds_local_without_a_key` — `make_server(..., key=None).server_address[0] == "127.0.0.1"`.
- [ ] **Step 2:** fail. **Step 3:** implement. The handler logs the method, path and status
  only (never headers or bodies). A socket timeout of 10 s on reads.
- [ ] **Step 4:** pass. **Step 5:** commit `Brain: the HTTP server`.

### Task 6: Tools, set-up and the live check

**Files:** create `tools/brain.sh`, `brain/ask.py`, `brain/pair.py`, `brain/env.example`,
`brain/test_tools.py`; modify `.gitignore` (`brain/.run/`), `docs/SETUP.md`, `README.md`,
`CHANGELOG.md` (`[Unreleased]`).

**Interfaces — produces:**
- `tools/brain.sh start|stop|status|ask WHO "QUESTION"|pair [user@host]` (pid and log in
  `brain/.run/`; `start` waits for `/health`).
- `pair.config_lines(host_name: str, port: int, key: str) -> str` (the four `brain…=` lines) and
  `pair.ensure_key(env_path) -> str` (keeps an existing key; otherwise writes a new
  `secrets.token_hex(16)` and sets mode 600). The key goes to the Cardputer on standard input.

- [ ] **Step 1: failing tests:** `test_config_lines` (four lines, in order `brain=on`,
  `brain_host=`, `brain_port=`, `brain_key=`); `test_ensure_key_keeps_existing`;
  `test_ensure_key_creates` (32 hex, file mode `0o600`, other lines of the file kept).
- [ ] **Step 2:** fail. **Step 3:** implement. **Step 4:** pass, and the whole suite passes.
- [ ] **Step 5: live check on this computer** (Ollama running, no devices):
  `tools/brain.sh start`; `tools/brain.sh ask zero "What do you think of the old model?"` and
  `ask desk "How are you feeling?"` each print an in-character line of ≤ 150 characters and
  play it; a clip from `/clip` converted to 16 kHz and posted to `/hear` comes back as the same
  words; a debate of 4 turns runs through `curl`; `tools/brain.sh stop`. Record timings in the ledger.
- [ ] **Step 6:** `docs/SETUP.md`: a new section "The AI brain (optional)": what it needs
  (Ollama and a model, `whisper-cli` and its model with the download command and size), the
  settings file, `start`/`ask`/`stop`, the firewall prompt, and that pairing with HAL Zero comes
  with the device work. README: one paragraph under a new "The AI brain" heading. CHANGELOG.
- [ ] **Step 7:** commit `Brain: tools, set-up guide and the first live answers`; push.
