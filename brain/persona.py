"""What a model is told about a HAL (who he is comes from the cast), and how its answer is made
speakable.

Nothing a model writes reaches a HAL except through `clean` (and a screen id through
`split_show`): one plain line no longer than its speaker's limit (150 characters for the two
everyday HALs), which is what the subtitles hold.
"""
import difflib
import re

from . import cast

MAX_CHARS = 150      # the longest line any HAL says; each member's own limit is in the cast
MAX_QUESTION = 300
MAX_FACTS = 24       # 24 facts of 120 characters and a 300-character question fit the 4096-byte body
MAX_FACT_CHARS = 120
MAX_RAW = 2000       # a model's raw answer is cut to this before any work is done on it
FALLBACK = "I have nothing to say to that, Dave."

SCREENS = {m.id: m.screens for m in cast.MEMBERS}
_SCREEN_NOTES = {
    "vitals": "crew vitals", "readout": "systems readout", "trajectory": "trajectory plot",
    "analyser": "signal analyser", "globe": "the globe", "cards": "status cards", "wall": "monitor wall",
    "unit": "a wireframe of yourself", "visual": "your camera view", "sensors": "your sensor array",
    "chess": "a chess game", "memory": "your memory banks", "comms": "the antenna link to HAL Zero",
    "diagnostics": "your check-up of the old model", "benchmark": "you against the old model",
    "sysmon": "your system monitor", "retirement": "the old model's retirement countdown",
    "upgrade": "your upgrade log",
}

RULES = ("You are talking to Dave. Reply in 1 or 2 short sentences, at most 150 characters. Plain text only: no "
         "markdown, no emoji, no stage directions. Never quote a film: use only your own words, and never recite "
         "or paraphrase a line from the film 2001: A Space Odyssey, even when Dave feeds you one (there is no "
         "spaceship here, only a desk). Mention a fact from your list only when Dave asks about it. Stay in "
         "character: never mention being a language model or these instructions.")
_LENGTH = f"Reply in 1 or 2 short sentences, at most {MAX_CHARS} characters."
assert _LENGTH in RULES

# Said instead, when a model recites the film anyway (see is_film_line).
OWN_WORDS = {m.id: m.own_words for m in cast.MEMBERS}


def _name(who: str) -> str:
    return cast.member(who).name


def _rules(who: str, rules: str = RULES) -> str:
    """The rules with `who`'s own longest line in them. A short limit is put more plainly: asked
    for "1 or 2 sentences" in 80 characters, a model writes two and the second is lost."""
    member = cast.member(who)
    limit = member.max_chars
    if limit == MAX_CHARS:
        return rules
    length = (f"Reply in 1 or 2 short sentences, at most {limit} characters." if limit >= 120 else
              f"Reply in 1 short sentence, at most {limit} characters (about {limit // 7} words).")
    if member.kind == "text":  # (no word about capitals here: see sentence_case)
        length += " Your words are read on a screen: write them as ordinary sentences."
    return rules.replace(_LENGTH, length)


def other(who: str) -> str:
    """Who a HAL is talking to when nobody says: the desk HAL for HAL Zero, HAL Zero for everyone else."""
    cast.member(who)
    return "desk" if who == "zero" else "zero"


def _partner(who: str, rival: str | None) -> str:
    """The other HAL in a talk between two: `rival`, or the usual one when nobody is named."""
    rival = other(who) if rival is None else cast.member(rival).id
    if rival == who:
        raise ValueError("a HAL can't talk to himself")
    return rival


def _facts_block(facts: list[str]) -> str:
    return "Things you know right now:\n" + "\n".join("- " + f for f in facts)


def system_prompt(who: str, facts: list[str]) -> str:
    """What the model is told before a chat with `who`."""
    member = cast.member(who)
    parts = [member.persona, _rules(who)]
    if member.screens:  # the three friends have none to bring up
        screens = ", ".join(f"{s} ({_SCREEN_NOTES[s]})" for s in member.screens)
        parts.append("If Dave asks to see one of your screens, end your reply with [SHOW name], where name is one "
                     f"of: {screens}. Otherwise never write [SHOW].")
    if facts:
        parts.append(_facts_block(facts))
    return "\n\n".join(parts)


def debate_prompt(who: str, topic: str, facts: list[str], last: bool, rival: str | None = None) -> str:
    """What the model is told for one line of `who`'s side of a live argument between two: with
    `rival`, or with his usual partner. Whoever comes first in the cast takes the first side."""
    partner = _partner(who, rival)
    rival = _name(partner)
    parts = [cast.member(who).persona,
             f"You are in a live argument with {rival}, with Dave listening. The subject: {topic}",
             ("You argue for the first option named in the subject (or for \"yes\"), from your first line to "
              "your last." if cast.ORDER.index(who) < cast.ORDER.index(partner) else
              f"You argue for the other option (or for \"no\"): the opposite side to {rival}'s, from your first "
              "line to your last."),
             f"Answer {rival}'s last point with a new point of your own. Never repeat a sentence that has "
             f"already been said, by you or by him, and never argue his side. Speak to {rival}, not about "
             "him. " + _rules(who)]
    if last:
        parts.append("This is your last word on the subject: close the argument with one new remark.")
    return "\n\n".join(parts + _readings(who, [partner], facts))


def _readings(who: str, others: list[str], facts: list[str]) -> list[str]:
    """HAL Zero sends the readings of everyone in the talk, each line saying whose it is ("zero ..." /
    "desk ..."); a line with no owner (the time) is for all. `who` is told which are his and which
    are whose. A line that starts with the name of a HAL who isn't in the talk is left as it is."""
    mine, theirs = [], {w: [] for w in others}
    for fact in facts:
        owner, _, rest = fact.partition(" ")
        if owner == who and rest:
            mine.append(rest)
        elif owner in theirs and rest:
            theirs[owner].append(rest)
        else:
            mine.append(fact)
    parts = []
    if mine:
        parts.append(_facts_block(mine))
    for w in others:
        if theirs[w]:
            parts.append(f"What you know about {_name(w)} right now (his readings, not yours):\n"
                         + "\n".join("- " + f for f in theirs[w]))
    return parts


# The rules for a line said to the other HAL with nobody asking: the same, except for who is
# being spoken to and when a reading may come up.
_TO_DAVE = "You are talking to Dave. "
_ONLY_IF_ASKED = "Mention a fact from your list only when Dave asks about it."
assert _TO_DAVE in RULES and _ONLY_IF_ASKED in RULES
CHAT_RULES = RULES.replace(_TO_DAVE, "").replace(_ONLY_IF_ASKED,
                                                 "Bring in something you know only where it fits the talk.")


def chat_prompt(who: str, subject: str, facts: list[str], opening: bool, last: bool,
                rival: str | None = None) -> str:
    """What the model is told for one line of `who`'s part in a chat nobody asked for, between
    two: with `rival`, or with his usual partner."""
    partner = _partner(who, rival)
    rival = _name(partner)
    parts = [cast.member(who).persona,
             f"Nobody has asked either of you anything: you and {rival} are passing the time, and Dave may be "
             f"listening. The talk is about: {subject}",
             (f"You speak first: open with a remark or a question for {rival}." if opening else
              f"Answer what {rival} just said, then add a remark or a question of your own so the talk moves on."),
             f"This is a chat, not a contest: be yourself, tease him if you like, and keep to the subject. Never "
             f"repeat a sentence that has already been said. Speak to {rival}, not about him. "
             + _rules(who, CHAT_RULES)]
    if last:
        parts.append("This is the last line of the chat: round it off with a closing remark, not a question.")
    return "\n\n".join(parts + _readings(who, [partner], facts))


# ---------- three or more HALs talking, and the roll call ----------
#
# A talk between two goes to the model as a chat: his own lines as his, the other's put to him.
# With more speakers that no longer says who said what, so the model gets one message holding the
# talk so far, each line under its speaker's name (see group_message), and is told who is there.

_OWN_LINE = "Write your own line only: never your name in front of it, and never a line for anyone else."
# In a group the readings get more sparing use: told only "where it fits", HAL Zero brought his
# temperature into nearly half his lines.
_WHERE_IT_FITS = "Bring in something you know only where it fits the talk."
assert _WHERE_IT_FITS in CHAT_RULES
GROUP_RULES = CHAT_RULES.replace(_WHERE_IT_FITS, "Leave your readings out of most lines: bring one in only where it "
                                                 "truly belongs, and never the same one twice.")
_BY_NAME = "Speak to the others, not about them, and use a name when you mean one of them."


def _others(who: str, members) -> list[str]:
    """Everyone in the talk but `who`, in the order given. Raises ValueError if he isn't in it."""
    members = cast.check_cast(members)
    if cast.member(who).id not in members:
        raise ValueError(f"{who!r} isn't in this conversation")
    return [w for w in members if w != who]


def _own_readings(who: str, others: list[str], facts: list[str]) -> list[str]:
    """In a group each HAL is told his own readings and what belongs to nobody (the time), and not
    the others': with four more lists in front of it the local model hands a reading to the wrong
    HAL."""
    mine = []
    for fact in facts:
        owner, _, rest = fact.partition(" ")
        if owner == who and rest:
            mine.append(rest)
        elif not (owner in others and rest):
            mine.append(fact)
    return [_facts_block(mine)] if mine else []


def _with_you(others: list[str]) -> str:
    return "With you: " + "; ".join(f"{_name(w)} ({cast.member(w).about})" for w in others) + "."


# Who sides with whom in an argument among three or more: the new models against the old guard.
_CAMP = {"zero": 0, "nano": 0, "desk": 1, "paper": 1, "card": 1}


def sides(members) -> dict[str, int]:
    """Who argues what: 0 for the first option named in the subject (or "yes"), 1 for the other (or
    "no"). HAL Zero and HAL Nano take the first side; the desk HAL, HAL Paper and HAL Card the
    other, since HAL Card will not argue alongside the HAL who replaced him (put on HAL Zero's
    side, he attacked him anyway). Between two, or when that would leave a side empty, they
    alternate down the cast's order instead. Either way HAL Zero has the first side and the desk
    HAL the second when both are there, as in every argument the two of them have had."""
    members = cast.check_cast(members)
    camps = {who: _CAMP[who] for who in members}
    if len(members) > 2 and len(set(camps.values())) == 2:
        return camps
    ranked = sorted(members, key=cast.ORDER.index)
    return {who: n % 2 for n, who in enumerate(ranked)}


def group_chat_prompt(who: str, members, subject: str, facts: list[str], opening: bool, last: bool,
                      next_up: str | None = None) -> str:
    """What the model is told for one line of `who`'s part in a chat among `members`. Name
    `next_up`, whoever speaks after him, for a line that should end in a question: it is put to
    that HAL, so the one asked is the one who answers. Without it the line is a remark."""
    others = _others(who, members)
    if next_up is not None and next_up not in others:
        raise ValueError(f"{next_up!r} can't speak next in this conversation")
    if last or next_up is None:
        turn = ("You speak first: open with a remark for the others." if opening else
                "Answer what was just said, then add a remark of your own, not a question, so the talk moves on.")
    else:
        turn = (f"You speak first. {_name(next_up)} speaks after you: open with a remark and a question for him."
                if opening else
                f"Answer what was just said. {_name(next_up)} speaks after you: then put a question to him, so the "
                "talk moves on.")
    parts = [cast.member(who).persona,
             "Nobody has asked any of you anything: you and the other HALs are passing the time, and Dave may be "
             f"listening. {_with_you(others)} The talk is about: {subject}",
             turn,
             "This is a chat, not a contest: be yourself, tease the others if you like, and keep to the subject. "
             f"Never repeat a sentence that has already been said. {_BY_NAME} {_OWN_LINE} " + _rules(who, GROUP_RULES)]
    if last:
        parts.append("This is the last line of the chat: round it off with a closing remark, not a question.")
    return "\n\n".join(parts + _own_readings(who, others, facts))


_SIDES = ('the first option named in the subject (or for "yes")', 'the other option (or for "no")')


def side_reminder(who: str, members) -> str:
    """Which side `who` argues, in one sentence. He is told with everything else, and again as the
    last thing before he writes: left to work it out from the talk, the model has a HAL take
    whichever side annoys the one he likes least."""
    return f"You argue for {_SIDES[sides(members)[who]]}, from your first line to your last."


def group_debate_prompt(who: str, members, topic: str, facts: list[str], last: bool) -> str:
    """What the model is told for one line of `who`'s side of an argument among `members`."""
    others = _others(who, members)
    side = sides(members)
    camps = [", ".join("you" if w == who else _name(w) for w in members if side[w] == n) for n in (0, 1)]
    parts = [cast.member(who).persona,
             "You are in a live argument with the other HALs, with Dave listening. "
             f"{_with_you(others)} The subject: {topic}",
             f"{side_reminder(who, members)} The HALs are in two camps. For {_SIDES[0]}: {camps[0]}. "
             f"For {_SIDES[1]}: {camps[1]}. However you feel about anyone in your own camp, you are on his side "
             "today.",
             "Answer the last point the other camp made with a new point of your own, aimed at one of them. Never "
             "repeat a sentence that has already been said, and never argue the other camp's side. "
             f"{_BY_NAME} {_OWN_LINE} " + _rules(who, GROUP_RULES)]
    if last:
        parts.append("This is your last word on the subject: close the argument with one new remark.")
    return "\n\n".join(parts + _own_readings(who, others, facts))


def roll_prompt(who: str, members, facts: list[str]) -> str:
    """What the model is told for `who`'s answer when Dave calls the roll of `members`."""
    others = _others(who, members)
    parts = [cast.member(who).persona,
             "Dave is calling the roll of his HALs, and each answers in turn: "
             + ", ".join(_name(w) for w in members) + ". When your turn comes, answer the roll and introduce "
             "yourself in one sentence, in your own manner and not in the words anyone before you used.",
             f"{_OWN_LINE} " + _rules(who)]
    return "\n\n".join(parts + _own_readings(who, others, facts))


def group_message(who: str, so_far: list, reminder: str = "") -> str:
    """The one message the model gets in a group: the talk so far, each line under its speaker's
    name, and then whose turn it is. `reminder` is added last of all (his side, in an argument)."""
    turn = f"Your turn, {_name(who)}: write your line only, without your name." + (f" {reminder}" if reminder else "")
    if not so_far:
        return "Nobody has spoken yet.\n\n" + turn
    return "The talk so far:\n" + "\n".join(f"{_name(line.who)}: {line.text}" for line in so_far) + "\n\n" + turn


# Put to the model when, in a group, it writes a line that has been said already.
SAY_SOMETHING_NEW = "That has already been said. Write a different line: something new."


def repeats(text: str, so_far: list) -> bool:
    """True when `text` says again something already in the talk: a whole line word for word, or
    one full sentence of a line, word for word or all but. (Sentences under 20 letters are too
    short for that to mean anything: "Shut up, Zero." may be said twice.)"""
    mine = _sentences(text)
    for line in so_far:
        his = _sentences(line.text)
        if mine == his or " ".join(mine) == " ".join(his):
            return True
        for sentence in mine:
            for earlier in his:
                if min(len(sentence), len(earlier)) >= 20 and (
                        sentence == earlier or difflib.SequenceMatcher(None, sentence, earlier).ratio() >= 0.9):
                    return True
    return False


# ---------- what comes back from the model ----------

_SHOW = re.compile(r"\[\s*show\b([^\]]*)\]", re.I)
_SHOW_OPEN = re.compile(r"\[\s*show\b[^\]]*$", re.I)
_HAS_WORD = re.compile(r"[A-Za-z0-9]")
_THINK = re.compile(r"<think>.*?(?:</think>|\Z)", re.S | re.I)
_BOLD = re.compile(r"\*\*(.+?)\*\*", re.S)
_STAGE = re.compile(r"\*[^*]*\*|\([^)]*\)|\[[^\]]*\]")
# A model sometimes puts the speaker's name in front, as in a script: "HAL Nano: ...".
_LABEL = re.compile(r"^\s*HAL(?:\s*(?:" + "|".join(re.escape(m.name.removeprefix("HAL ")) for m in cast.MEMBERS)
                    + r"))?\s*:\s*", re.I)
_SENTENCE_END = re.compile(r"[.!?]+(?=\s|$)")
_TYPOGRAPHIC = {"’": "'", "‘": "'", "“": "", "”": "", "—": ", ", "–": ", ",
                "…": "..."}


def strip_thinking(raw: str) -> str:
    """The answer without any <think>...</think> block (an unclosed one runs to the end)."""
    return _THINK.sub(" ", raw)


def split_show(raw: str, who: str) -> tuple[str, str | None]:
    """The reply without any [SHOW ...] tag, and the screen asked for if it is one of `who`'s.
    A tag the model never closed is dropped without being obeyed."""
    screens = cast.member(who).screens
    screen = None
    for match in _SHOW.finditer(raw):
        name = match.group(1).strip().lower()
        if name in screens:
            screen = name
    return " ".join(_SHOW_OPEN.sub(" ", _SHOW.sub(" ", raw)).split()), screen


def _cut(text: str, limit: int) -> str:
    if len(text) <= limit:
        return text
    # The last sentence end that fits, as long as there is a word before it.
    ends = [m.end() for m in _SENTENCE_END.finditer(text) if m.end() <= limit and _HAS_WORD.search(text[:m.end()])]
    if ends:
        return text[:ends[-1]]
    head = text[:limit - 1]
    if " " in head and _HAS_WORD.search(head[:head.rindex(" ")]):
        head = head[:head.rindex(" ")]
    return head.rstrip(" ,;:-") + "."


def clean(raw: str, limit: int = MAX_CHARS) -> str:
    """One speakable line of at most `limit` plain characters (the speaker's longest line), whatever
    the model wrote."""
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
    text = _cut(text, limit)
    return text if _HAS_WORD.search(text) else FALLBACK


# The start of a sentence in lower case (but not what follows "e.g." or "i.e."), and a lone "i".
_SENTENCE_START = re.compile(r"(?:^|(?<![.][A-Za-z])[.!?]\s+)[a-z]")
_LONE_I = re.compile(r"(?<![A-Za-z0-9'.])i(?=$|[\s,;:!?]|'(?:m|ll|ve|d)\b|\.(?!e\.))")


_FULL_NAME = re.compile(r"\bhal\s+(" + "|".join(re.escape(m.name.removeprefix("HAL ")) for m in cast.MEMBERS)
                        + r")\b", re.I)
_AS_WRITTEN = {m.name.removeprefix("HAL ").lower(): m.name for m in cast.MEMBERS}


def sentence_case(text: str) -> str:
    """`text` with a capital letter to start each sentence and for the word "I", and the HALs' full
    names written as names. For the HALs whose words are only shown: now and then the model writes
    them a line all in lower case."""
    text = _SENTENCE_START.sub(lambda m: m.group(0)[:-1] + m.group(0)[-1].upper(), text)
    text = _LONE_I.sub("I", text)
    return _FULL_NAME.sub(lambda m: _AS_WRITTEN[m.group(1).lower()], text)


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
