import io
import os
import pathlib
import socket
import threading
import time
import unittest
import wave

from brain import providers, stt, voice
from brain.providers import ollama


def make_wav(seconds=1.0, rate=16000, channels=1, width=2) -> bytes:
    out = io.BytesIO()
    with wave.open(out, "wb") as w:
        w.setnchannels(channels)
        w.setsampwidth(width)
        w.setframerate(rate)
        w.writeframes(b"\x00" * int(seconds * rate) * channels * width)
    return out.getvalue()


class OllamaTest(unittest.TestCase):
    def test_ollama_request(self):
        history = [{"role": "user", "content": "q1"}, {"role": "assistant", "content": "a1"},
                   {"role": "user", "content": "q2"}]
        body = ollama.build_request("some-model", "You are HAL.", history, 80)
        self.assertEqual(body["model"], "some-model")
        self.assertIs(body["stream"], False)
        self.assertIs(body["think"], False)
        self.assertEqual(body["keep_alive"], "30m")
        self.assertEqual(body["options"]["num_predict"], 80)
        self.assertEqual(body["messages"][0], {"role": "system", "content": "You are HAL."})
        self.assertEqual(body["messages"][1:], history)

    def test_ollama_response(self):
        self.assertEqual(ollama.parse_response({"message": {"role": "assistant", "content": " hi \n"}}), "hi")
        for bad in ({}, {"message": {}}, {"message": {"content": 5}}, {"error": "model not found"}, [], None):
            with self.assertRaises(providers.ProviderError, msg=repr(bad)):
                ollama.parse_response(bad)

    def test_ollama_unreachable_is_provider_error(self):
        dead = ollama.OllamaProvider(host="http://127.0.0.1:9", timeout_s=2)  # the discard port: nothing answers
        with self.assertRaises(providers.ProviderError):
            dead.chat("s", [{"role": "user", "content": "q"}])
        dead.warm()  # never raises

    def test_ollama_cut_off_mid_answer_is_provider_error(self):
        # A stand-in that promises 500 bytes and hangs up after 20, and one that isn't HTTP at all.
        for reply in (b"HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 500\r\n\r\n" + b'{"message": {"conte',
                      b"SSH-2.0-OpenSSH_9.9\r\n"):
            listener = socket.create_server(("127.0.0.1", 0))
            port = listener.getsockname()[1]

            def serve(listener=listener, reply=reply):
                conn, _ = listener.accept()
                conn.recv(65536)
                conn.sendall(reply)
                conn.close()
                listener.close()

            threading.Thread(target=serve, daemon=True).start()
            provider = ollama.OllamaProvider(host=f"http://127.0.0.1:{port}", timeout_s=3)
            with self.assertRaises(providers.ProviderError):
                provider.chat("s", [{"role": "user", "content": "q"}])

    def test_warm_never_raises_whatever_happens(self):
        provider = ollama.OllamaProvider(host="http://127.0.0.1:9", timeout_s=1)

        def boom(*a, **k):
            raise RuntimeError("anything")

        provider.chat = boom
        self.assertIs(provider.warm(), False)

    def test_local_calls_ignore_proxies(self):
        # With http_proxy set, urllib would hand a request for 127.0.0.1 (prompt, facts and all) to the proxy.
        seen = []
        listener = socket.create_server(("127.0.0.1", 0))
        proxy_port = listener.getsockname()[1]

        def proxy():
            listener.settimeout(1.5)
            try:
                conn, _ = listener.accept()
                seen.append(conn.recv(65536))
                conn.close()
            except OSError:
                pass
            listener.close()

        threading.Thread(target=proxy, daemon=True).start()
        old = {k: os.environ.get(k) for k in ("http_proxy", "HTTP_PROXY", "no_proxy", "NO_PROXY")}
        os.environ["http_proxy"] = os.environ["HTTP_PROXY"] = f"http://127.0.0.1:{proxy_port}"
        os.environ.pop("no_proxy", None)
        os.environ.pop("NO_PROXY", None)
        try:
            with self.assertRaises(providers.ProviderError):  # nothing listens on the discard port
                ollama.OllamaProvider(host="http://127.0.0.1:9", timeout_s=1).chat("secret prompt", [])
        finally:
            for k, v in old.items():
                os.environ.pop(k, None) if v is None else os.environ.__setitem__(k, v)
        time.sleep(0.1)
        self.assertEqual(seen, [])

    def test_make_provider(self):
        p = providers.make_provider({})
        self.assertEqual((p.name, p.model), ("ollama", "qwen3.6:35b-a3b"))
        self.assertEqual(providers.make_provider({"HAL_BRAIN_MODEL": "tiny"}).model, "tiny")
        self.assertEqual(providers.make_provider({"HAL_BRAIN_PROVIDER": "Ollama", "HAL_BRAIN_MODEL": ""}).model,
                         "qwen3.6:35b-a3b")
        with self.assertRaises(ValueError):
            providers.make_provider({"HAL_BRAIN_PROVIDER": "nope"})


class VoiceTest(unittest.TestCase):
    def test_wav_ms(self):
        self.assertEqual(voice.wav_ms(make_wav(1.0, rate=22050)), 1000)
        self.assertEqual(voice.wav_ms(make_wav(2.5, rate=22050)), 2500)
        self.assertEqual(voice.wav_ms(make_wav(1.0, rate=16000)), 1000)
        for junk in (b"", b"not a wav at all", make_wav(1.0)[:20]):
            self.assertEqual(voice.wav_ms(junk), 0)


class EarsTest(unittest.TestCase):
    def test_check_wav_accepts_16k_mono(self):
        stt.check_wav(make_wav(1.0))

    def test_check_wav_rejects(self):
        cases = {"22 kHz": make_wav(1.0, rate=22050), "stereo": make_wav(1.0, channels=2),
                 "8-bit": make_wav(1.0, width=1), "too short": make_wav(0.1), "truncated": make_wav(1.0)[:2000],
                 "not a wav": b"hello" * 100, "empty": b""}
        for name, clip in cases.items():
            with self.assertRaises(ValueError, msg=name):
                stt.check_wav(clip)

    def test_broken_headers_are_just_bad_audio(self):
        good = make_wav(1.0)
        huge_fmt = good[:16] + b"\xff\xff\xff\xff" + good[20:]  # a chunk that claims to be 4 GB
        for clip in (huge_fmt, good[:12] + b"fmt " + b"\x00" * 4 + good[20:], b"RIFF\xff\xff\xff\xffWAVE" + b"\x00" * 50):
            with self.assertRaises(ValueError):
                stt.check_wav(clip)
            self.assertEqual(voice.wav_ms(clip), 0)

    def test_tidy(self):
        self.assertEqual(stt.tidy(" [BLANK_AUDIO] \n"), "")
        self.assertEqual(stt.tidy("(wind) Hello  there [music]\n"), "Hello there")
        self.assertEqual(stt.tidy(" What time\n is it?\n"), "What time is it?")

    def test_missing_model_is_an_stt_error(self):
        ears = stt.Whisper(model=pathlib.Path("/nonexistent/model.bin"))
        with self.assertRaises(stt.SttError):
            ears.transcribe(make_wav(1.0))

    def test_warm_never_raises(self):
        stt.Whisper(model=pathlib.Path("/nonexistent/model.bin")).warm()

    def test_missing_program_is_an_stt_error(self):
        ears = stt.Whisper(model=pathlib.Path(__file__), binary="hal-no-such-whisper")
        with self.assertRaises(stt.SttError):
            ears.transcribe(make_wav(1.0))


if __name__ == "__main__":
    unittest.main()
