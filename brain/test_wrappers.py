import io
import pathlib
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
