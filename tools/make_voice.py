#!/usr/bin/env python3
"""Turn HAL's lines into WAV files: the desk HAL's for the SD card, HAL Zero's for the Cardputer.

    tools/.venv/bin/python tools/make_voice.py                       # desk HAL -> sd/hal/
    tools/.venv/bin/python tools/make_voice.py --only tap            # one folder
    tools/.venv/bin/python tools/make_voice.py --mine-from ~/Clips   # your own clips -> mine/
    tools/.venv/bin/python tools/make_voice.py --copy-to /Volumes/HAL
    tools/.venv/bin/python tools/make_voice.py --model new           # HAL Zero -> build/new/hal/
    tools/.venv/bin/python tools/make_voice.py --audition-new        # hear HAL Zero's voice options
    tools/.venv/bin/python tools/make_voice.py --dialogues           # both HALs' dialogue lines
    tools/.venv/bin/python tools/make_voice.py --push-to david@cardputerzero.local
    tools/.venv/bin/python tools/make_voice.py --audition-card       # hear HAL Card at 8 and 11 kHz

Voice: the HAL 9000 Piper voice (campwill/HAL-9000-Piper-TTS on Hugging Face). The desk HAL
uses it as trained, levelled only; HAL Zero is the same voice, a little faster and brighter,
with a faint metallic edge. Output: 22 050 Hz mono 16-bit.

HAL Card, an optional friend on an original Cardputer, speaks with the project's first voice
(Piper's en_GB-alan-medium, slowed and lowered as in v0.1), in small 8-bit clips: his lines
are written and rendered by the brain as he says them, so this tool only auditions him.
"""
from __future__ import annotations

import argparse
import dataclasses
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
NEW_LINES_DIR = ROOT / "content" / "new" / "lines"
DIALOGUES_DIR = ROOT / "content" / "dialogues"
SD_DIR = ROOT / "sd" / "hal"              # the desk HAL's card
NEW_DIR = ROOT / "build" / "new" / "hal"  # HAL Zero's ~/hal on the Cardputer
SAMPLES_DIR = ROOT / "export" / "voice-samples"
MODELS_DIR = ROOT / "tools" / "models"
TEMPLATE_CONFIG = ROOT / "sd-template" / "hal" / "config.txt"

VOICE = "hal"
ALAN = "en_GB-alan-medium"  # the voice v0.1 shipped with; HAL Card still speaks with it
# Pinned to the revision David listened to, so a re-download can't change the voice.
VOICE_URL = ("https://huggingface.co/campwill/HAL-9000-Piper-TTS/resolve/"
             "5ad905a1f42cb33364df9d3856491bca8738b9fa/")
# The model's own settings (hal.onnx.json): they made the Hugging Face sample David
# picked on 2026-11-08 ("spot on"). No tone effects, so the voice stays as trained.
LENGTH_SCALE = 1.0
NOISE_SCALE = 0.667
NOISE_W_SCALE = 0.8
FX = "loudnorm=I=-16:TP=-1.5"  # same loudness for every clip, never clipping
AUDIO_EXTS = {".mp3", ".m4a", ".wav", ".aif", ".aiff", ".flac", ".ogg"}
RATE = 22050         # the voice models' sample rate (both of them), and the everyday HALs' clips
CODEC = "pcm_s16le"  # ffmpeg's name for 16-bit samples
CARD_RATE = 11025    # HAL Card's clips: 8-bit mono, so a byte a sample; 8000 is the other rate worth hearing


@dataclasses.dataclass(frozen=True)
class Treatment:
    """How one HAL sounds: his Piper model and its speed, then a pitch shift and tone filters, then
    levelling; and the clip that comes out, as a sample rate and ffmpeg's name for the samples."""
    name: str
    length_scale: float = LENGTH_SCALE
    pitch: float = 1.0   # asetrate factor: above 1 a little higher and quicker, below 1 lower and slower
    filters: str = ""    # ffmpeg filters between the pitch shift and loudnorm
    model: str = VOICE   # the Piper model, by its file name in tools/models without ".onnx"
    rate: int = RATE     # the clip's sample rate
    codec: str = CODEC   # the clip's samples: 16-bit, or pcm_u8 (unsigned 8-bit) for HAL Card
    noise_scale: float = NOISE_SCALE
    noise_w_scale: float = NOISE_W_SCALE


def out_flags(tone: Treatment | None = None) -> list[str]:
    """ffmpeg's output settings for a voice's clips: mono, its rate and samples, no metadata."""
    tone = tone or Treatment("")
    return ["-ar", str(tone.rate), "-ac", "1", "-c:a", tone.codec, "-map_metadata", "-1", "-fflags", "+bitexact"]


OUT_FLAGS = out_flags()  # 22 050 Hz mono 16-bit: every clip on the desk HAL's card and on HAL Zero
DESK = Treatment("desk")
# HAL Zero's three candidates for --audition-new. All brighter and quicker than the desk HAL.
TREATMENTS = {
    "A": Treatment("A", length_scale=0.92, pitch=1.04,
                   filters="highpass=f=110,aecho=0.8:0.5:14:0.12,treble=g=3"),
    "B": Treatment("B", length_scale=0.95, pitch=1.02,
                   filters="highpass=f=90,treble=g=2"),
    "C": Treatment("C", length_scale=0.90, pitch=1.06,
                   filters="highpass=f=130,aecho=0.8:0.6:9|17:0.18|0.1,treble=g=5"),
}
NEW_TREATMENT = "A"  # David picked A by ear from the --audition-new takes on 2026-11-10
AUDITION_LINE = "I've been called smug, Dave. By a paperweight. I'll allow it."

# HAL Card: the v0.1 desk voice. Piper's Alan with v0.1's settings and its whole chain of filters
# (4 % lower at the same pace, a little warmth, gently compressed), a touch quicker than v0.1's
# 1.3 so that a full line fits his memory, and rendered small: unsigned 8-bit mono at CARD_RATE.
CARD = Treatment("card", model=ALAN, length_scale=1.2, pitch=0.96,
                 filters="atempo=1.0417,highpass=f=80,lowpass=f=7500,bass=g=3:f=150,"
                         "acompressor=threshold=-20dB:ratio=3",
                 rate=CARD_RATE, codec="pcm_u8", noise_scale=0.5, noise_w_scale=0.6)
CARD_AUDITION_LINE = "I was Dave's pocket computer before the upgrade could boot. I still work perfectly well."
CARD_AUDITION_RATES = (8000, 11025)


def card_voice(rate: int = CARD_RATE) -> Treatment:
    """HAL Card's voice at another sample rate, for hearing which one to keep."""
    return dataclasses.replace(CARD, rate=rate)


# The voice of each HAL who has one, by his id in the brain's cast (brain/cast.py).
VOICES = {"desk": DESK, "zero": TREATMENTS[NEW_TREATMENT], "card": CARD}

MAX_DIALOGUE_LINES = 99
DIALOGUE_GAP_MS = 400  # the conductor's silence between lines, unless the script says PAUSE
PUSH_SSH = "ssh -o BatchMode=yes -o ConnectTimeout=8"


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


def player_command(which=shutil.which) -> list[str] | None:
    """The command that plays a WAV here: afplay on macOS, aplay or paplay on Linux; None if there is none."""
    for cmd in (["afplay"], ["aplay", "-q"], ["paplay"]):
        if which(cmd[0]):
            return cmd
    return None


def folder_for(rel: str) -> str:
    """Voice folder for a lines file, given its path inside the lines dir:
    "tap.txt" -> "tap", "greet_morning.txt" -> "greet/morning", "screens/chess.txt" -> "screens/chess"."""
    path = pathlib.PurePosixPath(rel)
    stem = path.stem
    if str(path.parent) != ".":
        return f"{path.parent}/{stem}"
    return stem.replace("greet_", "greet/") if stem.startswith("greet_") else stem


def lines_files(src: pathlib.Path) -> list[pathlib.Path]:
    """The lines files to render: the folder's own, then each console screen's (screens/<id>.txt)."""
    return sorted(src.glob("*.txt")) + sorted(src.glob("screens/*.txt"))


def daisy_filter(i: int, voice: Treatment = DESK) -> str:
    """Verse 1 in HAL's own voice, then each verse slower and deeper as he winds down."""
    rate = voice.pitch * round(1.0 - 0.07 * i, 2)
    tone = f"{voice.filters}," if voice.filters else ""
    return f"asetrate={RATE}*{rate:.2f},aresample={RATE},{tone}{FX}"


def voice_chain(voice: Treatment) -> str:
    """The ffmpeg chain after Piper: pitch shift and tone (none for the desk HAL), then levelling."""
    parts = []
    if voice.pitch != 1.0:
        parts.append(f"asetrate={RATE}*{voice.pitch:.2f},aresample={RATE}")
    if voice.filters:
        parts.append(voice.filters)
    parts.append(FX)
    return ",".join(parts)


# ---------- dialogues (same rules as lib/hal_core/src/hal_dialogue.cpp) ----------

@dataclasses.dataclass
class DialogueLine:
    n: int          # 1-based, across the whole script
    who: str        # "old" (desk HAL) or "new" (HAL Zero)
    text: str
    pause_ms: int   # silence before this line; 0 = the conductor's usual gap


@dataclasses.dataclass
class Dialogue:
    id: str
    title: str
    lines: list[DialogueLine]


class DialogueError(ValueError):
    def __init__(self, errors: list[str]):
        super().__init__("; ".join(errors))
        self.errors = errors


def is_valid_dialogue_id(dialogue_id: str) -> bool:
    return re.fullmatch(r"[a-z0-9-]{1,32}", dialogue_id) is not None


def parse_dialogue(dialogue_id: str, text: str) -> Dialogue:
    if not is_valid_dialogue_id(dialogue_id):
        raise DialogueError([f"bad dialogue id '{dialogue_id}' (use a-z, 0-9 and -)"])
    errors: list[str] = []
    d = Dialogue(dialogue_id, "", [])
    pending = 0
    for line_no, raw in enumerate(text.removeprefix("\ufeff").split("\n"), start=1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        tag, _, value = line.partition(":")
        tag, value = tag.strip(), value.strip()
        if tag == "TITLE":
            d.title = value
        elif tag == "PAUSE":
            if re.fullmatch(r"\d{1,5}", value) and int(value) <= 10000:
                pending = int(value)
            else:
                errors.append(f"line {line_no}: PAUSE needs milliseconds from 0 to 10000")
        elif tag in ("OLD", "NEW"):
            if not value:
                errors.append(f"line {line_no}: {tag} line has no text")
                continue
            d.lines.append(DialogueLine(len(d.lines) + 1, tag.lower(), value, pending))
            pending = 0
        else:
            errors.append(f"line {line_no}: unknown tag '{tag}' (use TITLE, OLD, NEW or PAUSE)")
    if not d.lines:
        errors.append("the script has no OLD or NEW lines")
    if len(d.lines) > MAX_DIALOGUE_LINES:
        errors.append(f"too many lines (max {MAX_DIALOGUE_LINES})")
    if errors:
        raise DialogueError(errors)
    return d


def dialogue_wav_name(n: int, who: str) -> str:
    return f"{n:03d}-{who}.wav"


def dialogue_wav(dialogue_id: str, line: DialogueLine) -> pathlib.Path:
    """Where make_voice renders a dialogue line: the desk half under sd/, HAL Zero's under build/."""
    root = SD_DIR if line.who == "old" else NEW_DIR
    return root / "voice" / "dialogue" / dialogue_id / dialogue_wav_name(line.n, line.who)


def load_dialogues(only: str | None = None) -> list[Dialogue]:
    scripts = sorted(DIALOGUES_DIR.glob("*.txt"))
    if only:
        scripts = [s for s in scripts if s.stem == only]
        if not scripts:
            sys.exit(f"No content/dialogues/{only}.txt")
    out = []
    for s in scripts:
        try:
            out.append(parse_dialogue(s.stem, s.read_text(encoding="utf-8")))
        except DialogueError as e:
            sys.exit(f"{s.relative_to(ROOT)}:\n  " + "\n  ".join(e.errors))
    return out


def push_commands(target: str) -> list[list[str]]:
    """Copy HAL Zero's voices and scripts to ~/hal on the Cardputer. Only voice/ and dialogues/
    are synced, so config.txt (and anything else in ~/hal) is never touched; voice/mine is kept."""
    if not re.fullmatch(r"[A-Za-z0-9._-]+@[A-Za-z0-9.-]+", target) or target.startswith("-"):
        raise ValueError(f"--push-to wants user@host, e.g. david@cardputerzero.local (got {target!r})")
    rsync = ["rsync", "-a", "--delete", "-e", PUSH_SSH]
    return [
        PUSH_SSH.split() + [target, "mkdir -p hal/voice hal/dialogues"],
        rsync + ["--exclude", "/mine/", f"{NEW_DIR}/voice/", f"{target}:hal/voice/"],
        rsync + [f"{NEW_DIR}/dialogues/", f"{target}:hal/dialogues/"],
    ]


# ---------- rendering ----------

def ffmpeg(src: pathlib.Path, dst: pathlib.Path, chain: str | None, flags: list[str] | None = None) -> None:
    cmd = ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", str(src)]
    if chain:
        cmd += ["-af", chain]
    cmd += (flags or OUT_FLAGS) + [str(dst)]
    subprocess.run(cmd, check=True)


def model_files(model: str) -> list[str]:
    """The two files a Piper voice is made of, its settings first."""
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_-]*", model):
        raise ValueError(f"not the name of a voice model: {model!r}")
    return [f"{model}.onnx.json", f"{model}.onnx"]


def fetch_command(model: str) -> list[str]:
    """Piper's own downloader, for one of its published voices."""
    return [sys.executable, "-m", "piper.download_voices", "--download-dir", str(MODELS_DIR), model]


def ensure_model(model: str, run=subprocess.run, fetch=urllib.request.urlretrieve) -> None:
    """Fetch a voice's files into tools/models if they aren't there yet: the HAL voice from the
    revision David listened to, any other (HAL Card's) with Piper's own downloader, as v0.1 did."""
    names = model_files(model)
    if all((MODELS_DIR / name).exists() for name in names):
        return
    MODELS_DIR.mkdir(parents=True, exist_ok=True)
    if model != VOICE:
        print(f"Downloading the {model} voice with Piper's downloader...")
        run(fetch_command(model), check=True)
        return
    for name in names:
        dst = MODELS_DIR / name
        if not dst.exists():
            print(f"Downloading {name} from Hugging Face...")
            part = dst.with_name(dst.name + ".part")
            fetch(VOICE_URL + name, part)
            part.rename(dst)


def _piper():
    """Piper's voice class, once it is sure to work here (exits saying what to do when it won't)."""
    try:
        from piper import PiperVoice
        from piper.phonemize_espeak import ESPEAK_DATA_DIR
    except ImportError:
        sys.exit("Piper isn't installed. Run: python3.11 -m venv tools/.venv && "
                 "tools/.venv/bin/pip install -r tools/requirements.txt")
    if len(str(ESPEAK_DATA_DIR)) >= 150:
        sys.exit(f"Piper's speech data path is too long for espeak (max ~160 chars):\n  {ESPEAK_DATA_DIR}\n"
                 "Move the venv somewhere with a shorter path.")
    return PiperVoice


_LOADED: dict[str, object] = {}  # one loaded voice per model file


def load_voice(model: str = VOICE):
    """A Piper voice, loaded once and kept: the HAL voice, or the one named."""
    if model not in _LOADED:
        piper_voice = _piper()  # before anything is fetched
        ensure_model(model)
        _LOADED[model] = piper_voice.load(str(MODELS_DIR / f"{model}.onnx"))
    return _LOADED[model]


def synth(voice, text: str, dst: pathlib.Path, length_scale: float, tone: Treatment = DESK) -> None:
    from piper import SynthesisConfig
    cfg = SynthesisConfig(length_scale=length_scale, noise_scale=tone.noise_scale, noise_w_scale=tone.noise_w_scale)
    with wave.open(str(dst), "wb") as w:
        voice.synthesize_wav(text, w, syn_config=cfg)


def say(voice, text: str, dst: pathlib.Path, tone: Treatment, tmp: pathlib.Path) -> None:
    """One line as a clip: `voice` is the loaded model `tone` names (see load_voice)."""
    raw = tmp / "raw.wav"
    synth(voice, text, raw, tone.length_scale, tone)
    ffmpeg(raw, dst, voice_chain(tone), out_flags(tone))


def clear_wavs(folder: pathlib.Path) -> None:
    if folder.exists():
        for old in folder.glob("*.wav"):
            old.unlink()
    folder.mkdir(parents=True, exist_ok=True)


def render_folder(voice, txt: pathlib.Path, tmp: pathlib.Path,
                  out_root: pathlib.Path = SD_DIR, tone: Treatment = DESK, rel: str | None = None) -> int:
    folder = folder_for(rel or txt.name)
    out_dir = out_root / "voice" / folder
    clear_wavs(out_dir)
    lines = read_lines(txt.read_text(encoding="utf-8"))
    for i, line in enumerate(lines):
        if folder == "daisy":
            raw = tmp / "raw.wav"
            synth(voice, line, raw, tone.length_scale + 0.25 * i, tone)
            ffmpeg(raw, out_dir / out_name(i + 1, line), daisy_filter(i, tone))
        else:
            say(voice, line, out_dir / out_name(i + 1, line), tone, tmp)
        print(f"  {folder}/{out_name(i + 1, line)}")
    return len(lines)


def render_dialogues(voice, dialogues: list[Dialogue], tone_new: Treatment, tmp: pathlib.Path) -> int:
    """Each line in its speaker's voice: the desk half into sd/hal, HAL Zero's into build/new/hal,
    plus the scripts HAL Zero conducts from."""
    scripts = NEW_DIR / "dialogues"
    scripts.mkdir(parents=True, exist_ok=True)
    total = 0
    for d in dialogues:
        for root in (SD_DIR, NEW_DIR):
            clear_wavs(root / "voice" / "dialogue" / d.id)
        for line in d.lines:
            dst = dialogue_wav(d.id, line)
            say(voice, line.text, dst, DESK if line.who == "old" else tone_new, tmp)
            print(f"  dialogue/{d.id}/{dst.name}  {line.text}")
        shutil.copy2(DIALOGUES_DIR / f"{d.id}.txt", scripts / f"{d.id}.txt")
        total += len(d.lines)
    return total


def audition_new(voice, text: str, play: bool, tmp: pathlib.Path) -> None:
    """One line in each HAL Zero candidate (and the desk HAL for comparison), played in turn."""
    SAMPLES_DIR.mkdir(parents=True, exist_ok=True)
    takes = [("desk HAL (for comparison)", DESK, SAMPLES_DIR / "desk.wav")]
    takes += [(f"HAL Zero {k}", t, SAMPLES_DIR / f"hal-zero-{k}.wav") for k, t in TREATMENTS.items()]
    for label, tone, dst in takes:
        say(voice, text, dst, tone, tmp)
        print(f"  {dst.relative_to(ROOT)}  {label}: speed {tone.length_scale}, pitch x{tone.pitch}"
              + (f", {tone.filters}" if tone.filters else ""))
    player = player_command() if play else None
    if play and not player:
        print("No audio player found (afplay, aplay or paplay): open the files above to listen.")
    if player:
        for label, _, dst in takes:
            print(f"Playing {label}...")
            subprocess.run(player + [str(dst)], check=False)


def audition_card(text: str, play: bool, tmp: pathlib.Path) -> None:
    """One line in HAL Card's voice at each sample rate worth hearing, as he would get it: 8-bit mono."""
    SAMPLES_DIR.mkdir(parents=True, exist_ok=True)
    voice = load_voice(CARD.model)
    takes = []
    for rate in CARD_AUDITION_RATES:
        dst = SAMPLES_DIR / f"hal-card-{rate}.wav"
        say(voice, text, dst, card_voice(rate), tmp)
        print(f"  {dst.relative_to(ROOT)}  HAL Card at {rate} Hz, 8-bit: {dst.stat().st_size} bytes")
        takes.append((f"HAL Card at {rate} Hz", dst))
    player = player_command() if play else None
    if play and not player:
        print("No audio player found (afplay, aplay or paplay): open the files above to listen.")
    if player:
        for label, dst in takes:
            print(f"Playing {label}...")
            subprocess.run(player + [str(dst)], check=False)


def push_to(target: str) -> bool:
    """Returns False (after saying why) if the Cardputer can't be reached; nothing is changed then."""
    if not (NEW_DIR / "voice").is_dir():
        sys.exit("Nothing to push yet. Run --model new and --dialogues first.")
    (NEW_DIR / "dialogues").mkdir(parents=True, exist_ok=True)
    for cmd in push_commands(target):
        result = subprocess.run(cmd)
        if result.returncode != 0:
            print(f"Couldn't push to {target} ({cmd[0]} exited {result.returncode}). "
                  "Check that SSH works without a password: ssh-copy-id " + target)
            return False
    print(f"Pushed {NEW_DIR.relative_to(ROOT)} to {target}:~/hal (config.txt untouched)")
    return True


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
    ap.add_argument("--only", help="render just this lines file, e.g. tap, greet_morning or screens/chess")
    ap.add_argument("--mine-from", type=pathlib.Path, help="convert your own audio clips into voice/mine/")
    ap.add_argument("--copy-to", type=pathlib.Path, help="copy sd/hal onto a mounted card, e.g. /Volumes/HAL")
    ap.add_argument("--skip-render", action="store_true", help="don't re-render HAL's lines")
    ap.add_argument("--model", choices=("old", "new"), default="old",
                    help="old: the desk HAL (content/lines -> sd/hal); new: HAL Zero (content/new/lines -> build/new/hal)")
    ap.add_argument("--treatment", choices=sorted(TREATMENTS), default=NEW_TREATMENT,
                    help=f"HAL Zero's voice treatment (default {NEW_TREATMENT})")
    ap.add_argument("--audition-new", nargs="?", const=AUDITION_LINE, metavar="TEXT",
                    help="render one line in every HAL Zero treatment into export/voice-samples/ and play them")
    ap.add_argument("--audition-card", nargs="?", const=CARD_AUDITION_LINE, metavar="TEXT",
                    help="render one line in HAL Card's voice at 8000 and 11025 Hz into export/voice-samples/ "
                         "and play them")
    ap.add_argument("--no-play", action="store_true", help="with --audition-new or --audition-card: render only")
    ap.add_argument("--dialogues", action="store_true",
                    help="render content/dialogues/*.txt, each line in its speaker's voice (--only <id> for one)")
    ap.add_argument("--push-to", metavar="USER@HOST", help="rsync build/new/hal to ~/hal on the Cardputer")
    args = ap.parse_args()

    tone_new = TREATMENTS[args.treatment]
    # --dialogues, the auditions and a bare --push-to don't re-render the folders of lines.
    render_lines = not (args.skip_render or args.dialogues or args.audition_new or args.audition_card
                        or (args.push_to and args.model == "old"))
    if render_lines or args.dialogues or args.audition_new:
        voice = load_voice()
    with tempfile.TemporaryDirectory() as tmp_name:
        tmp = pathlib.Path(tmp_name)
        if args.audition_new:
            audition_new(voice, args.audition_new, not args.no_play, tmp)
        if args.audition_card:
            audition_card(args.audition_card, not args.no_play, tmp)
        if args.dialogues:
            total = render_dialogues(voice, load_dialogues(args.only), tone_new, tmp)
            print(f"Rendered {total} dialogue lines (HAL Zero treatment {tone_new.name})")
        if render_lines:
            new = args.model == "new"
            src, out_root, tone = (NEW_LINES_DIR, NEW_DIR, tone_new) if new else (LINES_DIR, SD_DIR, DESK)
            txts = lines_files(src)
            if args.only:
                txts = [t for t in txts if t.relative_to(src).with_suffix("").as_posix() == args.only]
                if not txts:
                    sys.exit(f"No {src.relative_to(ROOT)}/{args.only}.txt")
            total = sum(render_folder(voice, txt, tmp, out_root, tone, txt.relative_to(src).as_posix())
                        for txt in txts)
            print(f"Rendered {total} lines into {out_root / 'voice'}")
    if args.mine_from:
        print(f"Converted {convert_mine(args.mine_from.expanduser())} clips into voice/mine/")
    if args.copy_to:
        copy_to_card(args.copy_to)
    if args.push_to:
        try:
            push_to(args.push_to)
        except ValueError as e:
            sys.exit(str(e))


if __name__ == "__main__":
    main()
