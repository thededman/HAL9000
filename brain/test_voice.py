"""Each voiced HAL in his own voice: which model, which treatment, and one loaded model per file."""
import dataclasses
import unittest

from brain import cast, voice


@dataclasses.dataclass(frozen=True)
class Tone:
    name: str
    model: str


class FakeTool:
    """Stands in for tools/make_voice.py: "loads" a model by name and writes what it was asked to say."""
    VOICE = "hal"
    VOICES = {"desk": Tone("desk", "hal"), "zero": Tone("A", "hal"), "card": Tone("card", "alan")}

    def __init__(self, missing=()):
        self.loads: list[str] = []
        self.missing = set(missing)

    def load_voice(self, model="hal"):
        self.loads.append(model)
        if model in self.missing:
            raise SystemExit(f"no {model} here")
        return f"<{model}>"

    def say(self, loaded, text, dst, tone, tmp):
        dst.write_bytes(f"{loaded}|{tone.name}|{text}".encode())


class VoiceTest(unittest.TestCase):
    def test_each_hal_speaks_in_his_own_voice(self):
        v = voice.Voice(FakeTool())
        self.assertEqual(v.render("desk", "Good evening."), b"<hal>|desk|Good evening.")
        self.assertEqual(v.render("zero", "Hello."), b"<hal>|A|Hello.")
        self.assertEqual(v.render("card", "Still here."), b"<alan>|card|Still here.")

    def test_one_loaded_model_per_file(self):
        tool = FakeTool()
        v = voice.Voice(tool)
        self.assertEqual(tool.loads, ["hal"])  # the everyday voice is ready before the first question
        for who in ("zero", "desk", "zero"):
            v.render(who, "x")
        self.assertEqual(tool.loads, ["hal"])
        v.render("card", "x")                  # HAL Card's is loaded the first time he speaks
        v.render("card", "y")
        v.render("desk", "z")
        self.assertEqual(tool.loads, ["hal", "alan"])

    def test_those_without_a_voice_cannot_be_rendered(self):
        v = voice.Voice(FakeTool())
        for who in ("nano", "paper", "dave", "", None):
            with self.assertRaises(ValueError, msg=repr(who)):
                v.render(who, "x")

    def test_a_missing_voice_is_a_voice_error(self):
        with self.assertRaises(voice.VoiceError) as failed:
            voice.Voice(FakeTool(missing={"hal"}))
        self.assertIn("no hal here", str(failed.exception))
        v = voice.Voice(FakeTool(missing={"alan"}))  # the brain still starts without HAL Card's voice
        self.assertEqual(v.render("zero", "x"), b"<hal>|A|x")
        with self.assertRaises(voice.VoiceError):
            v.render("card", "x")

    def test_a_failed_render_is_a_voice_error(self):
        tool = FakeTool()

        def broken(loaded, text, dst, tone, tmp):
            raise RuntimeError("ffmpeg fell over")

        tool.say = broken
        with self.assertRaises(voice.VoiceError):
            voice.Voice(tool).render("zero", "x")


class RealToolTest(unittest.TestCase):
    """The table against tools/make_voice.py itself (only its settings: nothing is loaded or rendered)."""

    def setUp(self):
        self.tool = voice._make_voice()

    def test_every_voiced_member_has_a_voice_in_the_tool(self):
        voiced = {m.id: m.voice for m in cast.MEMBERS if m.kind == "voice"}
        self.assertEqual(set(voiced.values()), set(self.tool.VOICES))
        for who, key in voiced.items():
            self.assertTrue(self.tool.VOICES[key].model, who)

    def test_the_everyday_two_sound_as_they_did(self):
        self.assertIs(self.tool.VOICES["desk"], self.tool.DESK)
        self.assertIs(self.tool.VOICES["zero"], self.tool.TREATMENTS[self.tool.NEW_TREATMENT])
        for key in ("desk", "zero"):
            tone = self.tool.VOICES[key]
            self.assertEqual((tone.model, tone.rate, tone.codec), ("hal", 22050, "pcm_s16le"))

    def test_hal_card_has_the_v01_voice_in_a_clip_he_can_hold(self):
        tone = self.tool.VOICES["card"]
        self.assertEqual((tone.model, tone.codec, tone.rate), ("en_GB-alan-medium", "pcm_u8", self.tool.CARD_RATE))
        # His longest line has to have a chance of fitting: 8-bit mono is one byte a sample.
        seconds = cast.member("card").clip_bytes / tone.rate
        self.assertGreater(seconds, 8)


if __name__ == "__main__":
    unittest.main()
