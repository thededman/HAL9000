"""What each HAL remembers of the chat so far: the last few messages, until it has been quiet a while."""
import threading
import time


class ChatMemory:
    def __init__(self, max_messages: int = 12, idle_s: float = 600, clock=time.monotonic):
        self._max, self._idle, self._clock = max_messages, idle_s, clock
        self._chats: dict[str, tuple[float, list[dict]]] = {}  # who -> (last used, messages oldest first)
        self._lock = threading.Lock()

    def _live(self, who: str) -> list[dict]:
        used, messages = self._chats.get(who, (0.0, []))
        if messages and self._clock() - used > self._idle:  # the conversation went quiet: forget it
            del self._chats[who]
            return []
        return messages

    def history(self, who: str) -> list[dict]:
        """The messages so far as {"role": "user"|"assistant", "content": ...}; empty after a long silence."""
        with self._lock:
            return list(self._live(who))

    def add(self, who: str, question: str, answer: str) -> None:
        with self._lock:
            messages = self._live(who) + [{"role": "user", "content": question},
                                          {"role": "assistant", "content": answer}]
            self._chats[who] = (self._clock(), messages[-self._max:])
