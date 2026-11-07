"""Tests for the pure helpers in make_voice.py.  Run: python3.11 -m unittest tools/test_make_voice.py"""
import pathlib
import sys
import tempfile
import unittest
import wave

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import make_voice as mv  # noqa: E402


class HelperTests(unittest.TestCase):
    def test_read_lines_skips_comments_and_blanks(self):
        self.assertEqual(mv.read_lines("# x\n\nHello Dave.\n  \n# y\nBye.\n"), ["Hello Dave.", "Bye."])

    def test_slug(self):
        self.assertEqual(mv.slug("I've taken the liberty of reorganising"), "ive-taken-the-liberty-of")
        self.assertEqual(mv.slug("...!!!"), "line")

    def test_out_name(self):
        self.assertEqual(mv.out_name(7, "Good evening, Dave."), "007-good-evening-dave.wav")

    def test_folder_for(self):
        self.assertEqual(mv.folder_for("greet_evening.txt"), "greet/evening")
        self.assertEqual(mv.folder_for("tap.txt"), "tap")

    def test_daisy_filters(self):
        f3 = mv.daisy_filter(3)
        self.assertIn("asetrate=22050*0.75", f3)
        self.assertNotIn("atempo", f3)
        self.assertIn("asetrate=22050*0.96", mv.daisy_filter(0))

    def test_mine_names(self):
        self.assertEqual(mv.mine_name("My Clip (final).mp3"), "my-clip-final.wav")


class CardTests(unittest.TestCase):
    """Uses temp folders in place of the repo's sd/ tree and a card."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        root = pathlib.Path(self.tmp.name)
        self.saved = (mv.SD_DIR, mv.TEMPLATE_CONFIG)
        mv.SD_DIR = root / "sd" / "hal"
        (mv.SD_DIR / "voice" / "tap").mkdir(parents=True)
        (mv.SD_DIR / "config.txt").write_text("proximity_threshold=300\n")  # stale local copy
        mv.TEMPLATE_CONFIG = root / "template.txt"
        mv.TEMPLATE_CONFIG.write_text("proximity_threshold=120\n")
        self.card = root / "card"
        self.card.mkdir()
        self.clips = root / "clips"
        self.clips.mkdir()

    def tearDown(self):
        mv.SD_DIR, mv.TEMPLATE_CONFIG = self.saved
        self.tmp.cleanup()

    def test_new_card_gets_current_template(self):
        mv.copy_to_card(self.card)
        self.assertIn("120", (self.card / "hal" / "config.txt").read_text())

    def test_existing_card_config_is_kept(self):
        (self.card / "hal").mkdir()
        (self.card / "hal" / "config.txt").write_text("wifi_ssid=Home\n")
        mv.copy_to_card(self.card)
        self.assertEqual((self.card / "hal" / "config.txt").read_text(), "wifi_ssid=Home\n")

    def test_mine_rerun_does_not_duplicate(self):
        with wave.open(str(self.clips / "My Clip.wav"), "wb") as w:
            w.setnchannels(1); w.setsampwidth(2); w.setframerate(22050); w.writeframes(b"\0\0" * 2205)
        mv.convert_mine(self.clips)
        mv.convert_mine(self.clips)
        self.assertEqual(sorted(p.name for p in (mv.SD_DIR / "voice" / "mine").glob("*.wav")), ["my-clip.wav"])


if __name__ == "__main__":
    unittest.main()
