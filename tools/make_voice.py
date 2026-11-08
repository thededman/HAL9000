#!/usr/bin/env python3
"""Turn HAL's lines (content/lines/*.txt) into WAV files laid out for the SD card.

    tools/.venv/bin/python tools/make_voice.py                       # everything -> sd/hal/
    tools/.venv/bin/python tools/make_voice.py --only tap            # one folder
    tools/.venv/bin/python tools/make_voice.py --mine-from ~/Clips   # your own clips -> mine/
    tools/.venv/bin/python tools/make_voice.py --copy-to /Volumes/HAL

Voice: the HAL 9000 Piper voice (campwill/HAL-9000-Piper-TTS on Hugging Face), used as
trained, levelled only. Output: 22 050 Hz mono 16-bit.
"""
from __future__ import annotations

import argparse
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import urllib.request
import wave

ROOT = pathlib.Path(__file__).resolve().parent.parent
LINES_DIR = ROOT / "content" / "lines"
SD_DIR = ROOT / "sd" / "hal"
MODELS_DIR = ROOT / "tools" / "models"
TEMPLATE_CONFIG = ROOT / "sd-template" / "hal" / "config.txt"

VOICE = "hal"
# Pinned to the revision David listened to, so a re-download can't change the voice.
VOICE_URL = ("https://huggingface.co/campwill/HAL-9000-Piper-TTS/resolve/"
             "5ad905a1f42cb33364df9d3856491bca8738b9fa/")
# The model's own settings (hal.onnx.json): they made the Hugging Face sample David
# picked on 2026-11-08 ("spot on"). No tone effects, so the voice stays as trained.
LENGTH_SCALE = 1.0
NOISE_SCALE = 0.667
NOISE_W_SCALE = 0.8
FX = "loudnorm=I=-16:TP=-1.5"  # same loudness for every clip, never clipping
OUT_FLAGS = ["-ar", "22050", "-ac", "1", "-c:a", "pcm_s16le", "-map_metadata", "-1", "-fflags", "+bitexact"]
AUDIO_EXTS = {".mp3", ".m4a", ".wav", ".aif", ".aiff", ".flac", ".ogg"}


# ---------- pure helpers (tested) ----------

def read_lines(text: str) -> list[str]:
    out = []
    for raw in text.splitlines():
        line = raw.strip()
        if line and not line.startswith("#"):
            out.append(line)
    return out


def slug(text: str, words: int = 5) -> str:
    cleaned = re.sub(r"[^a-z0-9]+", " ", text.lower().replace("'", "").replace("’", ""))
    parts = cleaned.split()[:words]
    return "-".join(parts) or "line"


def out_name(n: int, text: str) -> str:
    return f"{n:03d}-{slug(text)}.wav"


def mine_name(filename: str) -> str:
    """Named after the clip, so converting the same clip again overwrites it."""
    return f"{slug(pathlib.Path(filename).stem, words=8)}.wav"


def folder_for(filename: str) -> str:
    stem = pathlib.Path(filename).stem
    return stem.replace("greet_", "greet/") if stem.startswith("greet_") else stem


def daisy_filter(i: int) -> str:
    """Verse 1 in HAL's own voice, then each verse slower and deeper as he winds down."""
    rate = round(1.0 - 0.07 * i, 2)
    return f"asetrate=22050*{rate:.2f},aresample=22050,{FX}"


# ---------- rendering ----------

def ffmpeg(src: pathlib.Path, dst: pathlib.Path, chain: str | None) -> None:
    cmd = ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", str(src)]
    if chain:
        cmd += ["-af", chain]
    cmd += OUT_FLAGS + [str(dst)]
    subprocess.run(cmd, check=True)


def load_voice():
    try:
        from piper import PiperVoice
        from piper.phonemize_espeak import ESPEAK_DATA_DIR
    except ImportError:
        sys.exit("Piper isn't installed. Run: python3.11 -m venv tools/.venv && "
                 "tools/.venv/bin/pip install -r tools/requirements.txt")
    if len(str(ESPEAK_DATA_DIR)) >= 150:
        sys.exit(f"Piper's speech data path is too long for espeak (max ~160 chars):\n  {ESPEAK_DATA_DIR}\n"
                 "Move the venv somewhere with a shorter path.")
    model = MODELS_DIR / f"{VOICE}.onnx"
    MODELS_DIR.mkdir(parents=True, exist_ok=True)
    for name in (f"{VOICE}.onnx.json", f"{VOICE}.onnx"):
        dst = MODELS_DIR / name
        if not dst.exists():
            print(f"Downloading {name} from Hugging Face...")
            part = dst.with_name(dst.name + ".part")
            urllib.request.urlretrieve(VOICE_URL + name, part)
            part.rename(dst)
    return PiperVoice.load(str(model))


def synth(voice, text: str, dst: pathlib.Path, length_scale: float) -> None:
    from piper import SynthesisConfig
    cfg = SynthesisConfig(length_scale=length_scale, noise_scale=NOISE_SCALE, noise_w_scale=NOISE_W_SCALE)
    with wave.open(str(dst), "wb") as w:
        voice.synthesize_wav(text, w, syn_config=cfg)


def render_folder(voice, txt: pathlib.Path, tmp: pathlib.Path) -> int:
    folder = folder_for(txt.name)
    out_dir = SD_DIR / "voice" / folder
    if out_dir.exists():
        for old in out_dir.glob("*.wav"):
            old.unlink()
    out_dir.mkdir(parents=True, exist_ok=True)
    lines = read_lines(txt.read_text(encoding="utf-8"))
    for i, line in enumerate(lines):
        raw = tmp / "raw.wav"
        if folder == "daisy":
            synth(voice, line, raw, LENGTH_SCALE + 0.25 * i)
            ffmpeg(raw, out_dir / out_name(i + 1, line), daisy_filter(i))
        else:
            synth(voice, line, raw, LENGTH_SCALE)
            ffmpeg(raw, out_dir / out_name(i + 1, line), FX)
        print(f"  {folder}/{out_name(i + 1, line)}")
    return len(lines)


def convert_mine(src_dir: pathlib.Path) -> int:
    out_dir = SD_DIR / "voice" / "mine"
    out_dir.mkdir(parents=True, exist_ok=True)
    files = sorted(p for p in src_dir.iterdir() if p.suffix.lower() in AUDIO_EXTS and not p.name.startswith("."))
    for f in files:
        ffmpeg(f, out_dir / mine_name(f.name), "loudnorm=I=-16:TP=-1.5")
        print(f"  mine/{mine_name(f.name)}  <- {f.name}")
    return len(files)


def copy_to_card(card: pathlib.Path) -> None:
    if not card.is_dir():
        sys.exit(f"{card} isn't mounted.")
    dest = card / "hal"
    (dest / "screens").mkdir(parents=True, exist_ok=True)
    (dest / "voice" / "mine").mkdir(parents=True, exist_ok=True)
    for folder in sorted((SD_DIR / "voice").rglob("*")):
        if not folder.is_dir() or not any(folder.glob("*.wav")):
            continue
        rel = folder.relative_to(SD_DIR)
        target = dest / rel
        target.mkdir(parents=True, exist_ok=True)
        if rel.parts[-1] != "mine":  # generated folders are owned by this tool
            for old in target.glob("*.wav"):
                old.unlink()
        for wav in folder.glob("*.wav"):
            shutil.copy2(wav, target / wav.name)
    if not (dest / "config.txt").exists():  # never overwrite: it holds the Wi-Fi password
        shutil.copy2(TEMPLATE_CONFIG, dest / "config.txt")
        print(f"Copied config.txt — open {dest / 'config.txt'} and fill in your Wi-Fi.")
    subprocess.run(["dot_clean", "-m", str(dest)], check=False)  # only our folder
    print(f"Copied to {dest}")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--only", help="render just this lines file, e.g. tap or greet_morning")
    ap.add_argument("--mine-from", type=pathlib.Path, help="convert your own audio clips into voice/mine/")
    ap.add_argument("--copy-to", type=pathlib.Path, help="copy sd/hal onto a mounted card, e.g. /Volumes/HAL")
    ap.add_argument("--skip-render", action="store_true", help="don't re-render HAL's lines")
    args = ap.parse_args()

    SD_DIR.mkdir(parents=True, exist_ok=True)

    if not args.skip_render:
        txts = sorted(LINES_DIR.glob("*.txt"))
        if args.only:
            txts = [t for t in txts if t.stem == args.only]
            if not txts:
                sys.exit(f"No content/lines/{args.only}.txt")
        voice = load_voice()
        total = 0
        with tempfile.TemporaryDirectory() as tmp:
            for txt in txts:
                total += render_folder(voice, txt, pathlib.Path(tmp))
        print(f"Rendered {total} lines into {SD_DIR / 'voice'}")
    if args.mine_from:
        print(f"Converted {convert_mine(args.mine_from.expanduser())} clips into voice/mine/")
    if args.copy_to:
        copy_to_card(args.copy_to)


if __name__ == "__main__":
    main()
