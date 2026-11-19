"""Looking something up: for a question that asks for a fact the model may not have (who holds an
office now, a figure, a date), a short reference text fetched from the internet and handed to the
model to answer from, still in the HAL's own voice.

Off unless HAL_BRAIN_LOOKUP in brain/.env says otherwise (tools/brain.sh lookup on). With it on,
the words of such questions leave this computer: they go to Wikipedia, with nothing that says who
is asking. Talk with the HALs, anything about Dave or about themselves, recordings and the HALs'
readings never do.

    tools/brain.sh lookup on | off | status
"""
import json
import re
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

from .config import ENV_PATH, read_env, save_setting

MAX_QUERY_CHARS = 200
MAX_REFERENCE_CHARS = 2400  # of the best article's opening; a second article gets half of that
SETTING = "HAL_BRAIN_LOOKUP"

# "Hey HAL, do you know ..." and the like, at the front of a question.
_LEAD_IN = re.compile(r"^\s*(?:(?:hey|hi|ok|okay|so|and|well|please|now)\b[\s,]*|hal\b[\s,]*|"
                      r"(?:can|could|would) you (?:please )?(?:tell me|say)\b[\s,]*|do you know\b[\s,]*|tell me\b[\s,]*)+",
                      re.I)
# Dave telling him to look.
_TOLD = re.compile(r"\b(?:look (?:it |that |this )?up|search(?: (?:the )?(?:internet|web))?(?: for)?|google|"
                   r"(?:check|find)(?: it)? (?:online|on the internet)|on the internet|online)\b[\s:,]*", re.I)
# A question that asks for a fact.
_ASKS = re.compile(r"^(?:who|whom|whose|who's|what|what's|when|where|where's|which|"
                   r"how (?:many|much|old|tall|far|long|big|high|heavy|fast|deep))\b", re.I)
# ... unless it is about him, about Dave, or something he already knows.
_AT_HOME = re.compile(r"\b(?:you|your|yours|yourself|you're|i|i'm|me|my|mine|we|us|our|dave)\b", re.I)
_HE_KNOWS = re.compile(r"\bwhat(?:'s| is) the (?:time|date)\b|\bwhat (?:time|day|date) is it\b", re.I)


# "US" the country is not "us" the people in the room.
_THE_STATES = re.compile(r"\bU\.S\.A\.?|\bU\.S\.|\bUSA?\b|(?<=\bthe )(?:us|usa)\b(?=\s+\w)", re.I)


def _plain(text: str) -> str:
    """One line of printable ASCII."""
    return " ".join("".join(ch if " " <= ch <= "~" else " " for ch in text).split())


def _spelt_out(text: str) -> str:
    """The question with "US" written as "United States": so that it isn't read as "us", and so
    that it finds the right page. ("Tell us a joke" stays as it is: only capitals, dots, or "the us ...".)"""
    def states(match):
        word = match.group(0)
        return "United States" if word.isupper() or "." in word or match.start() >= 4 and text[match.start() - 4:match.start()].lower() == "the " else word
    return _THE_STATES.sub(states, text)


def wants(question: str) -> bool:
    """Is this a question to look up? Only when Dave says to look, or it asks for a fact about
    the world. Talk with a HAL stays at home."""
    text = _spelt_out(_plain(question))
    if not text:
        return False
    if _TOLD.search(text):
        return True
    asked = _LEAD_IN.sub("", text)
    return bool(_ASKS.match(asked)) and not _AT_HOME.search(asked) and not _HE_KNOWS.search(asked)


# "Who is", "what was", "how many ... does": the asking part of a question, at its front.
_ASKING = re.compile(r"^(?:(?:who|whom|whose|what|when|where|which)(?:'s|\s+(?:is|are|was|were|did|does|do|has|have))?|"
                     r"how (?:many|much|old|tall|far|long|big|high|heavy|fast|deep)(?:\s+(?:is|are|was|were))?)\s+", re.I)


def query(question: str) -> str:
    """What is searched for: the subject of the question, without "Hey HAL", "look up", the
    asking words and the question mark. ("Who is the US president?" finds the article on the
    president when it is searched as "the United States president", and a list of presidents
    by age when the "who is" is left in.)"""
    text = _TOLD.sub(" ", _spelt_out(_plain(question)))
    text = _LEAD_IN.sub("", text)
    text = _ASKING.sub("", _plain(text))
    return _plain(text).strip(" ?.!:,")[:MAX_QUERY_CHARS].strip()


class _NoRedirects(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *args, **kwargs):  # the answer comes from where it was asked, or not at all
        return None


def _fetch(url: str, headers: dict, timeout_s: float, max_bytes: int) -> bytes:
    opener = urllib.request.build_opener(_NoRedirects)
    with opener.open(urllib.request.Request(url, headers=headers), timeout=timeout_s) as reply:
        return reply.read(max_bytes + 1)[:max_bytes]


def _whole_sentences(text: str, limit: int, from_end: bool = False) -> str:
    """At most `limit` characters of `text`, from its start (or its end), cut at a sentence."""
    if len(text) <= limit:
        return text
    if from_end:
        cut = text[-limit:]
        start = cut.find(". ")
        return cut[start + 2:] if 0 <= start < limit // 2 else cut
    cut = text[:limit]
    end = max(cut.rfind(". "), cut.rfind("? "), cut.rfind("! "))
    return cut[:end + 1] if end > limit // 2 else cut


def _cut(text: str, limit: int) -> str:
    """`text` in at most `limit` characters. A long article opening keeps its beginning and its
    end: what a thing is comes first, and who holds an office now is usually the last sentence."""
    if len(text) <= limit:
        return text
    return _whole_sentences(text, limit * 6 // 10) + " ... " + _whole_sentences(text, limit * 4 // 10 - 5, from_end=True)


class Wikipedia:
    """The opening section of the one or two Wikipedia articles that best fit the question.

    Wikipedia asks programs to go easy on it, so a question costs two requests at most: one that
    finds the best article and returns its whole opening, and one for the opening of the next
    best (asked for together, openings come back cut to a paragraph). Told to slow down, it
    waits a moment and tries once more; and what it found is remembered for ten minutes."""
    name = "Wikipedia"
    URL = "https://en.wikipedia.org/w/api.php"
    # Says what kind of program is asking, and nothing about whose it is.
    HEADERS = {"User-Agent": "desk-companion-lookup/1.0 (a home hobby device answering a spoken question)",
               "Accept": "application/json"}
    BUDGET_S = 5.0     # all of it: a HAL is waiting to answer
    REMEMBER_S = 600   # the same question again within this gets the same answer, and no request
    REMEMBERED = 32

    def __init__(self, fetch=_fetch, timeout_s: float = 4.0, clock=time.monotonic, sleep=time.sleep):
        self._fetch, self._timeout, self._clock, self._sleep = fetch, timeout_s, clock, sleep
        self._found: dict[str, tuple[float, list[str]]] = {}

    def _ask(self, patient: bool, **params) -> dict:
        url = self.URL + "?" + urllib.parse.urlencode({"action": "query", "format": "json", **params})
        try:
            raw = self._fetch(url, dict(self.HEADERS), self._timeout, 400 * 1024)
        except urllib.error.HTTPError as e:
            if e.code != 429 or not patient:
                raise
            try:  # "too many requests": it says how long to wait
                wait = float(e.headers.get("Retry-After", "1"))
            except (TypeError, ValueError):
                wait = 1.0
            self._sleep(min(max(wait, 0.5), 1.5))
            raw = self._fetch(url, dict(self.HEADERS), self._timeout, 400 * 1024)
        return json.loads(raw.decode("utf-8", "replace"))["query"]

    def find(self, question: str) -> list[str]:
        """Reference text for the question, best match first; [] when nothing was found or
        anything at all went wrong (the HAL then answers from what the model knows)."""
        asked = query(question)
        if not asked:
            return []
        began = self._clock()
        kept = self._found.get(asked.lower())
        if kept and began - kept[0] < self.REMEMBER_S:
            return list(kept[1])
        try:  # the two best titles, and the best article's whole opening, in one request
            got = self._ask(True, list="search", srsearch=asked, srlimit=2, srprop="", generator="search", gsrsearch=asked,
                            gsrlimit=1, prop="extracts", exintro=1, explaintext=1)
            titles = [_plain(str(hit["title"])) for hit in got["search"]]
            best = next(iter(got["pages"].values())) if titles else {}
            best_title, text = _plain(str(best.get("title", ""))), _plain(str(best.get("extract", "")))
        except Exception:  # no network, a slow answer, a page that isn't JSON, a shape that isn't expected
            return []
        found = [f"{best_title}: " + _cut(text, MAX_REFERENCE_CHARS)] if best_title and text else []
        second = next((t for t in titles if t and t != best_title), "")
        if second and self._clock() - began <= self.BUDGET_S - 1.0:
            try:
                pages = self._ask(False, prop="extracts", exintro=1, explaintext=1, redirects=1, titles=second)["pages"]
                more = _plain(str(next(iter(pages.values()))["extract"]))
                if more:
                    found.append(f"{second}: " + _cut(more, MAX_REFERENCE_CHARS // 2))
            except Exception:
                pass  # the best article is enough
        if found:
            if len(self._found) >= self.REMEMBERED:
                self._found.pop(next(iter(self._found)))
            self._found[asked.lower()] = (began, list(found))
        return found


def from_env(env: dict):
    """The lookup the settings ask for, or None (off). Raises ValueError for one that doesn't exist."""
    wanted = env.get(SETTING, "").strip().lower()
    if wanted in ("", "off", "no", "0"):
        return None
    if wanted in ("wikipedia", "on"):
        return Wikipedia()
    raise ValueError(f"{SETTING} in brain/.env is '{wanted}': it can be wikipedia or off")


def main(argv: list[str], env_path=ENV_PATH, say=print) -> int:
    """tools/brain.sh lookup on|off|status"""
    what = argv[0] if len(argv) == 1 else ""
    if what == "status":
        try:
            on = from_env(read_env(env_path)) is not None
        except ValueError as e:
            say(str(e))
            return 1
        say("Lookups are on: questions that ask for a fact are looked up on Wikipedia." if on else
            "Lookups are off: the HALs answer from what the model knows.")
        return 0
    if what not in ("on", "off"):
        say("Usage: tools/brain.sh lookup on | off | status")
        return 2
    save_setting(env_path, SETTING, "wikipedia" if what == "on" else "off")
    say("Lookups are on. A question that asks for a fact (\"Who is the president?\", \"look up ...\") is now sent to "
        "Wikipedia, with nothing that says who is asking; talk with the HALs stays on this computer."
        if what == "on" else "Lookups are off: nothing a HAL is asked leaves this computer for a lookup.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
