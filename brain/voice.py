"""A HAL's line as audio: the project's Piper voice, through tools/make_voice.py."""
import importlib.util
import io
import pathlib
import sys
import tempfile
import threading
import wave

from .config import ROOT


class VoiceError(Exception):
    """The line couldn't be spoken (Piper or ffmpeg missing or failing)."""


def wav_ms(wav: bytes) -> int:
    """How long a WAV plays, in milliseconds (0 when it isn't one)."""
    try:
        with wave.open(io.BytesIO(wav), "rb") as w:
            return w.getnframes() * 1000 // w.getframerate()
    except (wave.Error, EOFError, ZeroDivisionError):
        return 0


def _make_voice():
    spec = importlib.util.spec_from_file_location("make_voice", ROOT / "tools" / "make_voice.py")
    module = importlib.util.module_from_spec(spec)
    sys.modules["make_voice"] = module  # its dataclasses look the module up by name while it loads
    spec.loader.exec_module(module)
    return module


class Voice:
    """Renders 22 050 Hz mono 16-bit WAVs, like every clip on the HALs. The model stays loaded."""

    def __init__(self):
        self._tool = _make_voice()
        try:
            self._model = self._tool.load_voice()
        except SystemExit as e:  # the voice tool explains what's missing and exits
            raise VoiceError(str(e)) from None
        self._tones = {"desk": self._tool.DESK, "zero": self._tool.TREATMENTS[self._tool.NEW_TREATMENT]}
        self._lock = threading.Lock()

    def render(self, who: str, text: str) -> bytes:
        if who not in self._tones:
            raise ValueError(f"unknown HAL: {who!r}")
        with self._lock, tempfile.TemporaryDirectory() as folder:
            tmp = pathlib.Path(folder)
            try:
                self._tool.say(self._model, text, tmp / "line.wav", self._tones[who], tmp)
                return (tmp / "line.wav").read_bytes()
            except Exception as e:  # Piper or ffmpeg failed
                raise VoiceError(f"couldn't speak the line: {e}") from None
