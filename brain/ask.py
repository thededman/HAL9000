"""Ask a HAL a question from this computer: prints what he says and plays it.

    tools/brain.sh ask zero "What do you think of the old model?"
    tools/brain.sh ask desk --fact "battery: 37 % charging" "How is your battery?"

Talks to the brain running on this computer (tools/brain.sh start).
"""
import argparse
import pathlib
import subprocess
import sys
import tempfile
import urllib.error
import urllib.parse
import urllib.request

from .config import port_from, read_env
from .voice import _make_voice


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(prog="tools/brain.sh ask", description="Ask a HAL a question.")
    ap.add_argument("who", choices=["zero", "desk"], help="zero: HAL Zero; desk: HAL 9000")
    ap.add_argument("question", nargs="+")
    ap.add_argument("--fact", action="append", default=[], metavar="NAME: VALUE",
                    help="something he knows right now (may be repeated)")
    ap.add_argument("--no-play", action="store_true", help="print the reply only")
    args = ap.parse_args(argv[1:])

    env = read_env()
    base = f"http://127.0.0.1:{port_from(env)}"
    body = ("\n".join(args.fact) + "\n\n" + " ".join(args.question)).encode("utf-8")
    headers = {"X-HAL-Key": env["HAL_BRAIN_KEY"]} if env.get("HAL_BRAIN_KEY") else {}
    direct = urllib.request.build_opener(urllib.request.ProxyHandler({}))  # this computer: never through a proxy
    request = urllib.request.Request(f"{base}/ask?who={args.who}", body, headers)
    try:
        with direct.open(request, timeout=60) as reply:
            text = reply.read().decode("utf-8")
    except urllib.error.HTTPError as e:
        print(f"The brain said no: {e.code} {e.read().decode('utf-8', 'replace').strip()}", file=sys.stderr)
        return 1
    except OSError as e:
        print(f"The brain isn't answering (tools/brain.sh start): {e}", file=sys.stderr)
        return 1
    got = dict(line.split("=", 1) for line in text.splitlines() if "=" in line)
    print(got.get("text", ""))
    if got.get("show"):
        print(f"(he would bring up his '{got['show']}' screen)")
    if args.no_play or "clip" not in got:
        return 0
    player = _make_voice().player_command()
    if not player:
        print("(no audio player found: this needs afplay, aplay or paplay)", file=sys.stderr)
        return 0
    try:
        with direct.open(f"{base}/clip/{urllib.parse.quote(got['clip'])}", timeout=30) as clip:
            audio = clip.read()
    except OSError as e:
        print(f"(couldn't fetch the audio: {e})", file=sys.stderr)
        return 0
    with tempfile.TemporaryDirectory() as folder:
        wav = pathlib.Path(folder) / "reply.wav"
        wav.write_bytes(audio)
        subprocess.run(player + [str(wav)], check=False)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
