"""Tests for the pure helpers in make_voice.py.  Run: python3.11 -m unittest tools/test_make_voice.py"""
import pathlib
import sys
import unittest

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
        self.assertEqual(mv.mine_name(2, "My Clip (final).mp3"), "002-my-clip-final.wav")


if __name__ == "__main__":
    unittest.main()
