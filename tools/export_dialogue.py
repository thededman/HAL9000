#!/usr/bin/env python3
"""Export a scripted dialogue as one stereo file for posting: desk HAL left, HAL Zero right.

    tools/.venv/bin/python tools/export_dialogue.py introduction          # .wav + .srt
    tools/.venv/bin/python tools/export_dialogue.py introduction --mp3    # and an .mp3

Uses the clips make_voice.py --dialogues rendered, with the live conductor's timing:
400 ms between lines unless the script says PAUSE. Writes export/dialogues/<id>.wav
(44.1 kHz stereo), .srt and .mp3.
"""
from __future__ import annotations

import argparse
import dataclasses
import pathlib
import subprocess
import sys
import tempfile
import wave
from array import array

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import make_voice as mv  # noqa: E402

OUT_DIR = mv.ROOT / "export" / "dialogues"
SPEAKERS = {"old": "HAL 9000", "new": "HAL Zero"}
CHANNEL = {"old": 0, "new": 1}  # left, right


@dataclasses.dataclass
class Cue:
    n: int
    who: str
    text: str
    start_ms: int
    end_ms: int


# ---------- pure helpers (tested) ----------

def srt_timestamp(ms: int) -> str:
    h, rest = divmod(ms, 3_600_000)
    m, rest = divmod(rest, 60_000)
    s, ms = divmod(rest, 1000)
    return f"{h:02d}:{m:02d}:{s:02d},{ms:03d}"


def build_cues(d: mv.Dialogue, durations_ms: list[int], gap_ms: int = mv.DIALOGUE_GAP_MS) -> list[Cue]:
    """Same timing as hal::Conductor: line 1 after its own PAUSE (0 if none); every later line
    after the previous one ends, plus its PAUSE, or the usual gap when it has none."""
    if len(durations_ms) != len(d.lines):
        raise ValueError(f"{len(d.lines)} lines but {len(durations_ms)} durations")
    cues, t = [], 0
    for i, (line, dur) in enumerate(zip(d.lines, durations_ms)):
        if i == 0:
            t = line.pause_ms
        else:
            t += line.pause_ms if line.pause_ms > 0 else gap_ms
        cues.append(Cue(line.n, line.who, line.text, t, t + dur))
        t += dur
    return cues


def srt_text(cues: list[Cue]) -> str:
    blocks = [f"{i}\n{srt_timestamp(c.start_ms)} --> {srt_timestamp(c.end_ms)}\n{SPEAKERS[c.who]}: {c.text}\n"
              for i, c in enumerate(cues, start=1)]
    return "\n".join(blocks)


def mix_stereo(clips: list[tuple[int, str, array]], frames: int) -> array:
    """Interleaved 16-bit stereo: each (start_frame, who, mono samples) on its HAL's side."""
    out = array("h", bytes(4 * frames))
    for start, who, samples in clips:
        ch = CHANNEL[who]
        for i, s in enumerate(samples):
            out[2 * (start + i) + ch] = s
    return out


# ---------- export ----------

def read_clip(path: pathlib.Path) -> array:
    if not path.exists():
        sys.exit(f"Missing {path.relative_to(mv.ROOT)}. Run: tools/.venv/bin/python tools/make_voice.py --dialogues")
    with wave.open(str(path), "rb") as w:
        if (w.getnchannels(), w.getsampwidth(), w.getframerate()) != (1, 2, mv.RATE):
            sys.exit(f"{path.name} isn't {mv.RATE} Hz mono 16-bit; re-render it with make_voice.py --dialogues")
        samples = array("h", w.readframes(w.getnframes()))
    if sys.byteorder == "big":
        samples.byteswap()
    return samples


def export(dialogue_id: str, mp3: bool) -> None:
    script = mv.DIALOGUES_DIR / f"{dialogue_id}.txt"
    if not mv.is_valid_dialogue_id(dialogue_id) or not script.exists():
        sys.exit(f"No content/dialogues/{dialogue_id}.txt")
    try:
        d = mv.parse_dialogue(dialogue_id, script.read_text(encoding="utf-8"))
    except mv.DialogueError as e:
        sys.exit(f"{script.name}:\n  " + "\n  ".join(e.errors))
    clips = [read_clip(mv.dialogue_wav(dialogue_id, line)) for line in d.lines]
    cues = build_cues(d, [round(len(c) * 1000 / mv.RATE) for c in clips])
    starts = [round(c.start_ms * mv.RATE / 1000) for c in cues]
    frames = max(s + len(c) for s, c in zip(starts, clips))
    mixed = mix_stereo([(s, line.who, c) for s, line, c in zip(starts, d.lines, clips)], frames)
    if sys.byteorder == "big":
        mixed.byteswap()

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    wav_out, srt_out = OUT_DIR / f"{dialogue_id}.wav", OUT_DIR / f"{dialogue_id}.srt"
    with tempfile.TemporaryDirectory() as tmp:
        raw = pathlib.Path(tmp) / "mix.wav"
        with wave.open(str(raw), "wb") as w:
            w.setnchannels(2)
            w.setsampwidth(2)
            w.setframerate(mv.RATE)
            w.writeframes(mixed.tobytes())
        subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", str(raw),
                        "-af", "aresample=44100:filter_size=64:cutoff=0.97", "-ar", "44100", "-ac", "2",
                        "-c:a", "pcm_s16le", "-map_metadata", "-1", "-fflags", "+bitexact", str(wav_out)],
                       check=True)
    srt_out.write_text(srt_text(cues), encoding="utf-8")
    print(f"{d.title or dialogue_id}: {len(cues)} lines, {cues[-1].end_ms / 1000:.1f} s")
    print(f"  {wav_out.relative_to(mv.ROOT)}\n  {srt_out.relative_to(mv.ROOT)}")
    if mp3:
        mp3_out = OUT_DIR / f"{dialogue_id}.mp3"
        subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", str(wav_out),
                        "-c:a", "libmp3lame", "-b:a", "192k", "-map_metadata", "-1", str(mp3_out)], check=True)
        print(f"  {mp3_out.relative_to(mv.ROOT)}")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("id", help="dialogue id, e.g. introduction (content/dialogues/<id>.txt)")
    ap.add_argument("--mp3", action="store_true", help="also write an .mp3")
    args = ap.parse_args()
    export(args.id, args.mp3)


if __name__ == "__main__":
    main()
