"""Spoken lines waiting to be fetched: kept in memory, each under a random token, for ten minutes."""
import re
import secrets
import threading
import time
from collections import OrderedDict

_TOKEN = re.compile(r"[0-9a-f]{32}")


class ClipStore:
    def __init__(self, ttl_s: float = 600, max_clips: int = 64, max_bytes: int = 40 * 1024 * 1024,
                 clock=time.monotonic):
        self._ttl, self._max_clips, self._max_bytes, self._clock = ttl_s, max_clips, max_bytes, clock
        self._clips: "OrderedDict[str, tuple[float, bytes]]" = OrderedDict()  # oldest first
        self._bytes = 0
        self._lock = threading.Lock()

    def _drop_oldest(self) -> None:
        _, (_, wav) = self._clips.popitem(last=False)
        self._bytes -= len(wav)

    def _expire(self) -> None:
        now = self._clock()
        while self._clips and now - next(iter(self._clips.values()))[0] > self._ttl:
            self._drop_oldest()

    def put(self, wav: bytes) -> str:
        """Keep a clip; returns the token to fetch it by (32 hex characters)."""
        token = secrets.token_hex(16)
        with self._lock:
            self._expire()
            self._clips[token] = (self._clock(), wav)
            self._bytes += len(wav)
            while len(self._clips) > self._max_clips or (self._bytes > self._max_bytes and len(self._clips) > 1):
                self._drop_oldest()
        return token

    def get(self, token) -> bytes | None:
        """The clip, or None when the token is unknown, malformed or older than ten minutes."""
        if not isinstance(token, str) or not _TOKEN.fullmatch(token):
            return None
        with self._lock:
            self._expire()
            found = self._clips.get(token)
        return found[1] if found else None
