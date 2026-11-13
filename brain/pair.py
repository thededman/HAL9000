"""Share a key between this computer's brain and HAL Zero, and tell him where the brain is.

    tools/brain.sh pair [user@host]        (default pi@cardputerzero.local)

Writes brain=on, brain_host, brain_port and brain_key into ~/hal/config.txt on the Cardputer
over SSH, leaving his other settings as they are, and then saves the key in brain/.env. The key
travels on standard input: it is never printed and never part of a command line.
"""
import os
import pathlib
import platform
import re
import secrets
import socket
import subprocess
import sys
import tempfile

from .config import ENV_PATH, port_from, read_env, valid_key

SSH = ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=8"]
# Runs on the Cardputer: replace any earlier brain settings in HAL Zero's file with the four
# lines on standard input. Every step must succeed before the file is swapped; otherwise the
# old file stays exactly as it was. (grep -v exits 1 when it selects no line: not an error.)
REMOTE = (
    'umask 077; f="$HOME/hal/config.txt"; '
    '[ -f "$f" ] || { echo "no settings file: deploy HAL Zero first" >&2; exit 3; }; '
    'f=$(readlink -f "$f" 2>/dev/null || echo "$f"); n="$f.new"; '
    '{ grep -v -E "^[[:space:]]*(brain|brain_host|brain_port|brain_key)[[:space:]]*=" "$f" > "$n" || [ $? -eq 1 ]; } '
    '&& cat >> "$n" '
    '&& [ "$(grep -c -E "^brain(_host|_port|_key)?=" "$n")" -eq 4 ] '
    '&& mv "$n" "$f" || { rm -f "$n"; exit 1; }'
)


def check_target(target: str) -> str:
    if not re.fullmatch(r"[A-Za-z0-9._-]+@[A-Za-z0-9.-]+", target) or target.startswith("-"):
        raise ValueError(f"pair wants user@host, e.g. pi@cardputerzero.local (got {target!r})")
    return target


def config_lines(host_name: str, port: int, key: str) -> str:
    return f"brain=on\nbrain_host={host_name}\nbrain_port={port}\nbrain_key={key}\n"


def read_key(env_path=ENV_PATH) -> str | None:
    """The shared key in the settings file, or None when there is none yet.

    Raises ValueError (without repeating the key) when there is one that HAL Zero would ignore.
    """
    key = read_env(env_path).get("HAL_BRAIN_KEY", "").strip()
    if not key:
        return None
    if not valid_key(key):
        raise ValueError("HAL_BRAIN_KEY in brain/.env isn't 32 lower-case hex characters, so HAL Zero would "
                         "ignore it. Delete that line and pair again: a new key is made for you.")
    return key


def save_key(env_path, key: str) -> None:
    """Put the key in the settings file, keeping its other lines. The file ends up private
    (mode 600) and is swapped in whole, never left half-written."""
    path = pathlib.Path(env_path)
    try:
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError:
        lines = []
    lines = [line for line in lines if not re.match(r"\s*HAL_BRAIN_KEY\s*=", line)] + [f"HAL_BRAIN_KEY={key}"]
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, tmp = tempfile.mkstemp(dir=path.parent, prefix=".env.")  # created with mode 600
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as f:
            f.write("\n".join(lines) + "\n")
        os.replace(tmp, path)
    except BaseException:
        if os.path.exists(tmp):
            os.unlink(tmp)
        raise


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


def main(argv: list[str], env_path=ENV_PATH, quiet: bool = False) -> int:
    def say(message: str) -> None:
        if not quiet:
            print(message, file=sys.stderr)

    try:
        target = check_target(argv[1] if len(argv) > 1 else "pi@cardputerzero.local")
        key = read_key(env_path)
    except ValueError as e:
        say(str(e))
        return 2
    fresh = key is None
    if fresh:
        key = secrets.token_hex(16)
    port = port_from(read_env(env_path))
    done = subprocess.run(SSH + [target, REMOTE], input=config_lines(this_host(), port, key).encode(),
                          capture_output=quiet)
    if done.returncode == 3:
        say(f"HAL Zero's settings file isn't on {target} yet: run tools/cardputer.sh deploy first.")
        return 1
    if done.returncode != 0:
        say(f"Couldn't pair with {target} (is the Cardputer on, with key login set up?). "
            "Nothing was changed on either side.")
        return 1
    if fresh:  # only now: an unpaired brain must not start listening on the network
        save_key(env_path, key)
    else:
        os.chmod(env_path, 0o600)
    say(f"Paired: {target} now looks for the brain at {this_host()}:{port}.\n"
        "Restart the brain (tools/brain.sh stop, then start) and HAL Zero to use it.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
