"""Share a key between this computer's brain and HAL Zero, and tell him where the brain is.

    tools/brain.sh pair [user@host]        (default pi@cardputerzero.local)

Makes a key if brain/.env has none, and writes brain=on, brain_host, brain_port and brain_key
into ~/hal/config.txt on the Cardputer over SSH. The key travels on standard input: it is
never printed and never part of a command line.
"""
import os
import pathlib
import platform
import re
import secrets
import socket
import subprocess
import sys

from .config import ENV_PATH, port_from, read_env

SSH = ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=8"]
# Replace any earlier brain settings in HAL Zero's file with the lines on standard input.
REMOTE = ('umask 077; f="$HOME/hal/config.txt"; mkdir -p "$HOME/hal"; touch "$f"; '
          'grep -v -E "^(brain|brain_host|brain_port|brain_key)=" "$f" > "$f.new"; cat >> "$f.new"; mv "$f.new" "$f"')


def check_target(target: str) -> str:
    if not re.fullmatch(r"[A-Za-z0-9._-]+@[A-Za-z0-9.-]+", target) or target.startswith("-"):
        raise ValueError(f"pair wants user@host, e.g. pi@cardputerzero.local (got {target!r})")
    return target


def config_lines(host_name: str, port: int, key: str) -> str:
    return f"brain=on\nbrain_host={host_name}\nbrain_port={port}\nbrain_key={key}\n"


def ensure_key(env_path=ENV_PATH) -> str:
    """The shared key from the settings file, made and saved (mode 600) if there is none yet."""
    path = pathlib.Path(env_path)
    key = read_env(path).get("HAL_BRAIN_KEY", "").strip()
    if key:
        return key
    key = secrets.token_hex(16)
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except OSError:
        lines = []
    lines = [line for line in lines if not re.match(r"\s*HAL_BRAIN_KEY\s*=", line)] + [f"HAL_BRAIN_KEY={key}"]
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(os.open(path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600), "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    os.chmod(path, 0o600)
    return key


def this_host() -> str:
    """This computer's name on the local network, as HAL Zero can look it up (name.local)."""
    name = ""
    if platform.system() == "Darwin":
        try:
            name = subprocess.run(["scutil", "--get", "LocalHostName"], capture_output=True, text=True,
                                  timeout=5).stdout.strip()
        except (OSError, subprocess.TimeoutExpired):
            pass
    name = name or socket.gethostname().split(".")[0]
    name = re.sub(r"[^A-Za-z0-9-]", "-", name) or "localhost"
    return name + ".local"


def main(argv: list[str]) -> int:
    try:
        target = check_target(argv[1] if len(argv) > 1 else "pi@cardputerzero.local")
    except ValueError as e:
        print(e, file=sys.stderr)
        return 2
    port = port_from(read_env())
    lines = config_lines(this_host(), port, ensure_key())
    done = subprocess.run(SSH + [target, REMOTE], input=lines.encode())
    if done.returncode != 0:
        print(f"Couldn't reach {target} (is HAL Zero's Cardputer on, with key login set up?)", file=sys.stderr)
        return 1
    print(f"Paired: {target} now looks for the brain at {this_host()}:{port}.\n"
          "Restart the brain (tools/brain.sh stop, then start) and HAL Zero to use it.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
