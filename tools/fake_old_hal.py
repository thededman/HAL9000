#!/usr/bin/env python3
"""A stand-in desk HAL for testing HAL Zero's conversations without the CoreS3.

    python3 tools/fake_old_hal.py [--port 9000] [--mode MODE] [--line-ms 1500] [--freeze-after 10]

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
_DIRECT = urllib.request.build_opener(urllib.request.ProxyHandler({}))


def hello_version(mode: str) -> int:
    return 1 if mode == "old" else 2 if mode == "v2" else 3


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


def download(ip: str, port: int, token: str) -> int:
    """Fetch the clip like the desk HAL would; its length in ms, or 0 if it can't be had."""
    try:
        with _DIRECT.open(f"http://{ip}:{port}/clip/{token}", timeout=8) as reply:
            data = reply.read(MAX_CLIP + 1)
    except (OSError, http.client.HTTPException):  # nobody there, or not a web server at all
        return 0
    return wav_ms(data) if len(data) <= MAX_CLIP else 0


async def serve(reader, writer, mode, line_ms, freeze_after=10.0):
    peer = writer.get_extra_info("peername")
    print(f"connected: {peer}", flush=True)
    version = hello_version(mode)
    clips: dict[int, int] = {}  # live line number -> its length in ms
    playing = None              # the line being "spoken": cancelled by END

    async def send(line):
        print(f"  -> {line}", flush=True)
        writer.write((line + "\n").encode())
        await writer.drain()

    async def speak(n, ms):
        await send(f"PLAYING {n} {ms}")
        await asyncio.sleep(ms / 1000)
        await send(f"DONE {n}")

    await send(f"HELLO OLD {version}")
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
                    await send(STATE)
            elif words[0] == "SENSE" and len(words) == 1:
                if version >= 3 and mode != "slow":
                    await send(SENSORS)
            elif words[0] == "FETCH":
                fetch = parse_fetch(words)
                if version < 3 or fetch is None or mode in ("slow", "slowfetch"):
                    continue  # too old to know the word, not one he'd accept, or never answering
                n, ip, port, token = fetch
                ms = 0 if mode == "nofetch" else await loop.run_in_executor(None, download, ip, port, token)
                if ms:
                    clips[n] = ms
                    await send(f"READY {n}")
                else:
                    await send(f"MISSING {n}")
            elif words[0] == "END":
                clips.clear()
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
                elif words[1] == "live":
                    if n.isdigit() and int(n) in clips:
                        playing = asyncio.ensure_future(speak(n, clips[int(n)]))
                    else:
                        await send(f"MISSING {n}")
                else:
                    playing = asyncio.ensure_future(speak(n, line_ms))
    finally:
        if playing and not playing.done():
            playing.cancel()
        writer.close()
        print("disconnected", flush=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", type=int, default=9000)
    ap.add_argument("--mode", default="ok", choices=["ok", "busy", "slow", "drop", "missing", "old", "v2", "nofetch",
                                                     "slowfetch", "freeze", "flood"])
    ap.add_argument("--freeze-after", type=float, default=10.0, help="seconds before freeze mode goes silent")
    ap.add_argument("--line-ms", type=int, default=1500)
    args = ap.parse_args()

    async def run():
        server = await asyncio.start_server(lambda r, w: serve(r, w, args.mode, args.line_ms, args.freeze_after), "0.0.0.0", args.port)
        print(f"fake desk HAL listening on port {args.port}, mode {args.mode}", flush=True)
        async with server:
            await server.serve_forever()

    try:
        asyncio.run(run())
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
