"""The brain itself, with no network in it: a question in, a spoken line out."""
import dataclasses
import threading

from . import persona, stt
from .voice import wav_ms


@dataclasses.dataclass
class Line:
    who: str                 # "zero" or "desk"
    text: str                # what he says: one plain line, at most 150 characters
    clip: str                # the token to fetch the audio by
    ms: int                  # how long the audio plays
    show: str | None = None  # a screen he was asked to bring up


class Brain:
    def __init__(self, provider, voice, ears, clips, memory):
        self.provider, self.voice, self.ears, self.clips, self.memory = provider, voice, ears, clips, memory
        self._one_at_a_time = threading.Lock()

    def speak(self, who: str, system: str, messages: list[dict], allow_show: bool = True) -> Line:
        """One line from the model, cleaned, spoken in `who`'s voice and kept for fetching."""
        with self._one_at_a_time:
            raw = self.provider.chat(system, messages)[:persona.MAX_RAW]  # far more than a line: don't work on it
            text, show = persona.split_show(persona.strip_thinking(raw), who)
            text = persona.clean(text)
            if persona.is_film_line(text):  # the prompt forbids it; this makes sure
                text, show = persona.OWN_WORDS[who], None
            wav = self.voice.render(who, text)
        return Line(who, text, self.clips.put(wav), wav_ms(wav), show if allow_show else None)

    def ask(self, who: str, facts: list[str], question: str) -> Line:
        """Dave's question to one HAL, with what that HAL knows right now and the chat so far."""
        system = persona.system_prompt(who, facts)
        line = self.speak(who, system, self.memory.history(who) + [{"role": "user", "content": question}])
        if line.text != persona.FALLBACK:  # no answer came (a cloud model may have declined): don't send it again
            self.memory.add(who, question, line.text)
        return line

    def hear(self, wav: bytes) -> str:
        """What was said in a recording (16-bit mono 16 kHz); may be empty."""
        stt.check_wav(wav)
        return persona.plain(stt.tidy(self.ears.transcribe(wav)), persona.MAX_QUESTION)
