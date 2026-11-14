"""The brain itself, with no network in it: a question in, a spoken line out."""
import dataclasses
import random
import threading

from . import cast, persona, stt
from .voice import VoiceError, wav_ms


@dataclasses.dataclass
class Line:
    who: str                 # his id in the cast: "zero", "desk", "nano", "paper" or "card"
    text: str                # what he says: one plain line, no longer than his limit
    clip: str                # the token to fetch the audio by; empty when his words are only shown
    ms: int                  # how long the audio plays; 0 when there is none
    show: str | None = None  # a screen he was asked to bring up


class Brain:
    def __init__(self, provider, voice, ears, clips, memory):
        self.provider, self.voice, self.ears, self.clips, self.memory = provider, voice, ears, clips, memory
        self._one_at_a_time = threading.Lock()

    def _write(self, member, system: str, messages: list[dict]) -> tuple[str, str | None]:
        """One line from the model, cleaned to the member's length, and any screen it asked for."""
        raw = self.provider.chat(system, messages)[:persona.MAX_RAW]  # far more than a line: don't work on it
        text, show = persona.split_show(persona.strip_thinking(raw), member.id)
        text = persona.clean(text, member.max_chars)
        if member.kind == "text":  # his words are all there is of him: put their capitals right
            text = persona.sentence_case(text)
        if persona.is_film_line(text):  # the prompt forbids it; this makes sure
            return member.own_words, None
        return text, show

    def speak(self, who: str, system: str, messages: list[dict], allow_show: bool = True, said=()) -> Line:
        """One line from the model, cleaned to `who`'s length and, if he has a voice, spoken in it
        and kept for fetching. A member whose words are only shown gets no audio. `said` is the
        talk so far when several HALs are talking: a line that repeats one of those is asked for
        once more before anything is spoken."""
        member = cast.member(who)
        with self._one_at_a_time:
            text, show = self._write(member, system, messages)
            if said and persona.repeats(text, said):
                text, show = self._write(member, system, messages + [
                    {"role": "assistant", "content": text}, {"role": "user", "content": persona.SAY_SOMETHING_NEW}])
            show = show if allow_show else None
            if member.kind != "voice":
                return Line(who, text, "", 0, show)
            wav = self.voice.render(who, text)
            if member.clip_bytes and len(wav) > member.clip_bytes:
                # More than his device can hold. Numbers and long words speak long, so his character
                # limit alone can't promise a fit: he says one of his short stock lines instead.
                text = random.choice(member.spare)
                wav = self.voice.render(who, text)
                if len(wav) > member.clip_bytes:
                    raise VoiceError(f"even a short line is too large for {member.name}: {len(wav)} bytes, "
                                     f"and he holds {member.clip_bytes}")
        return Line(who, text, self.clips.put(wav), wav_ms(wav), show)

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
