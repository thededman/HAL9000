import os
import pathlib
import stat
import tempfile
import unittest

from brain import pair
from brain.config import read_env


class PairTest(unittest.TestCase):
    def test_config_lines(self):
        lines = pair.config_lines("my-computer.local", 9100, "0123456789abcdef0123456789abcdef")
        self.assertEqual(lines, "brain=on\nbrain_host=my-computer.local\nbrain_port=9100\n"
                                "brain_key=0123456789abcdef0123456789abcdef\n")

    def test_ensure_key_keeps_existing(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, ".env")
            with open(path, "w") as f:
                f.write("HAL_BRAIN_PORT=9200\nHAL_BRAIN_KEY=already-there\n")
            self.assertEqual(pair.ensure_key(path), "already-there")
            self.assertEqual(pathlib.Path(path).read_text(), "HAL_BRAIN_PORT=9200\nHAL_BRAIN_KEY=already-there\n")

    def test_ensure_key_creates(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, ".env")
            with open(path, "w") as f:
                f.write("# my settings\nHAL_BRAIN_MODEL=tiny\nHAL_BRAIN_KEY=\n")
            key = pair.ensure_key(path)
            self.assertRegex(key, r"^[0-9a-f]{32}$")
            self.assertEqual(stat.S_IMODE(os.stat(path).st_mode), 0o600)
            self.assertEqual(read_env(path), {"HAL_BRAIN_MODEL": "tiny", "HAL_BRAIN_KEY": key})
            self.assertIn("# my settings", pathlib.Path(path).read_text())  # the rest of the file is kept
            self.assertEqual(pair.ensure_key(path), key)       # and asking again changes nothing

    def test_ensure_key_with_no_file_yet(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "sub", ".env")
            key = pair.ensure_key(path)
            self.assertEqual(read_env(path), {"HAL_BRAIN_KEY": key})
            self.assertEqual(stat.S_IMODE(os.stat(path).st_mode), 0o600)

    def test_target_must_be_user_at_host(self):
        for good in ("pi@cardputerzero.local", "me@192.0.2.7", "a.b-c_d@host-1.example"):
            self.assertEqual(pair.check_target(good), good)
        for bad in ("-oProxyCommand=x@y", "nohost", "pi@", "@host", "pi@host;rm", "pi@host name", ""):
            with self.assertRaises(ValueError, msg=bad):
                pair.check_target(bad)

    def test_this_host_is_a_plain_name(self):
        self.assertRegex(pair.this_host(), r"^[A-Za-z0-9.-]+$")


if __name__ == "__main__":
    unittest.main()
