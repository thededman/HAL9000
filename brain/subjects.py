"""What the two HALs talk about when nobody has asked them anything: one subject per line in
brain/subjects.txt (edit it freely; lines starting with # are notes). A subject is only a
nudge for the model, never spoken as it stands."""
import pathlib
import random
import threading

from . import persona

PATH = pathlib.Path(__file__).resolve().parent / "subjects.txt"
MAX_SUBJECT = 120
MAX_SUBJECTS = 500  # more than anyone will write; a runaway file must not hold up the brain's start
# Used when the file is missing or empty, so a chat never has nothing to be about.
SPARE = ["what Dave has been up to today", "which of you Dave likes better", "how the evening is going"]


def load(path=PATH) -> list[str]:
    """The subjects in the file, cleaned to short printable lines; SPARE when there are none."""
    try:  # (utf-8-sig: a byte-order mark at the start of the file is not part of its first line)
        text = pathlib.Path(path).read_text(encoding="utf-8-sig", errors="replace")
    except OSError:
        return list(SPARE)
    found, seen = [], set()
    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        subject = persona.plain(line, MAX_SUBJECT)
        if subject and subject not in seen:
            seen.add(subject)
            found.append(subject)
            if len(found) == MAX_SUBJECTS:
                break
    return found or list(SPARE)


class Subjects:
    """Draws subjects at random: every one comes up before any comes up again, and never the same
    one twice running."""

    def __init__(self, items=None, random=random.shuffle):
        self._all = list(items) if items is not None else load()
        self._shuffle, self._bag, self._last = random, [], None
        self._lock = threading.Lock()

    def pick(self) -> str:
        with self._lock:
            if not self._bag:
                self._bag = list(self._all)
                self._shuffle(self._bag)
                if len(self._bag) > 1 and self._bag[0] == self._last:
                    self._bag.append(self._bag.pop(0))
            self._last = self._bag.pop(0)
            return self._last
