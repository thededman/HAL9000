"""The brain's settings file: brain/.env, KEY=VALUE lines. It is git-ignored: keys live there."""
import pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
ENV_PATH = ROOT / "brain" / ".env"
DEFAULT_PORT = 9100


def read_env(path=ENV_PATH) -> dict[str, str]:
    """Settings from a KEY=VALUE file. Comments, blank lines and lines without '=' are skipped."""
    env: dict[str, str] = {}
    try:
        text = pathlib.Path(path).read_text(encoding="utf-8")
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


def port_from(env: dict[str, str]) -> int:
    """HAL_BRAIN_PORT, or 9100 when it is missing or not a usable port."""
    try:
        port = int(env.get("HAL_BRAIN_PORT", DEFAULT_PORT))
    except ValueError:
        return DEFAULT_PORT
    return port if 1024 <= port <= 65535 else DEFAULT_PORT
