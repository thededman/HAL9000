#!/usr/bin/env python3
"""A stand-in desk HAL for testing HAL Zero's conversations without the CoreS3, and a stand-in
for any of the friends (HAL Nano, HAL Paper, HAL Card) without their devices.

    python3 tools/fake_old_hal.py [--as desk|nano|paper|card] [--port 9000] [--mode MODE]
                                  [--line-ms 1500] [--freeze-after 10]

Who he is (--as, default desk):
  desk   the desk HAL: link version 3, speaks clips he downloads
  nano   HAL Nano: link version 4, says IAM nano; "says" a line by showing the words HAL Zero
         sent in NOW and chirping for as long as the firmware would (nothing is downloaded)
  paper  HAL Paper: the same, holding the words for a reading time
  card   HAL Card: link version 4, speaks clips he downloads, which must be unsigned 8-bit and
         fit his 96 KB; one clip at a time
Several can run at once, each on its own --port.

Modes (how the desk HAL answers):
  ok         PLAYING n <ms>, then DONE n when the line would end; STATUS gets a STATE.
             Speaks link version 3: FETCH really downloads the clip and answers READY n (or
             MISSING n), "SAY live n" plays a fetched clip for its real length, SENSE gets SENSORS.
  busy       BUSY (he's in the middle of something)
  slow       never answers anything (HAL Zero should time out after 2 s)
  drop       hangs up on the first SAY
  missing    MISSING n (no voice file for that line)
  old        link version 1 firmware: says HELLO OLD 1 and ignores STATUS
  v2         link version 2 firmware (v0.4-v0.5): no FETCH, READY, SENSE
  nofetch    like ok, but every FETCH is answered MISSING (he can't reach the brain)
  slowfetch  like ok, but a FETCH is never answered
  freeze     like ok, then after --freeze-after seconds goes silent without hanging up
             (like the CoreS3 losing power: no answers, no FIN)
  flood      says HELLO, then pours PONGs out without end (HAL Zero should hang up at once,
             carry on unharmed, and try again within 5 s)
Every message in and out is printed. Stdlib only.
"""
import argparse
import http.client
import asyncio
import io
import re
import urllib.request
import wave

STATE = "STATE up=86400 heap=109388 psram=7340032 cam=12 lines=40 mode=idle rssi=-58 fw=0.6.0 cores=2 mhz=240"
SENSORS = "SENSORS light=67 prox=19 tilt=3 bat=37 chg=1 temp=42"
MAX_CLIP = 1536000
CARD_MAX_CLIP = 96 * 1024  # all HAL Card's device can hold (it has no PSRAM)
CAST = ("zero", "desk", "nano", "paper", "card")
# What each stand-in reports about himself. A friend has no light sensor (-1) or camera (0).
_STATES = {
    "desk": STATE,
    "nano": "STATE up=1260 heap=148212 psram=0 cam=0 lines=12 mode=idle rssi=-61 fw=0.7.0 cores=2 mhz=240",
    "paper": "STATE up=93600 heap=201776 psram=4063232 cam=0 lines=31 mode=idle rssi=-55 fw=0.7.0 cores=2 mhz=240",
    "card": "STATE up=7300 heap=101408 psram=0 cam=0 lines=18 mode=idle rssi=-49 fw=0.7.0 cores=2 mhz=240",
}
_SENSORS = {
    "desk": SENSORS,
    "nano": "SENSORS light=-1 prox=0 tilt=38 bat=81 chg=0 temp=44",
    "paper": "SENSORS light=-1 prox=0 tilt=0 bat=64 chg=0 temp=22",
    "card": "SENSORS light=-1 prox=0 tilt=0 bat=55 chg=1 temp=48",
}
_DIRECT = urllib.request.build_opener(urllib.request.ProxyHandler({}))


def hello_version(mode: str) -> int:
    return 1 if mode == "old" else 2 if mode == "v2" else 3


def voiced(who: str) -> bool:
    """Does he speak a clip (the desk HAL, HAL Card), or only show words (HAL Nano, HAL Paper)?"""
    return who in ("desk", "card")


def hello_lines(who: str, mode: str) -> list[str]:
    """What he says on connecting: the desk HAL his version; a friend version 4 and who he is."""
    if who == "desk":
        return [f"HELLO OLD {hello_version(mode)}"]
    return ["HELLO OLD 4", f"IAM {who}"]


def state_for(who: str) -> str:
    return _STATES[who]


def sensors_for(who: str) -> str:
    return _SENSORS[who]


def parse_now(line: str):
    """"NOW <n> <who> <text>" as (n, who, text), or None: read as the firmware reads it, with single
    spaces between the fields and the words taken exactly as written (1..150 printable ASCII)."""
    m = re.fullmatch(r"NOW (\d{1,2}) ([a-z]+) (.*)", line)
    if not m:
        return None
    n, who, text = int(m.group(1)), m.group(2), m.group(3)
    if not 1 <= n <= 99 or who not in CAST or not 1 <= len(text) <= 150:
        return None
    if text[0] == " " or text[-1] == " " or any(not " " <= ch <= "~" for ch in text):
        return None
    return n, who, text


def chirp_ms(text: str) -> int:
    """How long HAL Nano chirps a line: the same sum as lib/hal_core/src/hal_chirp.cpp."""
    total, any_chirp = 0, False
    for ch in text:
        if ch == " ":
            total += 60 if any_chirp else 0
        elif ch in ".,!;:":
            total, any_chirp = total + 180, True
        elif ch == "?":
            total, any_chirp = total + 270, True
        elif " " < ch <= "~":
            total, any_chirp = total + 52, True
    return max(total, 2500)


def reading_ms(text: str) -> int:
    """How long HAL Paper holds a line: the same sum as lib/hal_core/src/hal_minutes.cpp."""
    return min(max(1500 + 55 * len(text), 4000), 12000)


def words_ms(who: str, text: str) -> int:
    return chirp_ms(text) if who == "nano" else reading_ms(text)


def is_private_ipv4(dotted: str) -> bool:
    parts = dotted.split(".")
    if len(parts) != 4 or not all(p.isascii() and p.isdigit() and len(p) <= 3 and (p == "0" or p[0] != "0") for p in parts):
        return False
    a, b, c, d = (int(p) for p in parts)
    if max(a, b, c, d) > 255:
        return False
    return a == 10 or (a == 172 and 16 <= b <= 31) or (a == 192 and b == 168)


def parse_fetch(words: list[str]):
    """("FETCH", n, ip, port, token) as (n, ip, port, token), or None when the real desk HAL would ignore it."""
    if len(words) != 5 or words[0] != "FETCH":
        return None
    _, n, ip, port, token = words
    if not (n.isdigit() and 1 <= int(n) <= 99 and port.isdigit() and 1024 <= int(port) <= 65535):
        return None
    if not is_private_ipv4(ip) or not re.fullmatch(r"[0-9a-f]{16,32}", token):
        return None
    return int(n), ip, int(port), token


def wav_ms(data: bytes) -> int:
    """How long a 16-bit PCM WAV plays, in ms; 0 when it isn't one."""
    try:
        with wave.open(io.BytesIO(data), "rb") as w:
            if w.getsampwidth() != 2 or w.getcomptype() != "NONE":
                return 0
            return w.getnframes() * 1000 // w.getframerate()
    except Exception:
        return 0


def clip_ms(who: str, data: bytes) -> int:
    """How long a downloaded clip plays for `who`, or 0 if it isn't one he can play: the desk HAL
    takes 16-bit PCM up to 1.5 MB, HAL Card unsigned 8-bit mono that fits his 96 KB."""
    if who != "card":
        return wav_ms(data) if len(data) <= MAX_CLIP else 0
    if len(data) > CARD_MAX_CLIP:
        return 0
    try:
        with wave.open(io.BytesIO(data), "rb") as w:
            if w.getsampwidth() != 1 or w.getnchannels() != 1 or w.getcomptype() != "NONE":
                return 0
            return w.getnframes() * 1000 // w.getframerate()
    except Exception:
        return 0


def download(ip: str, port: int, token: str, who: str = "desk") -> int:
    """Fetch the clip like the HAL would; its length in ms, or 0 if it can't be had or played."""
    try:
        with _DIRECT.open(f"http://{ip}:{port}/clip/{token}", timeout=8) as reply:
            data = reply.read(MAX_CLIP + 1)
    except (OSError, http.client.HTTPException):  # nobody there, or not a web server at all
        return 0
    return clip_ms(who, data)


async def serve(reader, writer, mode, line_ms, freeze_after=10.0, who="desk"):
    peer = writer.get_extra_info("peername")
    print(f"connected: {peer}", flush=True)
    version = hello_version(mode) if who == "desk" else 4
    clips: dict[int, int] = {}  # live line number -> its length in ms
    playing = None              # the line being "spoken": cancelled by END
    now = None                  # the last NOW: (n, who, text)

    async def send(line):
        print(f"  -> {line}", flush=True)
        writer.write((line + "\n").encode())
        await writer.drain()

    async def speak(n, ms):
        await send(f"PLAYING {n} {ms}")
        await asyncio.sleep(ms / 1000)
        await send(f"DONE {n}")

    for line in hello_lines(who, mode):
        await send(line)
    loop = asyncio.get_running_loop()
    started = loop.time()
    if mode == "flood":
        print("  (flooding: PONG after PONG, as fast as they go)", flush=True)
        poured = 0
        try:
            while True:
                writer.write(b"PONG\n" * 200)
                await writer.drain()
                poured += 200
        except OSError:  # he hung up
            pass
        finally:
            writer.close()
            print(f"disconnected after about {poured} lines in {loop.time() - started:.1f} s", flush=True)
        return
    try:
        while True:
            raw = await reader.readline()
            if not raw:
                break
            if mode == "freeze" and loop.time() - started > freeze_after:
                print("  (frozen: reading, never answering)", flush=True)
                while await reader.readline():
                    pass
                break
            line = raw.decode(errors="replace").strip()
            print(f"  <- {line}", flush=True)
            words = line.split()
            if not words:
                continue
            if words[0] == "PING":
                await send("PONG")
            elif words[0] == "STATUS" and len(words) == 1:
                if mode not in ("slow", "old"):
                    await send(state_for(who))
            elif words[0] == "SENSE" and len(words) == 1:
                if version >= 3 and mode != "slow":
                    await send(sensors_for(who))
            elif words[0] == "NOW":
                heard = parse_now(line) if version >= 4 else None
                if heard:  # what his screen would show: who is speaking, and the words
                    now = heard
                    print(f"  (screen) {heard[1]}: {heard[2]}", flush=True)
            elif words[0] == "FETCH":
                fetch = parse_fetch(words)
                if version < 3 or fetch is None or mode in ("slow", "slowfetch"):
                    continue  # too old to know the word, not one he'd accept, or never answering
                n, ip, port, token = fetch
                speaking = playing is not None and not playing.done()
                if not voiced(who) or (who == "card" and speaking):
                    await send(f"MISSING {n}")  # nothing to play it with, or his one clip is in use
                    continue
                if who == "card":
                    clips.clear()  # room for one
                ms = 0 if mode == "nofetch" else await loop.run_in_executor(None, download, ip, port, token, who)
                if ms:
                    clips[n] = ms
                    await send(f"READY {n}")
                else:
                    await send(f"MISSING {n}")
            elif words[0] == "END":
                clips.clear()
                now = None
                if playing and not playing.done():
                    playing.cancel()
                    print("  (stopped speaking)", flush=True)
            elif words[0] == "SAY" and len(words) == 3:
                n = words[2]
                if mode == "busy":
                    await send("BUSY")
                elif mode == "slow":
                    pass
                elif mode == "drop":
                    print("  (hanging up)", flush=True)
                    break
                elif mode == "missing":
                    await send(f"MISSING {n}")
                elif words[1] == "live" and not voiced(who):
                    # A HAL with no voice says the words of the NOW that named him, for as long as
                    # chirping or reading them takes.
                    if playing is not None and not playing.done():
                        await send("BUSY")
                    elif now and n.isdigit() and int(n) == now[0] and now[1] == who:
                        print(f"  (says) {now[2]}", flush=True)
                        playing = asyncio.ensure_future(speak(n, words_ms(who, now[2])))
                    else:
                        await send(f"MISSING {n}")
                elif words[1] == "live":
                    if n.isdigit() and int(n) in clips:
                        playing = asyncio.ensure_future(speak(n, clips.pop(int(n)) if who == "card" else clips[int(n)]))
                    else:
                        await send(f"MISSING {n}")
                elif who != "desk":
                    await send(f"MISSING {n}")  # the scripts on the card are the desk HAL's
                else:
                    playing = asyncio.ensure_future(speak(n, line_ms))
    finally:
        if playing and not playing.done():
            playing.cancel()
        writer.close()
        print("disconnected", flush=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--as", dest="who", default="desk", choices=["desk", "nano", "paper", "card"],
                    help="which HAL to stand in for (default: the desk HAL)")
    ap.add_argument("--port", type=int, default=9000)
    ap.add_argument("--mode", default="ok", choices=["ok", "busy", "slow", "drop", "missing", "old", "v2", "nofetch",
                                                     "slowfetch", "freeze", "flood"])
    ap.add_argument("--freeze-after", type=float, default=10.0, help="seconds before freeze mode goes silent")
    ap.add_argument("--line-ms", type=int, default=1500)
    args = ap.parse_args()

    async def run():
        server = await asyncio.start_server(lambda r, w: serve(r, w, args.mode, args.line_ms, args.freeze_after, args.who),
                                            "0.0.0.0", args.port)
        name = {"desk": "desk HAL", "nano": "HAL Nano", "paper": "HAL Paper", "card": "HAL Card"}[args.who]
        print(f"fake {name} listening on port {args.port}, mode {args.mode}", flush=True)
        async with server:
            await server.serve_forever()

    try:
        asyncio.run(run())
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
