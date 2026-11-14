"""Read what the HALs say to each other, without the server: whole conversations from the real
local model, printed with notes on anything that went wrong.

    tools/.venv/bin/python -m brain.try_cast                       # one five-way chat
    tools/.venv/bin/python -m brain.try_cast --count 20 --silent   # twenty, without rendering audio
    tools/.venv/bin/python -m brain.try_cast --kind argue --topic "tea or coffee"
    tools/.venv/bin/python -m brain.try_cast --kind roll --cast zero,desk,card

It builds its own brain in this process (Ollama on this computer, the real voices) and never
touches the brain that is serving the HALs, its port or its settings file. For reading a change
to a persona or a prompt before any device hears it.
"""
import argparse
import random
import re
import sys
import time

from . import cast, persona
from .clips import ClipStore
from .core import Brain
from .debate import Debate, default_turns
from .memory import ChatMemory
from .providers import ollama
from .subjects import Subjects

ARGUMENTS = ["tea or coffee", "is a small screen better than a large one", "should Dave tidy his desk",
             "which is better, morning or night", "does Dave need this many computers",
             "is it better to be carried about or to stay put", "should a computer ever sleep"]
READINGS = ["time: 21:40", "zero chip temperature: 54 C", "zero free memory: 131 MB", "desk battery: 53 % and charging",
            "desk room: 22 C", "nano battery: 80 %", "paper battery: 91 %", "card free memory: 96 KB"]
_ANY_LABEL = re.compile(r"\bHAL(?:\s*(?:" + "|".join(m.name.removeprefix("HAL ") for m in cast.MEMBERS) + r"))?\s*:",
                        re.I)


class Recording:
    """The real model, keeping what it wrote before any cleaning."""

    def __init__(self, provider):
        self._provider, self.raw, self.again = provider, [], set()
        self.name, self.model = provider.name, provider.model

    def chat(self, system, messages, max_tokens=80):
        reply = self._provider.chat(system, messages, max_tokens)
        if messages and messages[-1]["content"] == persona.SAY_SOMETHING_NEW:
            self.raw[-1] = reply  # a second go at the same line: it had repeated something
            self.again.add(len(self.raw) - 1)
        else:
            self.raw.append(reply)
        return reply


class Silent:
    """Stands in for the voices when only the words are being read: an empty clip for every line."""

    def render(self, who, text):
        return b""


def notes(line, raw: str, again: bool) -> list[str]:
    """What a reader should look at twice in one line."""
    member = cast.member(line.who)
    found = ["asked again: he had repeated a line"] if again else []
    if line.text[:1].islower():
        found.append("lower case")
    if _ANY_LABEL.match(raw.strip()):
        found.append("wrote a name in front")
    elif _ANY_LABEL.search(raw):
        found.append("NAME LABEL INSIDE THE LINE")
    if line.text in member.spare:
        found.append("too large a clip: stock line")
    elif line.text == member.own_words:
        found.append("film line: own words")
    elif line.text == persona.FALLBACK:
        found.append("nothing usable: fallback")
    elif len(" ".join(raw.split())) > member.max_chars + 10:
        found.append(f"cut from {len(' '.join(raw.split()))}")
    return found


def run_one(brain, recording, kind: str, members, topic: str, turns: int, rng) -> dict:
    """One conversation, printed as it comes. Returns counts for the summary."""
    print(f"\n=== {kind}: {topic or '(roll call)'}  [{', '.join(members)}]")
    talk = Debate(brain, topic, [f for f in READINGS if f.split(" ")[0] in members or f.startswith("time")],
                  turns, kind=kind, cast=members, rng=rng, idle_s=600)
    tally = {"lines": 0, "flagged": 0, "card_bytes": []}
    start = len(recording.raw)
    for n in range(talk.turns):
        status, line = talk.get(n, timeout_s=120)
        if status != "line":
            print(f"  (stopped at line {n}: {status} {line or ''})")
            break
        member, found = cast.member(line.who), notes(line, recording.raw[start + n], start + n in recording.again)
        size = ""
        if line.clip and brain.clips.get(line.clip):
            wav = brain.clips.get(line.clip)
            size = f", {len(wav)} bytes, {line.ms / 1000:.1f} s"
            if line.who == "card":
                tally["card_bytes"].append(len(wav))
        flag = f"   <-- {'; '.join(found)}" if found else ""
        print(f"  {member.name}: {line.text}\n      [{len(line.text)}/{member.max_chars}{size}]{flag}")
        tally["lines"] += 1
        tally["flagged"] += bool(found)
    talk.stop()
    return tally


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(prog="python -m brain.try_cast", description=__doc__.split("\n\n")[0])
    ap.add_argument("--kind", choices=["chat", "argue", "roll"], default="chat")
    ap.add_argument("--cast", default=",".join(cast.ORDER), help="who is talking (default: all five)")
    ap.add_argument("--count", type=int, default=1, help="how many conversations")
    ap.add_argument("--turns", type=int, help="lines in each (default: about two each)")
    ap.add_argument("--topic", help="what it is about (default: drawn from the chat subjects, or a sample argument)")
    ap.add_argument("--seed", type=int, default=1, help="decides who speaks when; the same seed gives the same order")
    ap.add_argument("--model", default=ollama.DEFAULT_MODEL, help="the Ollama model to write the lines")
    ap.add_argument("--silent", action="store_true", help="don't render audio (faster; no clip sizes)")
    args = ap.parse_args(argv[1:])
    try:
        members = cast.parse_cast(args.cast)
    except ValueError as e:
        ap.error(str(e))

    recording = Recording(ollama.OllamaProvider(model=args.model, timeout_s=60))
    if args.silent:
        voice = Silent()
    else:
        from .voice import Voice
        voice = Voice()
    brain = Brain(recording, voice, None, ClipStore(), ChatMemory())
    subjects, total, began = Subjects(), {"lines": 0, "flagged": 0, "card_bytes": []}, time.monotonic()
    for i in range(args.count):
        topic = "" if args.kind == "roll" else args.topic or (
            subjects.pick() if args.kind == "chat" else ARGUMENTS[(args.seed + i) % len(ARGUMENTS)])
        tally = run_one(brain, recording, args.kind, members, topic, args.turns or default_turns(members),
                        random.Random(args.seed + i))
        for key in total:
            total[key] += tally[key]
    sizes, limit = total["card_bytes"], cast.member("card").clip_bytes
    print(f"\n{args.count} conversation(s), {total['lines']} lines, {total['flagged']} with a note, "
          f"{time.monotonic() - began:.0f} s.")
    if sizes:
        print(f"HAL Card's clips: {len(sizes)}, largest {max(sizes)} bytes of {limit} "
              f"({sum(s > limit * 0.9 for s in sizes)} within a tenth of the limit).")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
