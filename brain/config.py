"""The brain's settings file: brain/.env, KEY=VALUE lines. It is git-ignored: keys live there."""
import os
import pathlib
import re
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
ENV_PATH = ROOT / "brain" / ".env"
DEFAULT_PORT = 9100


def read_env(path=ENV_PATH) -> dict[str, str]:
    """Settings from a KEY=VALUE file. Comments, blank lines and lines without '=' are skipped."""
    env: dict[str, str] = {}
    try:
        text = pathlib.Path(path).read_text(encoding="utf-8", errors="replace")
    except OSError:
        return env
    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        name, value = line.split("=", 1)
        value = value.strip()
        if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
            value = value[1:-1]
        env[name.strip()] = value
    return env


def save_setting(path, name: str, value: str) -> None:
    """Put NAME=value in the settings file, keeping its other lines. The file ends up private
    (mode 600) and is swapped in whole, never left half-written. Nothing in it is read out."""
    path = pathlib.Path(path)
    try:
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError:
        lines = []
    lines = [line for line in lines if not re.match(rf"\s*{re.escape(name)}\s*=", line)] + [f"{name}={value}"]
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


def port_from(env: dict[str, str]) -> int:
    """HAL_BRAIN_PORT, or 9100 when it is missing or not a usable port."""
    try:
        port = int(env.get("HAL_BRAIN_PORT", DEFAULT_PORT))
    except ValueError:
        return DEFAULT_PORT
    return port if 1024 <= port <= 65535 else DEFAULT_PORT


def valid_key(key) -> bool:
    """The shared key is 32 lower-case hex characters: the only kind HAL Zero accepts."""
    return isinstance(key, str) and re.fullmatch(r"[0-9a-f]{32}", key) is not None
