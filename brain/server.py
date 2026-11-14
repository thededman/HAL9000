"""The brain's front door: a small HTTP server that speaks key=value text.

    tools/.venv/bin/python -m brain.server        (or: tools/brain.sh start)

See docs/superpowers/specs/2026-11-11-ai-brain-design.md, section 1, for the requests. With a
shared key in brain/.env it listens on the network (HAL Zero sends the key in X-HAL-Key);
without one, on this computer only.

It will sit on a home network, so it assumes strangers: a request must arrive whole within a
deadline, only so many are handled at once, nothing is read past its size limit, the log holds
short printable lines without keys or clip tokens, and whatever goes wrong inside is answered
with one line, never a traceback.
"""
import hmac
import re
import socket
import sys
import threading
import time
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from . import cast, persona
from .config import valid_key
from .debate import default_turns
from .providers import ProviderError
from .stt import SttError
from .voice import VoiceError

MAX_TEXT_BODY = 4096
MAX_AUDIO_BODY = 1024 * 1024
MIN_TURNS, MAX_TURNS = 2, 12
DEFAULT_TURNS = 8      # for the everyday pair; a cast that is named gets about two lines each
READ_DEADLINE_S = 15   # a whole request (headers and body) must arrive within this
MAX_HANDLERS = 16      # requests handled at once; HAL Zero and the desk HAL need three or four
MAX_LOG_CHARS = 200
_CLIP = re.compile(r"/clip/([0-9a-f]{32})")
_DEBATE_LINE = re.compile(r"/debate/([0-9a-f]{16})/(\d{1,2})")
_DEBATE_STOP = re.compile(r"/debate/([0-9a-f]{16})/stop")
_TEXT = "text/plain; charset=utf-8"
_LOCAL_HOSTS = ("127.0.0.1", "localhost", "::1")
_UNPRINTABLE = {c: f"\\x{c:02x}" for c in list(range(0x20)) + list(range(0x7f, 0xa0))}


def format_reply(pairs: dict) -> bytes:
    """key=value lines. A value never spans lines; a None value is left out."""
    return "".join(f"{k}={' '.join(str(v).split())}\n" for k, v in pairs.items() if v is not None).encode("utf-8")


class Handler(BaseHTTPRequestHandler):
    server_version = "HALBrain"
    sys_version = ""
    timeout = 10  # seconds any single read or write may take

    # ----- one connection -----

    def setup(self):
        super().setup()
        self._body_read = False
        # However slowly it trickles in, the request has to be here by the deadline.
        self._deadline = threading.Timer(self.server.hal_read_deadline, self._hang_up)
        self._deadline.daemon = True
        self._deadline.start()

    def _hang_up(self):
        try:
            self.connection.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass

    def _request_is_in(self):
        """The whole request has arrived: the deadline for sending it no longer applies."""
        self._deadline.cancel()

    def handle(self):
        if not self.server.hal_slots.acquire(blocking=False):
            return  # too many at once: this one is turned away at the door
        try:
            super().handle()
        except OSError:
            pass  # the other end went away, or was cut off for being slow
        finally:
            self.server.hal_slots.release()

    def finish(self):
        self._deadline.cancel()
        try:
            super().finish()
        except OSError:
            pass

    # ----- plumbing -----

    def _send(self, status: int, body: bytes = b"", content_type: str = _TEXT) -> None:
        self.send_response(status)
        if body:
            self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(body)
        self.close_connection = True

    def _reply(self, pairs: dict, status: int = 200) -> None:
        self._send(status, format_reply(pairs))

    def _refuse(self, status: int, code: str, why: str = "") -> None:
        """One line saying what was wrong. If the request's body hasn't been read, a bounded
        amount of it is swallowed (for two seconds at most) so the sender sees the answer rather
        than a reset connection."""
        if why:
            self.log_message("%d %s: %s", status, code, why)
        self._send(status, format_reply({"error": code}))
        if self._body_read:
            return
        try:
            length = self.headers.get("Content-Length") or "0"
            left = min(int(length), 2 * MAX_AUDIO_BODY) if length.isascii() and length.isdigit() and len(length) <= 8 else 0
            give_up = time.monotonic() + 2
            self.connection.settimeout(0.5)
            while left > 0 and time.monotonic() < give_up:
                chunk = self.rfile.read(min(left, 65536))
                if not chunk:
                    break
                left -= len(chunk)
        except (OSError, ValueError):
            pass

    def send_error(self, code, message=None, explain=None):
        """The library's own refusals (an unknown method, an over-long request line) in our format."""
        try:
            self._send(code, format_reply({"error": "bad-request"}))
        except OSError:
            pass

    def _allowed(self) -> bool:
        """Is this request allowed in? Answers 401 or 403 itself when it isn't."""
        key = self.server.hal_key
        if key:
            if hmac.compare_digest((self.headers.get("X-HAL-Key") or "").encode(), key.encode()):
                return True
            self._refuse(401, "bad-key")
            return False
        # Not paired: only this computer can reach us. A web page open in a browser here could
        # still post to 127.0.0.1, but it can't hide where it came from.
        host = (self.headers.get("Host") or "").strip().lower()
        host = host[:host.rindex(":")] if ":" in host and not host.endswith("]") else host
        if self.headers.get("Origin") is not None or host.strip("[]") not in _LOCAL_HOSTS:
            self._refuse(403, "local-only")
            return False
        return True

    def _body(self, limit: int) -> bytes | None:
        """The request body, or None after answering 411, 413 or 400."""
        length = self.headers.get("Content-Length")
        if length is None or not (length.isascii() and length.isdigit()):
            self._refuse(411, "no-length")
            return None
        if len(length) > 8 or int(length) > limit:
            self._refuse(413, "too-large")
            return None
        body = self.rfile.read(int(length))
        self._body_read = True
        if len(body) != int(length):
            self._refuse(400, "bad-request")
            return None
        return body

    def log_message(self, fmt, *args):
        """Method, path and status only, as one short printable line; clip tokens aren't written down."""
        sink = self.server.hal_log
        if not sink:
            return
        line = re.sub(r"/clip/[0-9a-f]+", "/clip/...", fmt % args).translate(_UNPRINTABLE)
        sink(f"{self.log_date_time_string()} {line[:MAX_LOG_CHARS]}")

    def _guarded(self, work) -> None:
        """Run a request; whatever goes wrong inside is one log line and a 500, never a traceback."""
        try:
            work()
        except (ConnectionError, TimeoutError):
            raise  # the other end went away
        except Exception as e:  # including trouble on this computer, such as a full disk
            self.log_message("internal error: %s", type(e).__name__)
            try:
                self._send(500, format_reply({"error": "internal"}))
            except OSError:
                pass

    def do_GET(self):
        self._guarded(self._get)

    def do_POST(self):
        self._guarded(self._post)

    # ----- requests -----

    def _get(self):
        self._request_is_in()
        try:
            path = urllib.parse.urlsplit(self.path).path
        except ValueError:
            return self._refuse(400, "bad-request")
        if path == "/health":
            return self._reply({"ok": 1})
        clip = _CLIP.fullmatch(path)
        if clip:
            wav = self.server.hal_brain.clips.get(clip.group(1))
            return self._send(200, wav, "audio/wav") if wav else self._refuse(404, "not-found")
        line = _DEBATE_LINE.fullmatch(path)
        if not line:
            return self._refuse(404, "not-found")
        if not self._allowed():
            return
        debate = self.server.hal_debates.get(line.group(1))
        if debate is None:
            return self._refuse(404, "not-found")
        n = int(line.group(2))
        status, said = debate.get(n, timeout_s=self.server.hal_long_poll)
        if status == "wait":
            return self._reply({"wait": 1}, 202)
        if status == "end":
            return self._send(204)
        if status == "failed":  # the model or the voice gave out: not the same as finished
            return self._refuse(503, said)
        self._reply({"who": said.who, "text": said.text, "clip": said.clip or None, "ms": said.ms,
                     "last": int(n == debate.turns - 1)})  # no clip: his words are shown, not spoken

    def _post(self):
        try:
            url = urllib.parse.urlsplit(self.path)
            query = urllib.parse.parse_qs(url.query)
        except ValueError:
            return self._refuse(400, "bad-request")
        stop = _DEBATE_STOP.fullmatch(url.path)
        if url.path not in ("/ask", "/hear", "/debate") and not stop:
            return self._refuse(404, "not-found")
        if not self._allowed():
            return
        body = self._body(MAX_AUDIO_BODY if url.path == "/hear" else MAX_TEXT_BODY)
        if body is None:
            return
        self._request_is_in()
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
            kind = query.get("kind", ["argue"])[0] if url.path == "/debate" else ""
            # The brain can pick a chat's subject, and a roll call has none.
            facts, question = persona.parse_body(body, need_question=kind not in ("chat", "roll"))
            if url.path == "/ask":
                said = self.server.hal_brain.ask(query.get("who", [""])[0], facts, question)
                return self._reply({"who": said.who, "text": said.text, "clip": said.clip or None, "ms": said.ms,
                                    "show": said.show})
            # Who is talking: cast=zero,desk,nano names them; without it, the everyday pair.
            members = cast.parse_cast(query["cast"][0]) if "cast" in query else None
            turns = int(query.get("turns", [str(default_turns(members) if members else DEFAULT_TURNS)])[0])
            if not MIN_TURNS <= turns <= MAX_TURNS:
                raise ValueError("turns out of range")
            first = query.get("first", [None])[0]
            self._reply({"debate": self.server.hal_debates.start(question, facts, turns, kind=kind, first=first,
                                                                 cast=members)})
        except ValueError:  # an unknown HAL, no question, not text, a bad number of turns, kind, cast or opener
            self._refuse(400, "bad-request")
        except ProviderError as e:
            self._refuse(503, "no-model", str(e))
        except VoiceError as e:
            self._refuse(503, "no-voice", str(e))
        except SttError as e:
            self._refuse(503, "no-ears", str(e))


def make_server(brain, debates, key: str | None, port: int = 9100, long_poll_s: float = 20.0, log=True,
                read_deadline_s: float = READ_DEADLINE_S, max_handlers: int = MAX_HANDLERS) -> ThreadingHTTPServer:
    """The server, not yet serving. Without a key it is reachable from this computer only.

    `log` is True (standard error), False, or something to call with each log line."""
    server = ThreadingHTTPServer(("0.0.0.0" if key else "127.0.0.1", port), Handler)
    server.daemon_threads = True
    server.hal_brain, server.hal_debates, server.hal_key, server.hal_long_poll = brain, debates, key or None, long_poll_s
    server.hal_read_deadline = read_deadline_s
    server.hal_slots = threading.BoundedSemaphore(max_handlers)
    server.hal_log = (lambda line: print(line, file=sys.stderr, flush=True)) if log is True else (log or None)
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
    if key and not valid_key(key):
        print("HAL_BRAIN_KEY in brain/.env isn't 32 lower-case hex characters, so it is ignored and the brain "
              "stays on this computer only. Delete that line and run tools/brain.sh pair.", file=sys.stderr, flush=True)
        key = None
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

    def warm_up():
        if provider.warm() is False:
            print(f"The model isn't answering yet ({provider.name} {provider.model}): {provider.hint} "
                  "Questions will get error=no-model until it is.", file=sys.stderr, flush=True)
        ears.warm()

    threading.Thread(target=warm_up, daemon=True).start()
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
