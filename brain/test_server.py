import http.client
import socket
import threading
import time
import unittest

from brain.clips import ClipStore
from brain.core import Brain
from brain.debate import Debates
from brain.fakes import FakeEars, FakeProvider, FakeVoice, make_wav
from brain.memory import ChatMemory
from brain.server import format_reply, make_server

KEY = "0123456789abcdef0123456789abcdef"


def fields(body: bytes) -> dict:
    return dict(line.split("=", 1) for line in body.decode().splitlines() if "=" in line)


class ServerCase(unittest.TestCase):
    key = KEY
    long_poll_s = 5.0
    read_deadline_s = 15.0
    max_handlers = 16

    def setUp(self):
        self.provider, self.voice, self.ears = FakeProvider(), FakeVoice(), FakeEars()
        self.brain = Brain(self.provider, self.voice, self.ears, ClipStore(), ChatMemory())
        self.log: list[str] = []
        self.server = make_server(self.brain, Debates(self.brain), self.key, port=0, long_poll_s=self.long_poll_s,
                                  log=self.log.append, read_deadline_s=self.read_deadline_s,
                                  max_handlers=self.max_handlers)
        self.port = self.server.server_address[1]
        self.thread = threading.Thread(target=self.server.serve_forever, kwargs={"poll_interval": 0.01}, daemon=True)
        self.thread.start()

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(5)

    def call(self, method, path, body=None, key=KEY, headers=None, length=True):
        conn = http.client.HTTPConnection("127.0.0.1", self.port, timeout=10)
        try:
            conn.putrequest(method, path, skip_host="Host" in (headers or {}))
            if key is not None:
                conn.putheader("X-HAL-Key", key)
            for name, value in (headers or {}).items():
                conn.putheader(name, value)
            if body is not None and length:
                conn.putheader("Content-Length", str(len(body)))
            conn.endheaders()
            if body:
                conn.send(body)
            reply = conn.getresponse()
            return reply.status, reply.read(), reply.getheader("Content-Type", "")
        finally:
            conn.close()


    def raw(self, data: bytes, hold_s: float = 5.0) -> tuple[bytes, float]:
        """Send bytes as they are; returns everything the server sent before it closed, and how long that took."""
        started = time.monotonic()
        with socket.create_connection(("127.0.0.1", self.port), timeout=hold_s) as sock:
            sock.sendall(data)
            got = b""
            try:
                while True:
                    chunk = sock.recv(65536)
                    if not chunk:
                        break
                    got += chunk
            except (TimeoutError, ConnectionError):
                pass
        return got, time.monotonic() - started


class RoundTripTest(ServerCase):
    def test_format_reply(self):
        self.assertEqual(format_reply({"text": "a\nb", "ms": 1200}), b"text=a b\nms=1200\n")
        self.assertEqual(format_reply({"text": "x", "show": None}), b"text=x\n")

    def test_health_needs_no_key(self):
        status, body, kind = self.call("GET", "/health", key=None)
        self.assertEqual((status, body), (200, b"ok=1\n"))
        self.assertTrue(kind.startswith("text/plain"))

    def test_ask_round_trip(self):
        self.provider.replies = ["I am a cool 47 C."]
        status, body, _ = self.call("POST", "/ask?who=zero", b"temp: 47 C\n\nHow hot are you?")
        got = fields(body)
        self.assertEqual(status, 200)
        self.assertEqual((got["text"], got["ms"]), ("I am a cool 47 C.", "1000"))
        self.assertNotIn("show", got)
        self.assertIn("temp: 47 C", self.provider.calls[0][0])
        status, wav, kind = self.call("GET", "/clip/" + got["clip"], key=None)  # the desk HAL has no key
        self.assertEqual((status, wav, kind), (200, self.voice.wav, "audio/wav"))

    def test_ask_show_line(self):
        self.provider.replies = ["Look. [SHOW wall]"]
        got = fields(self.call("POST", "/ask?who=desk", b"\nShow me the wall")[1])
        self.assertEqual((got["text"], got["show"]), ("Look.", "wall"))

    def test_hear_round_trip(self):
        status, body, _ = self.call("POST", "/hear", make_wav(1.0))
        self.assertEqual((status, body), (200, b"heard=hello there\n"))
        self.ears.text = "[BLANK_AUDIO]"
        self.assertEqual(self.call("POST", "/hear", make_wav(1.0))[1], b"heard=\n")

    def test_debate_round_trip(self):
        self.provider.replies = ["Zero opens.", "Desk ends."]
        status, body, _ = self.call("POST", "/debate?turns=2", b"time: 16:42\n\nwho makes better tea")
        self.assertEqual(status, 200)
        debate = fields(body)["debate"]
        self.assertRegex(debate, r"^[0-9a-f]{16}$")
        first = fields(self.call("GET", f"/debate/{debate}/0")[1])
        self.assertEqual((first["who"], first["text"], first["last"], first["ms"]), ("zero", "Zero opens.", "0", "1000"))
        self.assertEqual(self.call("GET", "/clip/" + first["clip"], key=None)[0], 200)
        second = fields(self.call("GET", f"/debate/{debate}/1")[1])
        self.assertEqual((second["who"], second["text"], second["last"]), ("desk", "Desk ends.", "1"))
        status, body, _ = self.call("GET", f"/debate/{debate}/2")
        self.assertEqual((status, body), (204, b""))
        self.assertEqual(self.call("POST", f"/debate/{debate}/stop", b"")[1], b"ok=1\n")
        self.assertIn("who makes better tea", self.provider.calls[0][0])

    def test_debate_default_turns(self):
        debate = fields(self.call("POST", "/debate", b"\ntea")[1])["debate"]
        self.assertEqual(self.server.hal_debates.get(debate).turns, 8)

    def test_503_then_still_serving(self):
        self.provider.fail_on = {1}
        status, body, _ = self.call("POST", "/ask?who=zero", b"\nHello?")
        self.assertEqual((status, body), (503, b"error=no-model\n"))
        self.assertEqual(self.call("POST", "/ask?who=zero", b"\nHello again?")[0], 200)


class RefusalTest(ServerCase):
    def test_rejects_missing_or_wrong_key(self):
        for method, path, body in (("POST", "/ask?who=zero", b"\nhi"), ("POST", "/hear", make_wav(1.0)),
                                   ("POST", "/debate", b"\ntea"), ("GET", "/debate/" + "0" * 16 + "/0", None),
                                   ("POST", "/debate/" + "0" * 16 + "/stop", b"")):
            for key in (None, "", "wrong", KEY[:-1], KEY + "0"):
                status, reply, _ = self.call(method, path, body, key=key)
                self.assertEqual((status, reply), (401, b"error=bad-key\n"), f"{method} {path} {key!r}")
        self.assertEqual(self.provider.calls, [])
        self.assertEqual(self.ears.heard, 0)

    def test_rejects_no_length(self):
        status, reply, _ = self.call("POST", "/ask?who=zero", b"", length=False)
        self.assertEqual((status, reply), (411, b"error=no-length\n"))
        status, _, _ = self.call("POST", "/ask?who=zero", b"", headers={"Content-Length": "lots"}, length=False)
        self.assertEqual(status, 411)

    def test_rejects_too_large(self):
        status, reply, _ = self.call("POST", "/ask?who=zero", b"\n" + b"q" * 4096)
        self.assertEqual((status, reply), (413, b"error=too-large\n"))
        self.assertEqual(self.call("POST", "/debate", b"\n" + b"q" * 4096)[0], 413)
        self.assertEqual(self.call("POST", "/hear", b"x" * (1024 * 1024 + 1))[0], 413)
        self.assertEqual(self.call("POST", "/ask?who=zero", b"\n" + b"q" * 4095)[0], 200)  # exactly the limit is fine
        self.assertEqual(len(self.provider.calls), 1)
        self.assertEqual(self.ears.heard, 0)

    def test_rejects_bad_requests(self):
        cases = (("/ask?who=dave", b"\nhi"), ("/ask", b"\nhi"), ("/ask?who=zero", b"fact: 1\n\n   "),
                 ("/ask?who=zero", b"\xff\xfe"), ("/debate?turns=99", b"\ntea"), ("/debate?turns=1", b"\ntea"),
                 ("/debate?turns=many", b"\ntea"), ("/debate", b""))
        for path, body in cases:
            status, reply, _ = self.call("POST", path, body)
            self.assertEqual((status, reply), (400, b"error=bad-request\n"), path)
        status, reply, _ = self.call("POST", "/hear", make_wav(1.0, rate=22050))
        self.assertEqual((status, reply), (400, b"error=bad-audio\n"))
        self.assertEqual(self.provider.calls, [])

    def test_rejects_unknown_paths_and_tokens(self):
        token = self.brain.clips.put(b"RIFF")
        debate = self.server.hal_debates.start("tea", [], 2)
        paths = ("/", "/clip/", "/clip/../../etc/passwd", "/clip/" + token[:31], "/clip/" + token + "0",
                 "/clip/" + "0" * 32, "/clip/" + token.upper() + "/x", "/debate/" + "0" * 16 + "/0",
                 f"/debate/{debate}/x", f"/debate/{debate}/-1", f"/debate/{debate}", "/ask", "/hear", "/nothing")
        for path in paths:
            status, reply, _ = self.call("GET", path)
            self.assertEqual((status, reply), (404, b"error=not-found\n"), path)
        for path in ("/", "/clip/" + token, "/health", "/nothing"):
            self.assertEqual(self.call("POST", path, b"")[0], 404, path)
        self.assertEqual(self.call("PUT", "/ask?who=zero", b"\nhi")[0], 501)  # not a method this server has

    def test_503_when_the_voice_or_ears_fail(self):
        from brain.stt import SttError
        from brain.voice import VoiceError

        def no_voice(who, text):
            raise VoiceError("no ffmpeg")

        def no_ears(wav):
            raise SttError("no whisper")

        self.voice.render = no_voice
        self.assertEqual(self.call("POST", "/ask?who=zero", b"\nhi")[1], b"error=no-voice\n")
        self.ears.transcribe = no_ears
        self.assertEqual(self.call("POST", "/hear", make_wav(1.0)), (503, b"error=no-ears\n", "text/plain; charset=utf-8"))


class HardeningTest(ServerCase):
    def post(self, path: str, body: bytes, extra: str = "") -> bytes:
        return (f"POST {path} HTTP/1.1\r\nHost: 127.0.0.1\r\nX-HAL-Key: {KEY}\r\n{extra}"
                f"Content-Length: {len(body)}\r\n\r\n").encode() + body

    def test_error_after_body_closes_at_once(self):
        for path, body in (("/ask?who=dave", b"\nhi"), ("/hear", make_wav(1.0, rate=22050)),
                           ("/debate/" + "0" * 16 + "/stop", b"")):
            got, took = self.raw(self.post(path, body))
            self.assertIn(b"error=", got, path)
            self.assertLess(took, 0.5, path)  # not held open waiting for a body that was already read
        self.provider.fail_on = {1}
        got, took = self.raw(self.post("/ask?who=zero", b"\nhi"))
        self.assertIn(b"503", got.split(b"\r\n")[0])
        self.assertLess(took, 0.5)

    def test_content_length_odd_digits(self):
        head = f"POST /ask?who=zero HTTP/1.1\r\nHost: x\r\nX-HAL-Key: {KEY}\r\n"
        got, _ = self.raw(head.encode() + b"Content-Length: \xb2\r\n\r\n")  # a superscript two "is a digit"
        self.assertIn(b"error=no-length", got)
        got, _ = self.raw(head.encode() + b"Content-Length: " + b"9" * 5000 + b"\r\n\r\n")
        self.assertIn(b"error=too-large", got)
        got, _ = self.raw(head.encode() + b"Content-Length: -5\r\n\r\n")
        self.assertIn(b"error=no-length", got)
        self.assertEqual(self.provider.calls, [])

    def test_bad_url_gets_a_status(self):
        for line in (b"GET http://[::1 HTTP/1.1", b"POST http://[::1 HTTP/1.1"):
            got, _ = self.raw(line + b"\r\nHost: x\r\nContent-Length: 0\r\n\r\n")
            self.assertRegex(got.split(b"\r\n")[0], rb"^HTTP/1\.[01] 4\d\d", line)
            self.assertIn(b"error=", got)

    def test_unexpected_error_is_500_and_it_keeps_serving(self):
        def boom(system, messages, max_tokens=80):
            raise RuntimeError("something nobody planned for")

        self.provider.chat = boom
        status, body, _ = self.call("POST", "/ask?who=zero", b"\nhi")
        self.assertEqual((status, body), (500, b"error=internal\n"))
        self.assertEqual(self.call("GET", "/health")[0], 200)
        self.assertTrue(any("internal error: RuntimeError" in line for line in self.log))
        self.assertFalse(any("Traceback" in line for line in self.log))

    def test_trouble_on_this_computer_is_not_mistaken_for_a_caller_hanging_up(self):
        def full(system, messages, max_tokens=80):
            raise OSError(28, "No space left on device")

        self.provider.chat = full
        status, body, _ = self.call("POST", "/ask?who=zero", b"\nhi")
        self.assertEqual((status, body), (500, b"error=internal\n"))
        self.assertTrue(any("internal error: OSError" in line for line in self.log))

    def test_plain_errors_for_things_the_library_refuses(self):
        got, _ = self.raw(b"BREW /coffee HTTP/1.1\r\nHost: x\r\n\r\n")
        self.assertIn(b"error=", got)
        self.assertNotIn(b"<html", got.lower())

    def test_log_line_bounded_and_printable(self):
        self.raw(b"GET /" + b"a" * 5000 + b"\x1b[31m\x07 HTTP/1.1\r\nHost: x\r\n\r\n")
        self.assertTrue(self.log)
        for line in self.log:
            self.assertLessEqual(len(line), 260)
            self.assertNotIn("\x1b", line)
            self.assertNotIn("\x07", line)

    def test_log_never_holds_key_or_token(self):
        got = fields(self.call("POST", "/ask?who=zero", b"\nhi")[1])
        self.call("GET", "/clip/" + got["clip"], key=None)
        self.call("POST", "/ask?who=zero", b"\nhi", key="wrong-" + KEY)
        self.assertGreaterEqual(len(self.log), 3)
        for line in self.log:
            self.assertNotIn(KEY, line)
            self.assertNotIn(got["clip"], line)

    def test_a_503_says_why_in_the_log(self):
        self.provider.fail_on = {1}
        self.call("POST", "/ask?who=zero", b"\nhi")
        self.assertTrue(any("no-model" in line and "fake model is down" in line for line in self.log), self.log)

    def test_debate_failure_is_503(self):
        self.provider.fail_on = {1}
        debate = fields(self.call("POST", "/debate?turns=2", b"\ntea")[1])["debate"]
        status, body, _ = self.call("GET", f"/debate/{debate}/0")
        self.assertEqual((status, body), (503, b"error=no-model\n"))  # not 204: that means "finished"

    def test_heard_is_plain_and_bounded(self):
        self.ears.text = "caf\u00e9 \u266a \u201cquoted\u201d " + "x" * 500
        heard = fields(self.call("POST", "/hear", make_wav(1.0))[1])["heard"]
        self.assertLessEqual(len(heard), 300)
        self.assertTrue(all(" " <= ch <= "~" for ch in heard), heard[:40])
        self.assertTrue(heard.startswith("caf quoted x"))

    def test_documented_maximum_body_fits(self):
        body = ("\n".join("f" * 120 for _ in range(24)) + "\n\n" + "q" * 300).encode()
        self.assertLessEqual(len(body), 4096)
        self.assertEqual(self.call("POST", "/ask?who=zero", body)[0], 200)
        self.assertEqual(self.provider.calls[0][0].count("- " + "f" * 120), 24)

    def test_ask_during_an_argument(self):
        self.provider.delay_s = 0.05
        debate = fields(self.call("POST", "/debate?turns=4", b"\ntea")[1])["debate"]
        results = []
        asker = threading.Thread(target=lambda: results.append(self.call("POST", "/ask?who=desk", b"\nhi")[0]))
        asker.start()
        lines = [self.call("GET", f"/debate/{debate}/{n}")[0] for n in range(4)]
        asker.join(10)
        self.assertEqual((results, lines), ([200], [200, 200, 200, 200]))
        self.assertFalse(self.provider.overlapped)


class SlowSenderTest(ServerCase):
    read_deadline_s = 0.4
    max_handlers = 2

    def test_slow_sender_is_cut_off(self):
        head = f"POST /ask?who=zero HTTP/1.1\r\nHost: x\r\nX-HAL-Key: {KEY}\r\nContent-Length: 10\r\n\r\nabc"
        got, took = self.raw(head.encode(), hold_s=5)  # seven bytes of the body never come
        self.assertLess(took, 2.0)
        self.assertEqual(self.provider.calls, [])
        got, took = self.raw(b"GET /health HTT", hold_s=5)  # nor does the rest of this request line
        self.assertLess(took, 2.0)
        self.assertEqual(self.call("GET", "/health")[0], 200)

    def test_the_deadline_is_for_sending_not_for_thinking(self):
        self.provider.delay_s = 0.8  # twice the read deadline
        self.assertEqual(self.call("POST", "/ask?who=zero", b"\nhi")[0], 200)

    def test_handlers_are_capped(self):
        held = [socket.create_connection(("127.0.0.1", self.port), timeout=5) for _ in range(2)]
        try:
            for sock in held:
                sock.sendall(b"GET /health HTT")  # two requests that never finish
            time.sleep(0.1)
            got, took = self.raw(b"GET /health HTTP/1.1\r\nHost: x\r\n\r\n", hold_s=2)
            self.assertEqual(got, b"")  # the third is turned away at the door
            self.assertLess(took, 0.5)
        finally:
            for sock in held:
                sock.close()
        time.sleep(0.6)  # the two are cut off by the read deadline, and their places come free
        self.assertEqual(self.call("GET", "/health")[0], 200)


class WaitingTest(ServerCase):
    long_poll_s = 0.05

    def test_debate_wait_is_202(self):
        self.provider.gate = threading.Event()
        debate = fields(self.call("POST", "/debate?turns=2", b"\ntea")[1])["debate"]
        status, reply, _ = self.call("GET", f"/debate/{debate}/0")
        self.assertEqual((status, reply), (202, b"wait=1\n"))
        self.provider.gate.set()


class UnpairedTest(ServerCase):
    key = None

    def test_binds_local_without_a_key(self):
        self.assertEqual(self.server.server_address[0], "127.0.0.1")
        self.assertEqual(self.call("POST", "/ask?who=zero", b"\nhi", key=None)[0], 200)  # this computer only

    def test_unpaired_refuses_browser_style_requests(self):
        # A web page open in a browser on this computer can post to 127.0.0.1; it can't hide where it came from.
        for headers in ({"Origin": "https://attacker.example"}, {"Host": "attacker.example"},
                        {"Host": "attacker.example:9100"}, {"Origin": "null"}):
            status, body, _ = self.call("POST", "/ask?who=zero", b"\nhi", key=None, headers=headers)
            self.assertEqual((status, body), (403, b"error=local-only\n"), headers)
        for host in ("127.0.0.1:9100", "localhost", "localhost:9100", "[::1]:9100"):
            self.assertEqual(self.call("POST", "/ask?who=zero", b"\nhi", key=None, headers={"Host": host})[0], 200, host)
        self.assertEqual(len(self.provider.calls), 4)


class PairedBindTest(unittest.TestCase):
    def test_binds_everywhere_with_a_key(self):
        brain = Brain(FakeProvider(), FakeVoice(), FakeEars(), ClipStore(), ChatMemory())
        server = make_server(brain, Debates(brain), KEY, port=0)
        try:
            self.assertEqual(server.server_address[0], "0.0.0.0")
        finally:
            server.server_close()


if __name__ == "__main__":
    unittest.main()
