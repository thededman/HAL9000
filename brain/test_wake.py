""""Hey HAL": a recording checked for his name is transcribed with a hint about how it is spelt."""
import pathlib
import stat
import tempfile
import unittest

from . import stt
from .core import Brain
from .fakes import FakeEars, make_wav
from .test_server import ServerCase


class WakeHintTest(unittest.TestCase):
    def test_the_hint_names_him_but_is_not_a_call(self):
        # Whisper sometimes writes its hint out when it hears nothing clear. The hint must not
        # itself read as "Hey HAL", or silence would call him.
        self.assertIn("HAL", stt.WAKE_HINT)
        for opener in ("hey", "hi ", "ok", "hay"):
            self.assertNotIn(opener, stt.WAKE_HINT.lower())

    def test_brain_passes_the_hint_only_for_a_wake_check(self):
        ears = FakeEars("Hey HAL, what time is it?")
        brain = Brain(provider=None, voice=None, ears=ears, clips=None, memory=None)
        self.assertEqual(brain.hear(make_wav(1.0)), "Hey HAL, what time is it?")
        self.assertEqual(ears.hints, [""])
        self.assertEqual(brain.hear(make_wav(1.0), wake=True), "Hey HAL, what time is it?")
        self.assertEqual(ears.hints, ["", stt.WAKE_HINT])

    def test_whisper_is_given_the_hint(self):
        # A stand-in whisper-cli that writes its arguments out as the "transcript".
        with tempfile.TemporaryDirectory() as folder:
            fake = pathlib.Path(folder) / "fake-whisper"
            fake.write_text('#!/bin/sh\necho "$@"\n')
            fake.chmod(fake.stat().st_mode | stat.S_IXUSR)
            ears = stt.Whisper(model=pathlib.Path(__file__), binary=str(fake))
            plain = ears.transcribe(make_wav(1.0))
            hinted = ears.transcribe(make_wav(1.0), hint=stt.WAKE_HINT)
        self.assertNotIn("--prompt", plain)
        self.assertIn(f"--prompt {stt.WAKE_HINT}", hinted)


class WakeServerTest(ServerCase):
    def test_a_wake_check_is_heard_with_the_hint(self):
        status, body, _ = self.call("POST", "/hear?wake=1", make_wav(1.0))
        self.assertEqual((status, body), (200, b"heard=hello there\n"))
        self.assertEqual(self.ears.hints, [stt.WAKE_HINT])
        self.call("POST", "/hear", make_wav(1.0))
        self.assertEqual(self.ears.hints, [stt.WAKE_HINT, ""])

    def test_what_was_said_is_not_written_in_the_log(self):
        self.ears.text = "a private remark, not for HAL"
        self.call("POST", "/hear?wake=1", make_wav(1.0))
        self.assertTrue(self.log)
        self.assertFalse(any("private remark" in line for line in self.log))


if __name__ == "__main__":
    unittest.main()
