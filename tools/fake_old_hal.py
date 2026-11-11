#!/usr/bin/env python3
"""A stand-in desk HAL for testing HAL Zero's conversations without the CoreS3.

    python3 tools/fake_old_hal.py [--port 9000] [--mode ok|busy|slow|drop|missing|old|freeze] [--line-ms 1500]
                                  [--freeze-after 10]

Modes (how the desk HAL answers "SAY <id> <n>" and "STATUS"):
  ok       PLAYING n <line-ms>, then DONE n when the line would end; STATUS gets a STATE
  busy     BUSY (he's in the middle of something)
  slow     never answers anything (HAL Zero should time out after 2 s)
  drop     hangs up on the first SAY
  missing  MISSING n (no voice file for that line)
  old      link version 1 firmware: says HELLO OLD 1 and ignores STATUS
  freeze   like ok, then after --freeze-after seconds goes silent without hanging up
           (like the CoreS3 losing power: no answers, no FIN)
Every message in and out is printed. Stdlib only.
"""
import argparse
import asyncio


STATE = "STATE up=86400 heap=109388 psram=7340032 cam=12 lines=40 mode=idle rssi=-58 fw=0.4.0 cores=2 mhz=240"


async def serve(reader, writer, mode, line_ms, freeze_after=10.0):
    peer = writer.get_extra_info("peername")
    print(f"connected: {peer}", flush=True)

    async def send(line):
        print(f"  -> {line}", flush=True)
        writer.write((line + "\n").encode())
        await writer.drain()

    await send(f"HELLO OLD {1 if mode == 'old' else 2}")
    loop = asyncio.get_running_loop()
    started = loop.time()
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
                else:
                    await send(f"PLAYING {n} {line_ms}")
                    await asyncio.sleep(line_ms / 1000)
                    await send(f"DONE {n}")
    finally:
        writer.close()
        print("disconnected", flush=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", type=int, default=9000)
    ap.add_argument("--mode", choices=["ok", "busy", "slow", "drop", "missing", "old", "freeze"], default="ok")
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
