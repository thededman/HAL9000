"""A HAL's line as audio: his Piper voice and treatment, through tools/make_voice.py."""
import importlib.util
import io
import pathlib
import sys
import tempfile
import threading
import wave

from . import cast
from .config import ROOT


class VoiceError(Exception):
    """The line couldn't be spoken (Piper or ffmpeg missing or failing)."""


def wav_ms(wav: bytes) -> int:
    """How long a WAV plays, in milliseconds (0 when it isn't one)."""
    try:
        with wave.open(io.BytesIO(wav), "rb") as w:
            return w.getnframes() * 1000 // w.getframerate()
    except Exception:  # not a WAV, or a header that lies about its sizes
        return 0


def _make_voice():
    spec = importlib.util.spec_from_file_location("make_voice", ROOT / "tools" / "make_voice.py")
    module = importlib.util.module_from_spec(spec)
    sys.modules["make_voice"] = module  # its dataclasses look the module up by name while it loads
    spec.loader.exec_module(module)
    return module


class Voice:
    """Renders a voiced HAL's line as a WAV in his own voice and clip format: 22 050 Hz mono 16-bit
    for the desk HAL and HAL Zero, as on their own cards; smaller 8-bit clips for HAL Card. Each
    Piper model is loaded once and stays loaded."""

    def __init__(self, tool=None):
        self._tool = tool or _make_voice()
        self._tones = {m.id: self._tool.VOICES[m.voice] for m in cast.MEMBERS if m.voice}
        self._models: dict[str, object] = {}  # one loaded model per model file
        self._lock = threading.Lock()
        self._model(self._tool.VOICE)  # the everyday voice, ready now; HAL Card's waits until he first speaks

    def _model(self, name: str):
        if name not in self._models:
            try:
                self._models[name] = self._tool.load_voice(name)
            except SystemExit as e:  # the voice tool explains what's missing and exits
                raise VoiceError(str(e)) from None
            except Exception as e:   # the model's files couldn't be fetched or read
                raise VoiceError(f"couldn't load the voice {name}: {e}") from None
        return self._models[name]

    def render(self, who: str, text: str) -> bytes:
        tone = self._tones.get(who) if isinstance(who, str) else None
        if tone is None:
            raise ValueError(f"no voice for {who!r}")
        with self._lock, tempfile.TemporaryDirectory() as folder:
            tmp = pathlib.Path(folder)
            model = self._model(tone.model)
            try:
                self._tool.say(model, text, tmp / "line.wav", tone, tmp)
                return (tmp / "line.wav").read_bytes()
            except Exception as e:  # Piper or ffmpeg failed
                raise VoiceError(f"couldn't speak the line: {e}") from None
