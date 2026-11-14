"""The HALs talking live, in an argument Dave set off, a chat of their own or a roll call: lines
are written one ahead of the one being collected."""
import random
import secrets
import sys
import threading
import time

from . import cast, persona
from .providers import ProviderError
from .subjects import Subjects
from .voice import VoiceError


KINDS = ("argue", "chat", "roll")


def default_turns(members) -> int:
    """How long a talk runs when nobody says: about two lines each, at least 4 and at most 10."""
    return max(4, min(10, 2 * len(members)))


def plan_turns(members, turns: int, rng, first: str | None = None) -> list[str]:
    """Who says each of `turns` lines. `first` opens, or someone drawn by `rng` (a random.Random);
    after that whoever has spoken least goes next, never the same speaker twice running. So
    everyone has spoken before anyone speaks again, and two speakers simply take turns."""
    members = list(members)
    if first is not None and first not in members:
        raise ValueError(f"{first!r} can't open a conversation he isn't in")
    order: list[str] = []
    spoken = {who: 0 for who in members}
    for _ in range(turns):
        if not order and first is not None:
            who = first
        else:
            free = [w for w in members if not order or w != order[-1]]
            least = min(spoken[w] for w in free)
            who = rng.choice([w for w in free if spoken[w] == least])
        order.append(who)
        spoken[who] += 1
    return order


def check_talk(kind: str, members, first: str | None) -> tuple[tuple[str, ...], str | None]:
    """Who is talking and who opens, checked: the everyday pair when no cast is named, and with two
    speakers the first one named opens unless told otherwise. Raises ValueError for an unknown kind
    of conversation, a bad cast, or an opener who isn't in it."""
    if kind not in KINDS:
        raise ValueError(f"unknown kind of conversation: {kind!r}")
    members = cast.PAIR if members is None else cast.check_cast(members)
    if first is not None and first not in members:
        raise ValueError(f"{first!r} can't open a conversation he isn't in")
    if first is None and len(members) == 2:
        first = members[0]
    return members, first


class Debate:
    """A conversation among `cast` (the everyday pair when none is named): an argument about
    `topic` (kind "argue"), a chat about it (kind "chat"), or a roll call (kind "roll": one line
    each, in the order of the cast, however many `turns` were asked for). Two speakers take turns
    for `turns` lines, `first` opening; three or more speak in the order plan_turns draws. A worker
    thread writes each line once it is at most one ahead of the highest line anyone has asked for,
    so a conversation that is interrupted wastes one line at most."""

    def __init__(self, brain, topic: str, facts: list[str], turns: int, idle_s: float = 120,
                 clock=time.monotonic, poll_s: float = 1.0, kind: str = "argue", first: str | None = None,
                 cast=None, rng=None):
        self._cast, first = check_talk(kind, cast, first)
        self._kind = kind
        if kind == "roll":
            self._order = list(self._cast)
        else:
            self._order = plan_turns(self._cast, turns, rng or random.Random(), first)
        self.turns = len(self._order)
        self._brain, self._topic, self._facts = brain, topic, list(facts)
        self._idle, self._clock, self._poll = idle_s, clock, poll_s
        self._lines: list = []
        self._wanted = 0        # the highest line asked for so far
        self._stopped = False   # stopped, failed, or nobody came back for it
        self._error = None      # why it failed: "no-model", "no-voice" or "internal"
        self._touched = clock()
        self._cond = threading.Condition()
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def _messages(self, who: str, so_far: list) -> list[dict]:
        """The argument so far as `who` sees it: his own lines are his, the other's are put to him.
        It opens with a word from outside when he spoke first: some models (Claude) refuse a
        conversation that starts with their own words."""
        messages = [{"role": "assistant" if line.who == who else "user", "content": line.text} for line in so_far]
        if not messages or messages[0]["role"] != "user":
            messages.insert(0, {"role": "user", "content": "Begin."})
        return messages

    def _asked(self, who: str, n: int, so_far: list) -> tuple[str, list[dict]]:
        """What the model is told, and the talk so far as it is put to him, for line `n`."""
        opening, last = n == 0, n == self.turns - 1
        group = [{"role": "user", "content": persona.group_message(who, so_far)}]
        if self._kind == "roll":
            return persona.roll_prompt(who, self._cast, self._facts), group
        if len(self._cast) > 2:  # too many voices for "his lines and mine": one labelled transcript
            if self._kind == "chat":
                return persona.group_chat_prompt(who, self._cast, self._topic, self._facts, opening, last), group
            return persona.group_debate_prompt(who, self._cast, self._topic, self._facts, last), group
        rival = self._cast[1] if who == self._cast[0] else self._cast[0]
        if self._kind == "chat":
            system = persona.chat_prompt(who, self._topic, self._facts, opening=opening, last=last, rival=rival)
        else:
            system = persona.debate_prompt(who, self._topic, self._facts, last=last, rival=rival)
        return system, self._messages(who, so_far)

    def _run(self) -> None:
        while True:
            with self._cond:
                while not self._stopped and len(self._lines) < self.turns and len(self._lines) > self._wanted + 1:
                    if self._clock() - self._touched > self._idle:
                        self._stopped = True  # nobody is collecting this argument any more
                        break
                    self._cond.wait(self._poll)
                if self._stopped or len(self._lines) >= self.turns:
                    self._cond.notify_all()
                    return
                n, so_far = len(self._lines), list(self._lines)
            who = self._order[n]
            try:
                system, messages = self._asked(who, n, so_far)
                line = self._brain.speak(who, system, messages, allow_show=False)
            except Exception as e:  # the model or the voice failed: the argument stops here, and says why
                known = isinstance(e, (ProviderError, VoiceError))  # these explain themselves, without secrets
                why = "".join(ch if " " <= ch <= "~" else " " for ch in (str(e) if known else type(e).__name__))
                print(f"The argument stopped at line {n}: {why[:200]}", file=sys.stderr, flush=True)
                with self._cond:
                    self._error = ("no-model" if isinstance(e, ProviderError) else
                                   "no-voice" if isinstance(e, VoiceError) else "internal")
                self.stop()
                return
            with self._cond:
                if self._stopped:
                    return
                self._lines.append(line)
                self._cond.notify_all()

    def get(self, n: int, timeout_s: float = 20):
        """("line", Line) once line n exists; ("wait", None) if it still doesn't after
        `timeout_s`; ("end", None) past the last line or after the argument was stopped;
        ("failed", "no-model" | "no-voice" | "internal") when the line will never come."""
        deadline = time.monotonic() + timeout_s
        with self._cond:
            self._touched = self._clock()
            if n < 0 or n >= self.turns:
                return "end", None
            if n > self._wanted:
                self._wanted = n
                self._cond.notify_all()
            while n >= len(self._lines):
                if self._stopped:
                    return ("failed", self._error) if self._error else ("end", None)
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    return "wait", None
                self._cond.wait(remaining)
            return "line", self._lines[n]

    def stop(self) -> None:
        with self._cond:
            self._stopped = True
            self._cond.notify_all()

    def join(self, timeout_s: float) -> None:
        """Wait for the worker to finish (for tests)."""
        self._thread.join(timeout_s)


class Debates:
    """The arguments the server knows about. Starting one ends the one before (there is one HAL
    Zero); each is forgotten ten minutes after it began."""

    def __init__(self, brain, keep_s: float = 600, clock=time.monotonic, subjects=None):
        self._brain, self._keep, self._clock = brain, keep_s, clock
        self._subjects = subjects or Subjects()  # what a chat is about when nobody said
        self._debates: dict[str, tuple[float, Debate]] = {}
        self._lock = threading.Lock()

    def _forget_old(self) -> None:
        now = self._clock()
        for debate_id in [i for i, (began, _) in self._debates.items() if now - began > self._keep]:
            self._debates.pop(debate_id)[1].stop()

    def start(self, topic: str, facts: list[str], turns: int, kind: str = "argue", first: str | None = None,
              cast=None) -> str:
        check_talk(kind, cast, first)  # refuses a bad cast or opener before anything is stopped
        if kind == "chat" and not topic:
            topic = self._subjects.pick()
            print(f"The HALs are chatting about: {topic}", file=sys.stderr, flush=True)
        if not topic and kind != "roll":
            raise ValueError("an argument needs a subject")
        debate_id = secrets.token_hex(8)
        with self._lock:
            self._forget_old()
            for _, running in self._debates.values():
                running.stop()
            self._debates[debate_id] = (self._clock(), Debate(self._brain, topic, facts, turns, clock=self._clock,
                                                              kind=kind, first=first, cast=cast))
        return debate_id

    def get(self, debate_id) -> Debate | None:
        with self._lock:
            self._forget_old()
            found = self._debates.get(debate_id) if isinstance(debate_id, str) else None
        return found[1] if found else None
