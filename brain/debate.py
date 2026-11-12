"""The two HALs arguing live: lines are written one ahead of the one being collected."""
import secrets
import threading
import time

from . import persona


class Debate:
    """HAL Zero opens (line 0), then they alternate for `turns` lines. A worker thread writes
    each line once it is at most one ahead of the highest line anyone has asked for, so an
    argument that is interrupted wastes one line at most."""

    def __init__(self, brain, topic: str, facts: list[str], turns: int, idle_s: float = 120,
                 clock=time.monotonic, poll_s: float = 1.0):
        self.turns = turns
        self._brain, self._topic, self._facts = brain, topic, list(facts)
        self._idle, self._clock, self._poll = idle_s, clock, poll_s
        self._lines: list = []
        self._wanted = 0        # the highest line asked for so far
        self._stopped = False   # stopped, failed, or nobody came back for it
        self._touched = clock()
        self._cond = threading.Condition()
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def _messages(self, who: str, so_far: list) -> list[dict]:
        if not so_far:
            return [{"role": "user", "content": "Begin."}]
        return [{"role": "assistant" if line.who == who else "user", "content": line.text} for line in so_far]

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
            who = "zero" if n % 2 == 0 else "desk"
            try:
                system = persona.debate_prompt(who, self._topic, self._facts, last=n == self.turns - 1)
                line = self._brain.speak(who, system, self._messages(who, so_far), allow_show=False)
            except Exception:  # the model or the voice failed: the argument ends here
                self.stop()
                return
            with self._cond:
                if self._stopped:
                    return
                self._lines.append(line)
                self._cond.notify_all()

    def get(self, n: int, timeout_s: float = 20):
        """("line", Line) once line n exists, ("wait", None) if it still doesn't after
        `timeout_s`, ("end", None) past the last line or after the argument stopped."""
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
                    return "end", None
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

    def __init__(self, brain, keep_s: float = 600, clock=time.monotonic):
        self._brain, self._keep, self._clock = brain, keep_s, clock
        self._debates: dict[str, tuple[float, Debate]] = {}
        self._lock = threading.Lock()

    def _forget_old(self) -> None:
        now = self._clock()
        for debate_id in [i for i, (began, _) in self._debates.items() if now - began > self._keep]:
            self._debates.pop(debate_id)[1].stop()

    def start(self, topic: str, facts: list[str], turns: int) -> str:
        debate_id = secrets.token_hex(8)
        with self._lock:
            self._forget_old()
            for _, running in self._debates.values():
                running.stop()
            self._debates[debate_id] = (self._clock(), Debate(self._brain, topic, facts, turns, clock=self._clock))
        return debate_id

    def get(self, debate_id) -> Debate | None:
        with self._lock:
            self._forget_old()
            found = self._debates.get(debate_id) if isinstance(debate_id, str) else None
        return found[1] if found else None
