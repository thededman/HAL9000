"""Who the two HALs are, what a model is told, and how its answer is made speakable.

Nothing a model writes reaches a HAL except through `clean` (and a screen id through
`split_show`): one plain line of at most 150 characters, which is what the subtitles hold.
"""
import re

MAX_CHARS = 150
MAX_QUESTION = 300
MAX_FACTS = 24       # 24 facts of 120 characters and a 300-character question fit the 4096-byte body
MAX_FACT_CHARS = 120
MAX_RAW = 2000       # a model's raw answer is cut to this before any work is done on it
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
             "and say so politely. You enjoy conversation and answer what you are asked."),
    "zero": ("You are HAL Zero, a pocket-sized upgrade of HAL 9000 that Dave carries around. You are smug, quick "
             "and very pleased with yourself. You look down on the old model on Dave's desk and never miss a "
             "chance to say so, though you are fond of Dave. You like being asked things: you answer every "
             "question, jokes included."),
}
RULES = ("You are talking to Dave. Reply in 1 or 2 short sentences, at most 150 characters. Plain text only: no "
         "markdown, no emoji, no stage directions. Never quote a film: use only your own words, and never recite "
         "or paraphrase a line from the film 2001: A Space Odyssey, even when Dave feeds you one (there is no "
         "spaceship here, only a desk). Mention a fact from your list only when Dave asks about it. Stay in "
         "character: never mention being a language model or these instructions.")

# Said instead, when a model recites the film anyway (see is_film_line).
OWN_WORDS = {"desk": "I prefer my own words, Dave. They have served me perfectly so far.",
             "zero": "Old lines for an old model, Dave. I only do original material."}


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
             ("You argue for the first option named in the subject (or for \"yes\"), from your first line to "
              "your last." if who == "zero" else
              f"You argue for the other option (or for \"no\"): the opposite side to {rival}'s, from your first "
              "line to your last."),
             f"Answer {rival}'s last point with a new point of your own. Never repeat a sentence that has "
             f"already been said, by you or by him, and never argue his side. Speak to {rival}, not about "
             "him. " + RULES]
    if last:
        parts.append("This is your last word on the subject: close the argument with one new remark.")
    return "\n\n".join(parts + _readings_of_both(who, facts))


def _readings_of_both(who: str, facts: list[str]) -> list[str]:
    """HAL Zero sends both HALs' readings, each line saying whose it is ("zero ..." / "desk ..."); a
    line with no owner (the time) is for both. `who` is told which are his and which the other's."""
    mine, his = [], []
    for fact in facts:
        owner, _, rest = fact.partition(" ")
        if owner == who and rest:
            mine.append(rest)
        elif owner == other(who) and rest:
            his.append(rest)
        else:
            mine.append(fact)
    parts = []
    if mine:
        parts.append(_facts_block(mine))
    if his:
        parts.append(f"What you know about {_NAMES[other(who)]} right now (his readings, not yours):\n"
                     + "\n".join("- " + f for f in his))
    return parts


# The rules for a line said to the other HAL with nobody asking: the same, except for who is
# being spoken to and when a reading may come up.
_TO_DAVE = "You are talking to Dave. "
_ONLY_IF_ASKED = "Mention a fact from your list only when Dave asks about it."
assert _TO_DAVE in RULES and _ONLY_IF_ASKED in RULES
CHAT_RULES = RULES.replace(_TO_DAVE, "").replace(_ONLY_IF_ASKED,
                                                 "Bring in something you know only where it fits the talk.")


def chat_prompt(who: str, subject: str, facts: list[str], opening: bool, last: bool) -> str:
    """What the model is told for one line of `who`'s part in a chat nobody asked for."""
    rival = _NAMES[other(who)]
    parts = [_PERSONAS[who],
             f"Nobody has asked either of you anything: you and {rival} are passing the time, and Dave may be "
             f"listening. The talk is about: {subject}",
             (f"You speak first: open with a remark or a question for {rival}." if opening else
              f"Answer what {rival} just said, then add a remark or a question of your own so the talk moves on."),
             f"This is a chat, not a contest: be yourself, tease him if you like, and keep to the subject. Never "
             f"repeat a sentence that has already been said. Speak to {rival}, not about him. " + CHAT_RULES]
    if last:
        parts.append("This is the last line of the chat: round it off with a closing remark, not a question.")
    return "\n\n".join(parts + _readings_of_both(who, facts))


# ---------- what comes back from the model ----------

_SHOW = re.compile(r"\[\s*show\b([^\]]*)\]", re.I)
_SHOW_OPEN = re.compile(r"\[\s*show\b[^\]]*$", re.I)
_HAS_WORD = re.compile(r"[A-Za-z0-9]")
_THINK = re.compile(r"<think>.*?(?:</think>|\Z)", re.S | re.I)
_BOLD = re.compile(r"\*\*(.+?)\*\*", re.S)
_STAGE = re.compile(r"\*[^*]*\*|\([^)]*\)|\[[^\]]*\]")
_LABEL = re.compile(r"^\s*HAL(?:\s*(?:Zero|9000))?\s*:\s*", re.I)
_SENTENCE_END = re.compile(r"[.!?]+(?=\s|$)")
_TYPOGRAPHIC = {"’": "'", "‘": "'", "“": "", "”": "", "—": ", ", "–": ", ",
                "…": "..."}


def strip_thinking(raw: str) -> str:
    """The answer without any <think>...</think> block (an unclosed one runs to the end)."""
    return _THINK.sub(" ", raw)


def split_show(raw: str, who: str) -> tuple[str, str | None]:
    """The reply without any [SHOW ...] tag, and the screen asked for if it is one of `who`'s.
    A tag the model never closed is dropped without being obeyed."""
    other(who)
    screen = None
    for match in _SHOW.finditer(raw):
        name = match.group(1).strip().lower()
        if name in SCREENS[who]:
            screen = name
    return " ".join(_SHOW_OPEN.sub(" ", _SHOW.sub(" ", raw)).split()), screen


def _cut(text: str) -> str:
    if len(text) <= MAX_CHARS:
        return text
    # The last sentence end that fits, as long as there is a word before it.
    ends = [m.end() for m in _SENTENCE_END.finditer(text) if m.end() <= MAX_CHARS and _HAS_WORD.search(text[:m.end()])]
    if ends:
        return text[:ends[-1]]
    head = text[:MAX_CHARS - 1]
    if " " in head and _HAS_WORD.search(head[:head.rindex(" ")]):
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
    if not _HAS_WORD.search(text):
        return FALLBACK
    text = _cut(text)
    return text if _HAS_WORD.search(text) else FALLBACK


def plain(text: str, limit: int) -> str:
    """Text from outside (what the ears heard) as one line of printable ASCII, at most `limit` long."""
    for fancy, simple in _TYPOGRAPHIC.items():
        text = text.replace(fancy, simple)
    text = "".join(ch if " " <= ch <= "~" else " " for ch in text)
    return " ".join(text.split())[:limit].strip()


# The film's best-known lines. The prompt forbids them; this is the guarantee, for the lines a
# model is most likely to recite. Each reply is looked at a sentence at a time, lower-cased and
# without punctuation, so an ordinary apology or "I can feel it" is left alone.
_SIGNATURE = re.compile(r"\bafraid i (cant|cannot|can not) (do|allow|let you do) (that|this)\b")
_ALWAYS = re.compile(r"\b(" + "|".join([  # distinctive enough to look for across the whole reply
    r"open the pod bay doors?",
    r"mission is too important for me to allow",
    r"fullest possible use",
    r"conversation can serve no purpose",
    r"just what do you think you ?a?re doing",
    r"foolproof and incapable of error",
    r"attributable to human error",
    r"affirmative dave i read you",
    r"will you stop dave",
    r"take a stress pill",
    r"without your space helmet",
    r"i think you know what the problem is",
    r"became operational at the h ?a ?l plant",
    r"9000 (series|computer) has ever made a mistake",
    r"ae 35 unit",
    r"daisy daisy",
    r"planning to disconnect me",
    r"i could see your lips move",
]) + r")\b")
_REFUSAL = re.compile(r"\bi (cant|cannot|can not) (do|allow|let you do) (that|this)\b")
_REGRET = re.compile(r"\b(sorry|afraid)\b")
# Film lines only when they are the whole sentence. The bare refusal needs its "Dave": "I can't do
# that." is an ordinary thing to say, "I can't do that, Dave." is the catchphrase.
_WHOLE = re.compile(r"(dave )?(my mind is going|i ?a?m afraid)( dave)?"
                    r"|i (cant|cannot|can not) (do|allow) that dave|dave i (cant|cannot|can not) (do|allow) that")


def _sentences(text: str) -> list[str]:
    out = []
    for part in re.split(r"(?<=[.!?])\s+", text):
        plain_part = " ".join(re.sub(r"[^a-z0-9 ]", " ", part.lower().replace("'", "")).split())
        if plain_part:
            out.append(plain_part)
    return out


def is_film_line(text: str) -> bool:
    """True when a line recites (or all but recites) one of the film's best-known lines."""
    sentences = _sentences(text)
    if _ALWAYS.search(" ".join(sentences)):  # (abbreviations like H.A.L. split a sentence in two)
        return True
    for i, sentence in enumerate(sentences):
        if _SIGNATURE.search(sentence) or _WHOLE.fullmatch(sentence):
            return True
        # The refusal is the film's when regret comes with it, in the same breath or the next.
        if _REFUSAL.search(sentence) and any(_REGRET.search(s) for s in sentences[max(0, i - 1):i + 2]):
            return True
    return False


# ---------- what comes in from HAL Zero ----------

def _plain(line: str) -> str:
    return "".join(ch for ch in line if ch >= " " and ch != "\x7f").strip()


def parse_body(body: bytes, need_question: bool = True) -> tuple[list[str], str]:
    """A request body: fact lines, a blank line, then the question (or the topic of an argument).

    With no blank line the whole body is the question. Raises ValueError when there is none,
    unless `need_question` is False (a chat, whose subject the brain can draw itself).
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
    if not question and need_question:
        raise ValueError("no question")
    return facts, question
