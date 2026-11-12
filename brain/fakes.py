"""Stand-ins for the model, the voice and the ears, so the tests need none of them."""
import io
import threading
import time
import wave

from .providers import ProviderError


def make_wav(seconds: float = 1.0, rate: int = 16000, channels: int = 1, width: int = 2) -> bytes:
    out = io.BytesIO()
    with wave.open(out, "wb") as w:
        w.setnchannels(channels)
        w.setsampwidth(width)
        w.setframerate(rate)
        w.writeframes(b"\x00" * int(seconds * rate) * channels * width)
    return out.getvalue()


class FakeProvider:
    """Answers from a list (then "Reply N."), records every call, and can fail, stall or dawdle."""
    name = model = "fake"

    def __init__(self, replies=(), fail_on=(), delay_s: float = 0.0):
        self.replies = list(replies)
        self.fail_on = set(fail_on)  # 1-based call numbers that raise ProviderError
        self.delay_s = delay_s
        self.gate = None             # set to a threading.Event to hold every call until it is set
        self.calls: list[tuple[str, list[dict]]] = []
        self.finished = threading.Semaphore(0)  # released as each call ends
        self.overlapped = False
        self._active = 0
        self._lock = threading.Lock()

    def chat(self, system, messages, max_tokens=80):
        with self._lock:
            self._active += 1
            self.overlapped = self.overlapped or self._active > 1
            self.calls.append((system, list(messages)))
            number = len(self.calls)
        try:
            if self.gate is not None:
                self.gate.wait(5)
            if self.delay_s:
                time.sleep(self.delay_s)
            if number in self.fail_on:
                raise ProviderError("the fake model is down")
            return self.replies.pop(0) if self.replies else f"Reply {number}."
        finally:
            with self._lock:
                self._active -= 1
            self.finished.release()

    def warm(self):
        pass


class FakeVoice:
    """Every line is one second of silence at the HALs' sample rate."""

    def __init__(self):
        self.said: list[tuple[str, str]] = []
        self.wav = make_wav(1.0, rate=22050)

    def render(self, who, text):
        self.said.append((who, text))
        return self.wav


class FakeEars:
    def __init__(self, text: str = "hello there"):
        self.text = text
        self.heard = 0

    def transcribe(self, wav):
        self.heard += 1
        return self.text

    def warm(self):
        pass
