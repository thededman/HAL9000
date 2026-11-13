# AI Brain: Speaking to HAL Zero Implementation Plan (v0.6, part 4)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Hold a key on the Cardputer, say a question, let go, and HAL Zero answers: what he
heard appears in the ask box for a moment (so a mishearing can be fixed or sent to the desk HAL
instead) and is then sent like a typed question.

**Architecture:** A pure `Recording` in `lib/hal_core` collects 16 kHz samples, measures their
level, trims the silence at both ends, brings a quiet recording up to a sensible loudness and
writes the WAV the brain's `/hear` accepts. HAL Zero's glue captures through SDL audio while
the talk key is held, sends the WAV from `BrainClient`'s worker thread, and puts the words in
the ask box.

**Tech Stack:** C++17, PlatformIO, Unity; SDL2 audio capture (PipeWire on the Cardputer); the
brain's `POST /hear` (whisper.cpp).

**Spec:** `docs/superpowers/specs/2026-11-11-ai-brain-design.md` (sections 1, 3, 7, 8).

## Global Constraints

- Recording: 16 000 Hz, mono, 16-bit; 15 s at most (240 000 samples); nothing is recorded
  unless the talk key is held, and audio never leaves the device except to the paired brain.
- A recording whose speech lasts under 0.3 s, or with no sample louder than 150 (about 0.5 % of
  full scale: room noise on the Cardputer peaks near 60), is "nothing heard": it is not sent.
- Quiet speech is scaled so its loudest sample reaches half of full scale (never more than
  ×40, so room noise isn't turned into a roar). The Cardputer's microphone was measured at a
  peak of about 0.7 % of full scale for speech from across a room (2026-11-12).
- The talk key on the Cardputer is **Fn** held by itself (key code 389); pressing any other key
  while it is held makes it an ordinary Fn combination and drops the recording. In the
  simulator window it is the right Alt key.
- HAL Zero stops speaking when the key goes down (he would otherwise record himself).
- What was heard is shown in the ask box for 1.5 s and then sent; any key in that time turns
  it into an ordinary ask box (edit, Tab for the desk unit, Enter, Esc).
- Tests: `pio test -e native` (241 existing stay green). The three builds.
- `docs/SETUP.md` gains speaking in the task that makes it work on the device.
- Commits end with `Co-Authored-By: Claude Fable 5.2 <noreply@anthropic.com>`; push after each task.

## Review Focus

1. **Nothing, or only noise, was recorded** (a brushed key, a silent room): no request is sent
   and HAL Zero says he didn't catch that. Owner: Task 1 (`test_too_short`, `test_too_quiet`).
2. **A very long hold:** recording stops at 15 s and is sent as it is. Owner: Task 1 (`test_cap`).
3. **Loud input** (speaking into the microphone): not scaled up, never clipped by the scaling.
   Owner: Task 1 (`test_loud_is_left_alone`, `test_gain_never_clips`).
4. **The brain misheard, or heard nothing** (`heard=` empty): HAL Zero says he didn't catch
   that; a mishearing can be corrected in the box before it is sent. Owner: Task 2.
5. **Fn used as Fn** (Fn+a key for an arrow or a symbol): no recording is sent. Owner: Task 2.

---

### Task 1: `Recording` (pure)

**Files:** create `lib/hal_core/src/hal_recording.{h,cpp}`, `test/test_recording/test_main.cpp`.

**Interfaces — produces:**

```cpp
class Recording {
 public:
  static constexpr int kRate = 16000;
  static constexpr size_t kMaxSamples = 15 * kRate;
  void start();                                   // forget what was recorded
  void append(const int16_t* samples, size_t n);  // ignored past 15 s
  bool full() const;
  uint32_t ms() const;                            // how long it is so far
  uint8_t level() const;                          // 0..255: how loud the last 100 ms were (for the meter)
  // The WAV to send (16 kHz mono 16-bit), trimmed and brought up to loudness; empty if nothing was heard.
  std::vector<uint8_t> wav() const;
};
```

- [ ] **Step 1: failing tests:** `test_wav_round_trip` (one second of a 440 Hz tone at 30 % comes
  back as a WAV that `parseWav` reads as 16 000 Hz mono 16-bit, about 1000 ms);
  `test_trims_silence` (1 s silence, 1 s tone, 1 s silence → between 1.0 and 1.5 s, keeping
  0.2 s of margin each side); `test_quiet_is_brought_up` (a tone peaking at 230 comes back
  peaking between 8000 and 16500); `test_gain_is_limited` (a tone peaking at 100 is scaled by
  no more than 40); `test_loud_is_left_alone` (a tone at 60 % is unchanged);
  `test_gain_never_clips` (no sample at ±32768 after scaling noise with a few spikes);
  `test_too_short` (0.2 s of tone → empty); `test_too_quiet` (2 s of noise peaking at 60 →
  empty); `test_cap` (20 s appended → `full()`, 15 000 ms, a WAV of at most 15 s);
  `test_level` (silence → 0; a loud tone → over 150; back to silence → falls again);
  `test_start_forgets`.
- [ ] **Step 2:** fail. **Step 3:** implement. **Step 4:** pass. **Step 5:** commit `Recording: what the microphone heard, trimmed and loud enough to send`.

### Task 2: Hold Fn and speak (simulator, then the Cardputer)

**Files:** create `src/linux/mic.{h,cpp}` (SDL capture → `Recording`); modify
`src/linux/brain_client.{h,cpp}` (`hear(wav)` → event `Heard`), `src/linux/sdl_io.{h,cpp}` and
`fb_io.cpp` (`KeyInput::talkDown`), `src/linux/main.cpp`, `src/linux/hud.{h,cpp}` (a level meter).

- `--hear FILE.wav` feeds a recording from a file instead of the microphone (for checking
  without one); the simulator's talk key is right Alt.
- While the key is held: `LISTENING` in the mode row and a level meter in the strip. On release:
  `THINKING`, then the words in the ask box with a 1.5 s countdown to sending.
- Nothing heard (locally, or `heard=` empty): banner `I DIDN'T CATCH THAT, DAVE` and a line
  from `/hal/voice/screens/brain/` is not used for this: he just shows the banner.
- [ ] Simulator: `--hear` with a clip of the HAL voice asking a question → the words appear and
  the answer plays; a silent clip → the banner; a 20 s clip → sent after 15 s of it.
- [ ] Cardputer, with David: hold Fn, ask something, let go; a quiet and a normal voice; Fn+F
  (an arrow) records nothing. Tune the gain floor if speech close to the microphone clips or
  room noise is sent.
- [ ] `docs/SETUP.md`, README, CHANGELOG. Commit `HAL Zero: hold Fn and speak`; push.
