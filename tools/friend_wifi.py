#!/usr/bin/env python3
"""Give a friend HAL (HAL Nano, HAL Paper or HAL Card) your Wi-Fi network, over USB.

    tools/friend_wifi.py                              # finds the friend plugged in
    tools/friend_wifi.py --port /dev/cu.usbserial-XXXX

A friend has no SD card for a config.txt: the network's name and password are kept in his own
flash. You type both here. The password is not shown as you type, is not saved anywhere on this
computer, and the friend never repeats it. Needs nothing beyond Python (macOS or Linux).
"""
import argparse
import getpass
import glob
import os
import select
import sys
import termios
import time

NAMES = {"nano": "HAL Nano", "paper": "HAL Paper", "card": "HAL Card"}
PORT_PATTERNS = ("/dev/cu.usbserial-*", "/dev/cu.wchusbserial*", "/dev/cu.usbmodem*", "/dev/ttyUSB*", "/dev/ttyACM*")
MAX_SETTINGS = 400  # bytes of settings text a friend takes (hal_friendsetup.h)


def _plain(value: str) -> bool:
    return all(" " <= ch <= "~" for ch in value) and value == value.strip()


def settings_text(ssid: str, password: str) -> str:
    """The friend's settings, as config.txt lines. Raises ValueError, in plain words, for a
    name or password a friend can't take."""
    if not 1 <= len(ssid) <= 32 or not _plain(ssid):
        raise ValueError("The network name must be 1 to 32 plain characters (letters, digits, spaces inside, "
                         "punctuation), with no space at either end.")
    if password and (not 8 <= len(password) <= 63 or not _plain(password)):
        raise ValueError("The password must be 8 to 63 plain characters with no space at either end "
                         "(or empty, for a network without one).")
    text = f"wifi_ssid={ssid}\n" + (f"wifi_password={password}\n" if password else "")
    if len(text) > MAX_SETTINGS:
        raise ValueError("That is more than a friend can keep.")
    return text


def setup_line(text: str) -> str:
    """The one line that carries the settings to him: every byte as two hex digits."""
    return "HALCFG " + text.encode("ascii").hex()


def parse_answer(line: str):
    """What a friend says about himself ("HALCFG nano 0.7.0 wifi=set ip=192.168.1.40"), or None."""
    words = line.strip().split(" ")
    if len(words) != 5 or words[0] != "HALCFG" or words[1] not in NAMES:
        return None
    if words[3] not in ("wifi=set", "wifi=none") or not words[4].startswith("ip="):
        return None
    ip = words[4][3:]
    return {"id": words[1], "version": words[2], "wifi": words[3] == "wifi=set", "ip": "" if ip == "-" else ip}


def candidate_ports(found=None) -> list[str]:
    """USB serial ports a friend could be on."""
    if found is None:
        found = [path for pattern in PORT_PATTERNS for path in glob.glob(pattern)]
    return sorted(set(found))


class Port:
    """A serial port at 115200 baud, a line at a time, with nothing beyond Python's own library."""

    def __init__(self, path: str):
        self._fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        attrs = termios.tcgetattr(self._fd)
        attrs[0] = 0                                                  # input: raw
        attrs[1] = 0                                                  # output: raw
        attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL      # 8 bits, no parity, no modem lines
        attrs[3] = 0                                                  # no echo, no line editing
        attrs[4] = attrs[5] = termios.B115200
        termios.tcsetattr(self._fd, termios.TCSANOW, attrs)
        self._buf = b""

    def write_line(self, line: str) -> None:
        data = (line + "\n").encode("ascii")
        while data:
            _, ready, _ = select.select([], [self._fd], [], 2.0)
            if not ready:
                raise OSError("the port has stopped taking data")
            data = data[os.write(self._fd, data):]

    def read_line(self, timeout: float):
        """The next whole line he sent, or None when `timeout` seconds pass without one."""
        give_up = time.monotonic() + timeout
        while b"\n" not in self._buf:
            left = give_up - time.monotonic()
            if left <= 0:
                return None
            ready, _, _ = select.select([self._fd], [], [], left)
            if ready:
                try:
                    self._buf += os.read(self._fd, 256)
                except BlockingIOError:
                    pass
            self._buf = self._buf[-4096:]  # whatever else he prints (his start-up log) is of no interest
        line, _, self._buf = self._buf.partition(b"\n")
        return line.decode("ascii", "replace").strip()

    def close(self) -> None:
        os.close(self._fd)


def ask(port, question: str, wanted, seconds: float, clock=time.monotonic):
    """Send `question` once a second until a line `wanted` accepts comes back; None if none does."""
    give_up = clock() + seconds
    while clock() < give_up:
        port.write_line(question)
        until = min(give_up, clock() + 1.0)
        while clock() < until:
            line = port.read_line(max(0.05, until - clock()))
            if line is None:
                break
            got = wanted(line)
            if got is not None:
                return got
    return None


def configure(port, read_name=input, read_password=getpass.getpass, say=print, clock=time.monotonic) -> int:
    """The whole conversation with a friend on `port`. Returns the exit code."""
    who = ask(port, "HALCFG?", parse_answer, 15, clock)  # (opening the port may restart him: give him time)
    if who is None:
        say("Nothing on that port answers as a friend HAL. Is he flashed (pio run -e nano -t upload) and switched on?")
        return 1
    name = NAMES[who["id"]]
    state = f"joined to a network as {who['ip']}" if who["ip"] else "set up, not joined" if who["wifi"] else "not set up"
    say(f"Found {name} (firmware {who['version']}). Wi-Fi: {state}.")
    try:
        text = settings_text(read_name("Wi-Fi network name: "), read_password("Password (not shown as you type): "))
    except ValueError as e:
        say(str(e))
        return 1
    port.write_line(setup_line(text))
    answer = None
    for _ in range(20):
        line = port.read_line(0.5)
        if line in ("HALCFG OK", "HALCFG BAD"):
            answer = line
            break
    if answer != "HALCFG OK":
        say(f"{name} didn't take the settings." if answer else f"{name} didn't answer.")
        return 1
    say(f"{name} has the settings. Waiting for him to join the network...")
    joined = ask(port, "HALCFG?", lambda line: (parse_answer(line) or {}).get("ip") or None, 30, clock)
    if not joined:
        say(f"{name} hasn't joined yet. If the name or password was mistyped, run this again; "
            "he keeps trying by himself otherwise (2.4 GHz networks only).")
        return 1
    say(f"{name} has joined as {joined}. HAL Zero finds him as hal-{who['id']}.local: add him to the line "
        f"friends= in HAL Zero's config.txt (docs/SETUP.md).")
    return 0


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="Give a friend HAL your Wi-Fi network, over USB.")
    ap.add_argument("--port", help="his USB serial port (found automatically when only one is plugged in)")
    args = ap.parse_args(argv)
    path = args.port
    if not path:
        ports = candidate_ports()
        if len(ports) != 1:
            print("Plug in one friend and nothing else, or name his port with --port:" if ports else
                  "No USB serial port found. Plug the friend in with a data cable and switch him on.")
            for p in ports:
                print(f"  {p}")
            return 1
        path = ports[0]
    try:
        port = Port(path)
    except OSError as e:
        print(f"Couldn't open {path}: {e.strerror or e}")
        return 1
    try:
        return configure(port)
    except OSError as e:
        print(f"The port stopped working: {e.strerror or e}")
        return 1
    finally:
        port.close()


if __name__ == "__main__":
    sys.exit(main())
