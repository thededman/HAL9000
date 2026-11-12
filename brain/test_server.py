import http.client
import threading
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

    def setUp(self):
        self.provider, self.voice, self.ears = FakeProvider(), FakeVoice(), FakeEars()
        self.brain = Brain(self.provider, self.voice, self.ears, ClipStore(), ChatMemory())
        self.server = make_server(self.brain, Debates(self.brain), self.key, port=0, long_poll_s=self.long_poll_s,
                                  log=False)
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
            conn.putrequest(method, path)
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
