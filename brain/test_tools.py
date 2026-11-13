import os
import pathlib
import stat
import subprocess
import tempfile
import unittest

from brain import pair
from brain.config import read_env, valid_key

KEY = "0123456789abcdef0123456789abcdef"


class KeyTest(unittest.TestCase):
    def test_valid_key(self):
        self.assertTrue(valid_key(KEY))
        for bad in ("", "1", "my-secret", KEY[:-1], KEY + "0", KEY.upper(), None):
            self.assertFalse(valid_key(bad), repr(bad))

    def test_config_lines(self):
        self.assertEqual(pair.config_lines("my-computer.local", 9100, KEY),
                         "brain=on\nbrain_host=my-computer.local\nbrain_port=9100\nbrain_key=" + KEY + "\n")

    def test_read_key(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, ".env")
            self.assertIsNone(pair.read_key(path))  # no file yet
            pathlib.Path(path).write_text("HAL_BRAIN_PORT=9200\nHAL_BRAIN_KEY=\n")
            self.assertIsNone(pair.read_key(path))  # an empty setting is no key
            pathlib.Path(path).write_text("HAL_BRAIN_KEY=" + KEY + "\n")
            self.assertEqual(pair.read_key(path), KEY)
            pathlib.Path(path).write_text("HAL_BRAIN_KEY=my-secret\n")
            with self.assertRaises(ValueError) as caught:  # HAL Zero would ignore it: say so, don't "pair"
                pair.read_key(path)
            self.assertNotIn("my-secret", str(caught.exception))

    def test_save_key_keeps_the_rest_and_is_private(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, ".env")
            pathlib.Path(path).write_text("# my settings\nHAL_BRAIN_MODEL=tiny\nHAL_BRAIN_KEY=\n")
            os.chmod(path, 0o644)
            pair.save_key(path, KEY)
            self.assertEqual(stat.S_IMODE(os.stat(path).st_mode), 0o600)
            self.assertEqual(read_env(path), {"HAL_BRAIN_MODEL": "tiny", "HAL_BRAIN_KEY": KEY})
            self.assertIn("# my settings", pathlib.Path(path).read_text())
            self.assertEqual(os.listdir(d), [".env"])  # nothing left behind

    def test_save_key_with_no_file_yet(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "sub", ".env")
            pair.save_key(path, KEY)
            self.assertEqual(read_env(path), {"HAL_BRAIN_KEY": KEY})
            self.assertEqual(stat.S_IMODE(os.stat(path).st_mode), 0o600)

    def test_target_must_be_user_at_host(self):
        for good in ("pi@cardputerzero.local", "me@192.0.2.7", "a.b-c_d@host-1.example"):
            self.assertEqual(pair.check_target(good), good)
        for bad in ("-oProxyCommand=x@y", "nohost", "pi@", "@host", "pi@host;rm", "pi@host name", ""):
            with self.assertRaises(ValueError, msg=bad):
                pair.check_target(bad)

    def test_this_host_is_a_plain_name(self):
        self.assertRegex(pair.this_host(), r"^[A-Za-z0-9.-]+$")


class RemoteSnippetTest(unittest.TestCase):
    """The shell snippet that runs on the Cardputer, run here against a throwaway home folder."""

    def setUp(self):
        self.home = tempfile.TemporaryDirectory()
        self.addCleanup(self.home.cleanup)
        self.config = pathlib.Path(self.home.name) / "hal" / "config.txt"
        self.config.parent.mkdir()

    def remote(self, lines: str) -> int:
        env = {"HOME": self.home.name, "PATH": os.environ.get("PATH", "/usr/bin:/bin")}
        return subprocess.run(["sh", "-c", pair.REMOTE], input=lines.encode(), env=env, capture_output=True).returncode

    def test_settings_survive_and_old_brain_lines_go(self):
        self.config.write_text("# HAL Zero settings\nlink=on\nvolume=180\nbrain=off\n  brain_key = oldoldold\n"
                               "brain_host=old.local\npeer_host=hal9000-old.local\n")
        self.assertEqual(self.remote(pair.config_lines("mac.local", 9100, KEY)), 0)
        text = self.config.read_text()
        self.assertEqual(text, "# HAL Zero settings\nlink=on\nvolume=180\npeer_host=hal9000-old.local\n"
                               "brain=on\nbrain_host=mac.local\nbrain_port=9100\nbrain_key=" + KEY + "\n")
        self.assertEqual(stat.S_IMODE(self.config.stat().st_mode), 0o600)
        self.assertEqual(os.listdir(self.config.parent), ["config.txt"])

    def test_pairing_twice_changes_nothing_more(self):
        self.config.write_text("link=on\n")
        lines = pair.config_lines("mac.local", 9100, KEY)
        self.remote(lines)
        once = self.config.read_text()
        self.assertEqual(self.remote(lines), 0)
        self.assertEqual(self.config.read_text(), once)

    def test_a_settings_file_of_only_brain_lines(self):
        self.config.write_text("brain=off\n")  # grep -v then selects nothing: not an error
        self.assertEqual(self.remote(pair.config_lines("mac.local", 9100, KEY)), 0)
        self.assertEqual(self.config.read_text(), pair.config_lines("mac.local", 9100, KEY))

    def test_missing_settings_file_is_refused(self):
        self.assertNotEqual(self.remote(pair.config_lines("mac.local", 9100, KEY)), 0)
        self.assertFalse(self.config.exists())  # deploying HAL Zero puts the template there, not pairing

    @unittest.skipIf(os.geteuid() == 0, "root can read anything")
    def test_unreadable_settings_file_is_left_alone(self):
        self.config.write_text("link=on\nvolume=180\n")
        self.config.chmod(0)
        try:
            self.assertNotEqual(self.remote(pair.config_lines("mac.local", 9100, KEY)), 0)
        finally:
            self.config.chmod(0o600)
        self.assertEqual(self.config.read_text(), "link=on\nvolume=180\n")
        self.assertEqual(os.listdir(self.config.parent), ["config.txt"])

    def test_short_input_is_refused(self):
        self.config.write_text("link=on\n")
        self.assertNotEqual(self.remote("brain=on\n"), 0)  # the connection dropped mid-way
        self.assertEqual(self.config.read_text(), "link=on\n")
        self.assertEqual(os.listdir(self.config.parent), ["config.txt"])

    def test_a_linked_settings_file_stays_a_link(self):
        real = pathlib.Path(self.home.name) / "real-config.txt"
        real.write_text("link=on\n")
        self.config.symlink_to(real)
        self.assertEqual(self.remote(pair.config_lines("mac.local", 9100, KEY)), 0)
        self.assertTrue(self.config.is_symlink())
        self.assertIn("brain_key=" + KEY, real.read_text())


class PairRunTest(unittest.TestCase):
    """pair.main with a stand-in for ssh."""

    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)
        self.env = os.path.join(self.dir.name, ".env")
        self.ssh = pair.SSH
        self.addCleanup(lambda: setattr(pair, "SSH", self.ssh))

    def test_a_failed_pairing_leaves_no_key_behind(self):
        pair.SSH = ["sh", "-c", "cat > /dev/null; exit 1", "ssh"]
        self.assertEqual(pair.main(["pair", "pi@nowhere.invalid"], env_path=self.env, quiet=True), 1)
        self.assertFalse(os.path.exists(self.env))  # or the next start would open the brain to the network

    def test_a_successful_pairing_saves_the_key_it_sent(self):
        got = os.path.join(self.dir.name, "sent")
        pair.SSH = ["sh", "-c", f"cat > {got}", "ssh"]
        self.assertEqual(pair.main(["pair", "pi@cardputerzero.local"], env_path=self.env, quiet=True), 0)
        key = read_env(self.env)["HAL_BRAIN_KEY"]
        self.assertTrue(valid_key(key))
        self.assertIn("brain_key=" + key, pathlib.Path(got).read_text())

    def test_a_malformed_key_stops_pairing(self):
        pathlib.Path(self.env).write_text("HAL_BRAIN_KEY=my-secret\n")
        pair.SSH = ["sh", "-c", "exit 0", "ssh"]
        self.assertEqual(pair.main(["pair", "pi@cardputerzero.local"], env_path=self.env, quiet=True), 2)


if __name__ == "__main__":
    unittest.main()
