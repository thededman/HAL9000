"""What Dave said: a short recording to text, with whisper.cpp's whisper-cli on this computer."""
import io
import pathlib
import re
import shutil
import subprocess
import tempfile
import wave

from .config import ROOT

MODEL = ROOT / "brain" / "models" / "ggml-small.en.bin"
RATE = 16000
MIN_SECONDS = 0.2


class SttError(Exception):
    """The recording couldn't be turned into text (whisper-cli or its model missing, or it failed)."""


def check_wav(wav: bytes) -> None:
    """Raises ValueError unless this is a PCM 16-bit mono 16 kHz WAV with something in it."""
    try:
        with wave.open(io.BytesIO(wav), "rb") as w:
            shape = (w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getcomptype())
            frames = w.readframes(w.getnframes())
    except Exception as e:  # wave raises several kinds for a header that lies about its sizes
        raise ValueError(f"not a WAV: {type(e).__name__}") from None
    if shape != (1, 2, RATE, "NONE"):
        raise ValueError("the recording must be 16-bit mono at 16 kHz")
    if len(frames) < int(RATE * MIN_SECONDS) * 2:
        raise ValueError("the recording is too short")


def tidy(transcript: str) -> str:
    """The words only: no [BLANK_AUDIO] or (noise) markers, one line."""
    return " ".join(re.sub(r"\[[^\]]*\]|\([^)]*\)", " ", transcript).split())


class Whisper:
    def __init__(self, model: pathlib.Path = MODEL, binary: str = "whisper-cli", timeout_s: float = 20):
        self._model, self._binary, self._timeout = pathlib.Path(model), binary, timeout_s

    def transcribe(self, wav: bytes) -> str:
        program = shutil.which(self._binary)
        if not program:
            raise SttError(f"{self._binary} isn't installed")
        if not self._model.is_file():
            raise SttError(f"the speech model is missing: {self._model}")
        with tempfile.TemporaryDirectory() as folder:
            clip = pathlib.Path(folder) / "said.wav"
            clip.write_bytes(wav)
            command = [program, "-m", str(self._model), "-f", str(clip), "-nt", "-np", "-l", "en"]
            try:
                done = subprocess.run(command, capture_output=True, text=True, errors="replace",
                                      timeout=self._timeout)
            except (OSError, subprocess.TimeoutExpired) as e:
                raise SttError(f"whisper-cli failed: {e}") from None
        if done.returncode != 0:
            raise SttError(f"whisper-cli failed ({done.returncode})")
        return tidy(done.stdout)

    def warm(self) -> None:
        """Run once on silence so the model is in memory before Dave speaks (the first run takes seconds)."""
        quiet = io.BytesIO()
        with wave.open(quiet, "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(RATE)
            w.writeframes(b"\x00" * RATE)
        try:
            self.transcribe(quiet.getvalue())
        except Exception:
            pass
