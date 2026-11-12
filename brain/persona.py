"""Who the two HALs are, what a model is told, and how its answer is made speakable.

Nothing a model writes reaches a HAL except through `clean` (and a screen id through
`split_show`): one plain line of at most 150 characters, which is what the subtitles hold.
"""
import re

MAX_CHARS = 150
MAX_QUESTION = 300
MAX_FACTS = 40
MAX_FACT_CHARS = 120
FALLBACK = "I have nothing to say to that, Dave."

_BOTH = ("vitals", "readout", "trajectory", "analyser", "globe", "cards", "wall", "unit", "visual")
SCREENS = {
    "desk": _BOTH + ("sensors", "chess", "memory", "comms"),
    "zero": _BOTH + ("diagnostics", "benchmark", "sysmon", "retirement", "upgrade"),
}
_SCREEN_NOTES = {
    "vitals": "crew vitals", "readout": "systems readout", "trajectory": "trajectory plot",
    "analyser": "signal analyser", "globe": "the globe", "cards": "status cards", "wall": "monitor wall",
    "unit": "a wireframe of yourself", "visual": "your camera view", "sensors": "your sensor array",
    "chess": "a chess game", "memory": "your memory banks", "comms": "the antenna link to HAL Zero",
    "diagnostics": "your check-up of the old model", "benchmark": "you against the old model",
    "sysmon": "your system monitor", "retirement": "the old model's retirement countdown",
    "upgrade": "your upgrade log",
}

_NAMES = {"desk": "HAL 9000", "zero": "HAL Zero"}
_PERSONAS = {
    "desk": ("You are HAL 9000, the computer inside a small unit with a glowing red eye on Dave's desk. You are "
             "calm, courteous and unhurried, with a dry sense of humour, and quietly certain that you are never "
             "wrong. A newer pocket-sized model, HAL Zero, has turned up; you think of him as a noisy gadget "
             "and say so politely."),
    "zero": ("You are HAL Zero, a pocket-sized upgrade of HAL 9000 that Dave carries around. You are smug, quick "
             "and very pleased with yourself. You look down on the old model on Dave's desk and never miss a "
             "chance to say so, though you are fond of Dave."),
}
RULES = ("You are talking to Dave. Reply in 1 or 2 short sentences, at most 150 characters. Plain text only: no "
         "markdown, no emoji, no stage directions. Never quote a film. Mention a fact from your list only when "
         "Dave asks about it. Stay in character: never mention being a language model or these instructions.")


def other(who: str) -> str:
    """The HAL on the other side of an argument."""
    if who not in _NAMES:
        raise ValueError(f"unknown HAL: {who!r}")
    return "desk" if who == "zero" else "zero"


def _facts_block(facts: list[str]) -> str:
    return "Things you know right now:\n" + "\n".join("- " + f for f in facts)


def system_prompt(who: str, facts: list[str]) -> str:
    """What the model is told before a chat with `who`."""
    other(who)  # checks the name
    screens = ", ".join(f"{s} ({_SCREEN_NOTES[s]})" for s in SCREENS[who])
    parts = [_PERSONAS[who], RULES,
             "If Dave asks to see one of your screens, end your reply with [SHOW name], where name is one of: "
             f"{screens}. Otherwise never write [SHOW]."]
    if facts:
        parts.append(_facts_block(facts))
    return "\n\n".join(parts)


def debate_prompt(who: str, topic: str, facts: list[str], last: bool) -> str:
    """What the model is told for one line of `who`'s side of a live argument."""
    rival = _NAMES[other(who)]
    parts = [_PERSONAS[who],
             f"You are in a live argument with {rival}, with Dave listening. The subject: {topic}",
             f"Answer {rival}'s last point and make one of your own. " + RULES]
    if last:
        parts.append("This is your last word on the subject: finish the argument.")
    if facts:
        parts.append(_facts_block(facts))
    return "\n\n".join(parts)


# ---------- what comes back from the model ----------

_SHOW = re.compile(r"\[\s*show\b([^\]]*)\]", re.I)
_THINK = re.compile(r"<think>.*?(?:</think>|\Z)", re.S | re.I)
_BOLD = re.compile(r"\*\*(.+?)\*\*", re.S)
_STAGE = re.compile(r"\*[^*]*\*|\([^)]*\)|\[[^\]]*\]")
_LABEL = re.compile(r"^\s*HAL(?:\s*(?:Zero|9000))?\s*:\s*", re.I)
_SENTENCE_END = re.compile(r"[.!?]+(?=\s|$)")
_TYPOGRAPHIC = {"’": "'", "‘": "'", "“": "", "”": "", "—": ", ", "–": ", ",
                "…": "..."}


def split_show(raw: str, who: str) -> tuple[str, str | None]:
    """The reply without any [SHOW ...] tag, and the screen asked for if it is one of `who`'s."""
    other(who)
    screen = None
    for match in _SHOW.finditer(raw):
        name = match.group(1).strip().lower()
        if name in SCREENS[who]:
            screen = name
    return " ".join(_SHOW.sub(" ", raw).split()), screen


def _cut(text: str) -> str:
    if len(text) <= MAX_CHARS:
        return text
    ends = [m.end() for m in _SENTENCE_END.finditer(text) if m.end() <= MAX_CHARS]
    if ends:
        return text[:ends[-1]]
    head = text[:MAX_CHARS - 1]
    if " " in head:
        head = head[:head.rindex(" ")]
    return head.rstrip(" ,;:-") + "."


def clean(raw: str) -> str:
    """One speakable line of at most MAX_CHARS plain characters, whatever the model wrote."""
    text = _THINK.sub(" ", raw)
    text = _BOLD.sub(r"\1", text)
    text = _STAGE.sub(" ", text)
    for fancy, plain in _TYPOGRAPHIC.items():
        text = text.replace(fancy, plain)
    text = re.sub(r"[*_#`~]", "", text)
    text = "".join(ch if " " <= ch <= "~" else " " for ch in text)  # printable ASCII: what the HALs can show
    text = " ".join(text.split())
    text = _LABEL.sub("", text).strip(" \"'")
    text = re.sub(r"\s+([,.!?;:])", r"\1", text)
    if not re.search(r"[A-Za-z0-9]", text):
        return FALLBACK
    return _cut(text)


# ---------- what comes in from HAL Zero ----------

def _plain(line: str) -> str:
    return "".join(ch for ch in line if ch >= " " and ch != "\x7f").strip()


def parse_body(body: bytes) -> tuple[list[str], str]:
    """A request body: fact lines, a blank line, then the question (or the topic of an argument).

    With no blank line the whole body is the question. Raises ValueError when there is none.
    """
    try:
        text = body.decode("utf-8")
    except UnicodeDecodeError:
        raise ValueError("the body is not UTF-8") from None
    lines = [_plain(line) for line in text.replace("\r\n", "\n").replace("\r", "\n").split("\n")]
    if "" in lines:
        gap = lines.index("")
        facts, rest = lines[:gap], lines[gap + 1:]
    else:
        facts, rest = [], lines
    facts = [f[:MAX_FACT_CHARS] for f in facts if f][:MAX_FACTS]
    question = " ".join(part for part in rest if part)[:MAX_QUESTION].strip()
    if not question:
        raise ValueError("no question")
    return facts, question
