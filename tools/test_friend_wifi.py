"""tools/friend_wifi.py: what is sent to a friend, and that the password stays out of sight."""
import importlib.util
import pathlib
import unittest

_spec = importlib.util.spec_from_file_location("friend_wifi", pathlib.Path(__file__).with_name("friend_wifi.py"))
fw = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(fw)

SECRET = "open-the-door-2001"


class FakePort:
    """A friend at the other end of the cable: answers as the firmware does."""

    def __init__(self, who="nano", takes=True, joins_after=2, silent=False):
        self.who, self.takes, self.joins_after, self.silent = who, takes, joins_after, silent
        self.written, self.replies, self.wifi, self.asked_since_setup = [], [], False, 0

    def write_line(self, line):
        self.written.append(line)
        if self.silent:
            return
        if line == "HALCFG?":
            self.asked_since_setup += 1
            joined = self.wifi and self.asked_since_setup > self.joins_after
            self.replies.append("boot noise that is not for us")
            self.replies.append(f"HALCFG {self.who} 0.7.0 wifi={'set' if self.wifi else 'none'} "
                                f"ip={'192.168.1.40' if joined else '-'}")
        elif line.startswith("HALCFG "):
            self.wifi = self.takes
            self.asked_since_setup = 0
            self.replies.append("HALCFG OK" if self.takes else "HALCFG BAD")

    def read_line(self, timeout):
        return self.replies.pop(0) if self.replies else None


class Clock:
    """Time that moves a little every time it is looked at, so nothing really waits."""

    def __init__(self):
        self.now = 0.0

    def __call__(self):
        self.now += 0.3
        return self.now


def run(port, ssid="Discovery One", password=SECRET):
    said = []
    code = fw.configure(port, read_name=lambda prompt: ssid, read_password=lambda prompt: password,
                        say=said.append, clock=Clock())
    return code, said


class SettingsTest(unittest.TestCase):
    def test_settings_text(self):
        self.assertEqual(fw.settings_text("Home", "12345678"), "wifi_ssid=Home\nwifi_password=12345678\n")
        self.assertEqual(fw.settings_text("Cafe", ""), "wifi_ssid=Cafe\n")  # an open network
        self.assertEqual(fw.settings_text("Net #5 = home", "a#b=c d!"), "wifi_ssid=Net #5 = home\nwifi_password=a#b=c d!\n")

    def test_what_a_friend_cant_take_is_refused_in_plain_words(self):
        for ssid, password in [("", "12345678"), ("n" * 33, "12345678"), (" Home", "12345678"), ("Home", "short"),
                               ("Home", "x" * 64), ("Home", "12345678 "), ("Café", "12345678"),
                               ("Home", "pass\nword"), ("Ho\tme", "12345678")]:
            with self.assertRaises(ValueError, msg=repr((ssid, password))) as caught:
                fw.settings_text(ssid, password)
            self.assertNotIn(password or "\0", str(caught.exception))  # the message never quotes the password

    def test_setup_line_is_hex(self):
        self.assertEqual(fw.setup_line("wifi_ssid=A\n"), "HALCFG " + "wifi_ssid=A\n".encode().hex())
        self.assertNotIn(SECRET, fw.setup_line(fw.settings_text("Home", SECRET)))

    def test_parse_answer(self):
        self.assertEqual(fw.parse_answer("HALCFG nano 0.7.0 wifi=none ip=-"),
                         {"id": "nano", "version": "0.7.0", "wifi": False, "ip": ""})
        self.assertEqual(fw.parse_answer("HALCFG card 0.7.0 wifi=set ip=192.168.1.40\r")["ip"], "192.168.1.40")
        for line in ["", "HALCFG OK", "HALCFG desk 0.7.0 wifi=set ip=-", "HALCFG nano 0.7.0 wifi=maybe ip=-",
                     "hello", "HALCFG nano 0.7.0 wifi=set"]:
            self.assertIsNone(fw.parse_answer(line), line)

    def test_candidate_ports(self):
        self.assertEqual(fw.candidate_ports(["/dev/cu.usbserial-B", "/dev/cu.usbserial-A", "/dev/cu.usbserial-A"]),
                         ["/dev/cu.usbserial-A", "/dev/cu.usbserial-B"])
        self.assertEqual(fw.candidate_ports([]), [])


class ConversationTest(unittest.TestCase):
    def test_a_friend_is_set_up_and_joins(self):
        port = FakePort("nano")
        code, said = run(port)
        self.assertEqual(code, 0)
        self.assertEqual(port.written[0], "HALCFG?")
        setups = [line for line in port.written if line.startswith("HALCFG ")]
        self.assertEqual(setups, [fw.setup_line(fw.settings_text("Discovery One", SECRET))])
        self.assertIn("Found HAL Nano", said[0])
        self.assertIn("hal-nano.local", said[-1])

    def test_the_password_is_never_said(self):
        for port in (FakePort("paper"), FakePort("card", takes=False), FakePort("nano", joins_after=10 ** 6)):
            code, said = run(port)
            self.assertNotIn(SECRET, "\n".join(said))
            self.assertNotIn(SECRET.encode().hex(), "\n".join(said))

    def test_nobody_there(self):
        code, said = run(FakePort(silent=True))
        self.assertEqual(code, 1)
        self.assertIn("Nothing on that port answers", said[0])

    def test_he_refuses_or_never_joins(self):
        code, said = run(FakePort("card", takes=False))
        self.assertEqual(code, 1)
        self.assertIn("didn't take", said[-1])
        code, said = run(FakePort("nano", joins_after=10 ** 6))
        self.assertEqual(code, 1)
        self.assertIn("hasn't joined yet", said[-1])

    def test_a_bad_name_is_caught_before_anything_is_sent(self):
        port = FakePort("nano")
        code, said = run(port, ssid="", password=SECRET)
        self.assertEqual(code, 1)
        self.assertEqual([line for line in port.written if line.startswith("HALCFG ")], [])


if __name__ == "__main__":
    unittest.main()
