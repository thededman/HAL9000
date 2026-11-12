"""The brain's front door: a small HTTP server that speaks key=value text.

    tools/.venv/bin/python -m brain.server        (or: tools/brain.sh start)

See docs/superpowers/specs/2026-11-11-ai-brain-design.md, section 1, for the requests. With a
shared key in brain/.env it listens on the network (HAL Zero sends the key in X-HAL-Key);
without one, on this computer only.
"""
import hmac
import re
import sys
import threading
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from . import persona
from .providers import ProviderError
from .stt import SttError
from .voice import VoiceError

MAX_TEXT_BODY = 4096
MAX_AUDIO_BODY = 1024 * 1024
MIN_TURNS, MAX_TURNS, DEFAULT_TURNS = 2, 12, 8
_CLIP = re.compile(r"/clip/([0-9a-f]{32})")
_DEBATE_LINE = re.compile(r"/debate/([0-9a-f]{16})/(\d{1,2})")
_DEBATE_STOP = re.compile(r"/debate/([0-9a-f]{16})/stop")
_TEXT = "text/plain; charset=utf-8"


def format_reply(pairs: dict) -> bytes:
    """key=value lines. A value never spans lines; a None value is left out."""
    return "".join(f"{k}={' '.join(str(v).split())}\n" for k, v in pairs.items() if v is not None).encode("utf-8")


class Handler(BaseHTTPRequestHandler):
    server_version = "HALBrain"
    sys_version = ""
    timeout = 10  # seconds a client may take to send its request

    # ----- plumbing -----

    def _send(self, status: int, body: bytes = b"", content_type: str = _TEXT) -> None:
        self.send_response(status)
        if body:
            self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(body)

    def _reply(self, pairs: dict, status: int = 200) -> None:
        self._send(status, format_reply(pairs))

    def _refuse(self, status: int, code: str) -> None:
        """An error, sent before the request's body was read: swallow a bounded amount of it so
        the client gets the answer rather than a reset connection."""
        self._send(status, format_reply({"error": code}))
        try:
            left = min(int(self.headers.get("Content-Length") or 0), 2 * MAX_AUDIO_BODY)
            self.connection.settimeout(2)
            while left > 0:
                chunk = self.rfile.read(min(left, 65536))
                if not chunk:
                    break
                left -= len(chunk)
        except (OSError, ValueError):
            pass

    def _authorised(self) -> bool:
        key = self.server.hal_key
        if not key:
            return True  # not paired: only this computer can reach us (see make_server)
        return hmac.compare_digest((self.headers.get("X-HAL-Key") or "").encode(), key.encode())

    def _body(self, limit: int) -> bytes | None:
        """The request body, or None after answering 411, 413 or 400."""
        length = self.headers.get("Content-Length")
        if length is None or not length.isdigit():
            self._send(411, format_reply({"error": "no-length"}))
            return None
        if int(length) > limit:
            self._refuse(413, "too-large")
            return None
        body = self.rfile.read(int(length))
        if len(body) != int(length):
            self._send(400, format_reply({"error": "bad-request"}))
            return None
        return body

    def log_message(self, fmt, *args):  # method, path and status only; clip tokens aren't written down
        if not self.server.hal_log:
            return
        line = re.sub(r"/clip/[0-9a-f]+", "/clip/...", fmt % args)
        sys.stderr.write(f"{self.log_date_time_string()} {line}\n")

    # ----- requests -----

    def do_GET(self):
        path = urllib.parse.urlsplit(self.path).path
        if path == "/health":
            return self._reply({"ok": 1})
        clip = _CLIP.fullmatch(path)
        if clip:
            wav = self.server.hal_brain.clips.get(clip.group(1))
            return self._send(200, wav, "audio/wav") if wav else self._refuse(404, "not-found")
        line = _DEBATE_LINE.fullmatch(path)
        if not line:
            return self._refuse(404, "not-found")
        if not self._authorised():
            return self._refuse(401, "bad-key")
        debate = self.server.hal_debates.get(line.group(1))
        if debate is None:
            return self._refuse(404, "not-found")
        n = int(line.group(2))
        status, said = debate.get(n, timeout_s=self.server.hal_long_poll)
        if status == "wait":
            return self._reply({"wait": 1}, 202)
        if status == "end":
            return self._send(204)
        self._reply({"who": said.who, "text": said.text, "clip": said.clip, "ms": said.ms,
                     "last": int(n == debate.turns - 1)})

    def do_POST(self):
        url = urllib.parse.urlsplit(self.path)
        query = urllib.parse.parse_qs(url.query)
        stop = _DEBATE_STOP.fullmatch(url.path)
        if url.path not in ("/ask", "/hear", "/debate") and not stop:
            return self._refuse(404, "not-found")
        if not self._authorised():
            return self._refuse(401, "bad-key")
        body = self._body(MAX_AUDIO_BODY if url.path == "/hear" else MAX_TEXT_BODY)
        if body is None:
            return
        try:
            if stop:
                debate = self.server.hal_debates.get(stop.group(1))
                if debate is None:
                    return self._refuse(404, "not-found")
                debate.stop()
                return self._reply({"ok": 1})
            if url.path == "/hear":
                try:
                    return self._reply({"heard": self.server.hal_brain.hear(body)})
                except ValueError:
                    return self._refuse(400, "bad-audio")
            facts, question = persona.parse_body(body)
            if url.path == "/ask":
                said = self.server.hal_brain.ask(query.get("who", [""])[0], facts, question)
                return self._reply({"text": said.text, "clip": said.clip, "ms": said.ms, "show": said.show})
            turns = int(query.get("turns", [str(DEFAULT_TURNS)])[0])
            if not MIN_TURNS <= turns <= MAX_TURNS:
                raise ValueError("turns out of range")
            self._reply({"debate": self.server.hal_debates.start(question, facts, turns)})
        except ValueError:  # an unknown HAL, no question, not text, a bad number of turns
            self._refuse(400, "bad-request")
        except ProviderError:
            self._refuse(503, "no-model")
        except VoiceError:
            self._refuse(503, "no-voice")
        except SttError:
            self._refuse(503, "no-ears")


def make_server(brain, debates, key: str | None, port: int = 9100, long_poll_s: float = 20.0,
                log: bool = True) -> ThreadingHTTPServer:
    """The server, not yet serving. Without a key it is reachable from this computer only."""
    server = ThreadingHTTPServer(("0.0.0.0" if key else "127.0.0.1", port), Handler)
    server.daemon_threads = True
    server.hal_log = log
    server.hal_brain, server.hal_debates, server.hal_key, server.hal_long_poll = brain, debates, key or None, long_poll_s
    return server


def main() -> None:
    from .clips import ClipStore
    from .config import port_from, read_env
    from .core import Brain
    from .debate import Debates
    from .memory import ChatMemory
    from .providers import make_provider
    from .stt import Whisper
    from .voice import Voice

    env = read_env()
    key = env.get("HAL_BRAIN_KEY", "").strip() or None
    try:
        provider = make_provider(env)
        voice = Voice()
    except (ValueError, VoiceError) as e:
        sys.exit(f"HAL's brain can't start: {e}")
    ears = Whisper()
    brain = Brain(provider, voice, ears, ClipStore(), ChatMemory())
    try:
        server = make_server(brain, Debates(brain), key, port_from(env))
    except OSError as e:
        sys.exit(f"HAL's brain can't start: port {port_from(env)} is in use ({e})")
    threading.Thread(target=lambda: (provider.warm(), ears.warm()), daemon=True).start()
    where = "the network (paired)" if key else "this computer only (not paired yet)"
    print(f"HAL's brain is listening on port {server.server_address[1]}, {where}; "
          f"model: {provider.name} {provider.model}", file=sys.stderr, flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
