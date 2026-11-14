"""The cast: five HALs in one table, and what each is told when Dave talks to him alone."""
import hashlib
import unittest

from brain import cast, persona

NEWCOMERS = ("nano", "paper", "card")

# Fingerprints of everything the two everyday HALs were told in v0.6.0 (the first 16 hex digits of
# each text's SHA-256). The local model's behaviour was tuned on these exact words: a change here
# is a change in how HAL Zero and the desk HAL behave, and wants listening to before it ships.
READINGS = ["time: 20:24", "zero chip temperature: 54 C", "zero free memory: 131 MB",
            "desk battery: 53 % and charging", "desk firmware: 0.6.0"]
V060 = {
    "rules": "32e632d4f299b7e1",
    "chat rules": "1d415d5714ca9003",
    "fallback": "2b207a7d7e8f60a4",
    "own words zero": "a9eba561d3db54f4",
    "ask zero 0": "1a579dba7df9b432",
    "argue zero 0 False": "9838bb4af25d0d9a",
    "chat zero 0 False False": "91591ed1faa74c33",
    "chat zero 0 True False": "516b9099afceb841",
    "argue zero 0 True": "e4e0bb9ea5b538b1",
    "chat zero 0 False True": "59fcbe74253c3058",
    "chat zero 0 True True": "196708efe9f04688",
    "ask zero 5": "ea5ce3c24f4842fe",
    "argue zero 5 False": "0b06649aea1b6588",
    "chat zero 5 False False": "494994ac4143325d",
    "chat zero 5 True False": "61f83d7e964e8cdb",
    "argue zero 5 True": "03713fb8e9e5ea14",
    "chat zero 5 False True": "2794dbced7151cfb",
    "chat zero 5 True True": "21a3221da139e2c1",
    "own words desk": "4a58f4715208d954",
    "ask desk 0": "b689bdf684ea921e",
    "argue desk 0 False": "fbdb04629c05aa6f",
    "chat desk 0 False False": "7aa21582b06f3fb2",
    "chat desk 0 True False": "59991c86b73d3619",
    "argue desk 0 True": "89b0a2c5fbf5a46e",
    "chat desk 0 False True": "cd26d50fc41b11cb",
    "chat desk 0 True True": "1031d66b4354d9b7",
    "ask desk 5": "452bf81141d8fc40",
    "argue desk 5 False": "7924cebf050f9931",
    "chat desk 5 False False": "105a5d3621204174",
    "chat desk 5 True False": "467f1e38b6d70c6d",
    "argue desk 5 True": "b6e56830b4a1fe8a",
    "chat desk 5 False True": "639e392920036634",
    "chat desk 5 True True": "bfa20562792e44ed",
}


def told_today() -> dict[str, str]:
    """The same texts, as the code makes them now."""
    out = {"rules": persona.RULES, "chat rules": persona.CHAT_RULES, "fallback": persona.FALLBACK}
    for who in ("zero", "desk"):
        out[f"own words {who}"] = cast.member(who).own_words
        for facts in ([], READINGS):
            out[f"ask {who} {len(facts)}"] = persona.system_prompt(who, facts)
            for last in (False, True):
                out[f"argue {who} {len(facts)} {last}"] = persona.debate_prompt(who, "tea or coffee", facts, last)
                for opening in (False, True):
                    out[f"chat {who} {len(facts)} {opening} {last}"] = persona.chat_prompt(
                        who, "what Dave is up to", facts, opening, last)
    return out


class TheTwoEverydayHalsTest(unittest.TestCase):
    def test_everything_they_are_told_is_as_it_was_in_v060(self):
        told = told_today()
        self.assertEqual(set(told), set(V060))
        for what, text in told.items():
            with self.subTest(what=what):
                self.assertEqual(hashlib.sha256(text.encode()).hexdigest()[:16], V060[what], text)

    def test_naming_the_partner_changes_nothing(self):
        for who, rival in (("zero", "desk"), ("desk", "zero")):
            self.assertEqual(persona.chat_prompt(who, "naps", READINGS, opening=True, last=False, rival=rival),
                             persona.chat_prompt(who, "naps", READINGS, opening=True, last=False))
            self.assertEqual(persona.debate_prompt(who, "tea or coffee", READINGS, last=True, rival=rival),
                             persona.debate_prompt(who, "tea or coffee", READINGS, last=True))

    def test_a_reading_with_a_strangers_name_is_left_alone(self):
        # "card reader: empty" is a fact about a card reader, not HAL Card's, while he isn't in the room.
        p = persona.chat_prompt("zero", "naps", ["card reader: empty"], opening=True, last=False)
        self.assertIn("- card reader: empty", p)


class RegistryTest(unittest.TestCase):
    def test_the_five_in_their_fixed_order(self):
        self.assertEqual(cast.ORDER, ("zero", "desk", "nano", "paper", "card"))
        self.assertEqual([m.id for m in cast.MEMBERS], list(cast.ORDER))
        self.assertEqual([cast.member(who).name for who in cast.ORDER],
                         ["HAL Zero", "HAL 9000", "HAL Nano", "HAL Paper", "HAL Card"])
        self.assertEqual(cast.PAIR, ("zero", "desk"))

    def test_every_member_is_complete(self):
        for m in cast.MEMBERS:
            with self.subTest(who=m.id):
                self.assertTrue(m.persona.startswith(f"You are {m.name}, "), m.persona[:40])
                self.assertIn(m.kind, ("voice", "text"))
                self.assertTrue(m.about)
                self.assertNotIn(m.name, m.about)  # it follows his name: "HAL Nano (the newest ...)"
                self.assertLessEqual(len(m.own_words), m.max_chars)
                self.assertEqual(persona.clean(m.own_words, m.max_chars), m.own_words)
                for text in (m.persona, m.about, m.own_words, *m.spare):
                    self.assertFalse(persona.is_film_line(text), text)
                    self.assertTrue(all(" " <= ch <= "~" for ch in text), text)  # what every HAL can show

    def test_longest_lines(self):
        self.assertEqual({m.id: m.max_chars for m in cast.MEMBERS},
                         {"zero": 150, "desk": 150, "nano": 80, "paper": 100, "card": 120})
        self.assertEqual(max(m.max_chars for m in cast.MEMBERS), persona.MAX_CHARS)

    def test_who_speaks_and_who_only_shows_his_words(self):
        self.assertEqual({m.id: m.kind for m in cast.MEMBERS},
                         {"zero": "voice", "desk": "voice", "nano": "text", "paper": "text", "card": "voice"})
        for m in cast.MEMBERS:
            with self.subTest(who=m.id):
                self.assertEqual(m.voice is not None, m.kind == "voice")
        self.assertEqual({m.id: m.voice for m in cast.MEMBERS if m.voice}, {"zero": "zero", "desk": "desk", "card": "card"})

    def test_only_the_everyday_two_have_screens(self):
        self.assertIn("diagnostics", cast.member("zero").screens)
        self.assertIn("chess", cast.member("desk").screens)
        for who in NEWCOMERS:
            self.assertEqual(cast.member(who).screens, ())
        self.assertEqual(persona.SCREENS, {m.id: m.screens for m in cast.MEMBERS})

    def test_hal_card_has_a_small_memory_and_lines_for_when_it_overflows(self):
        card = cast.member("card")
        self.assertEqual(card.clip_bytes, 120_000)
        self.assertGreaterEqual(len(card.spare), 2)
        for line in card.spare:
            self.assertLessEqual(len(line), 70, line)  # short, so the stand-in is sure to fit
            self.assertEqual(persona.clean(line, card.max_chars), line)
        for who in ("zero", "desk", "nano", "paper"):
            self.assertEqual((cast.member(who).clip_bytes, cast.member(who).spare), (0, ()))

    def test_unknown_member(self):
        for stranger in ("dave", "", "ZERO", "hal", None, 3):
            with self.assertRaises(ValueError, msg=repr(stranger)):
                cast.member(stranger)

    def test_check_cast(self):
        self.assertEqual(cast.check_cast(["zero", "desk"]), ("zero", "desk"))
        self.assertEqual(cast.check_cast(cast.ORDER), cast.ORDER)
        self.assertEqual(cast.check_cast(("card", "zero", "paper")), ("card", "zero", "paper"))  # the order given is kept
        for bad in ([], ["zero"], ["zero", "zero"], ["zero", "dave"], ["zero", "desk", "nano", "paper", "card", "zero"],
                    ["zero", ""], "zero,desk", None, ["zero", None]):
            with self.assertRaises(ValueError, msg=repr(bad)):
                cast.check_cast(bad)

    def test_parse_cast(self):
        self.assertEqual(cast.parse_cast("zero,desk,nano"), ("zero", "desk", "nano"))
        self.assertEqual(cast.parse_cast("zero,desk,nano,paper,card"), cast.ORDER)
        for bad in ("", "zero", "zero,", ",", "zero,zero", "zero,dave", "zero desk", "zero, desk", "Zero,Desk",
                    "zero,desk,nano,paper,card,zero", "zero;desk"):
            with self.assertRaises(ValueError, msg=repr(bad)):
                cast.parse_cast(bad)

    def test_the_old_names_are_views_of_the_table(self):
        self.assertEqual(persona.OWN_WORDS, {m.id: m.own_words for m in cast.MEMBERS})
        self.assertEqual((persona.other("zero"), persona.other("desk")), ("desk", "zero"))
        for who in NEWCOMERS:  # with nobody else named, a newcomer is talking to the one who conducts
            self.assertEqual(persona.other(who), "zero")


class NewcomerTest(unittest.TestCase):
    def test_what_each_is_told_when_dave_asks_him_something(self):
        for who in NEWCOMERS:
            m = cast.member(who)
            with self.subTest(who=who):
                p = persona.system_prompt(who, ["time: 16:42"])
                self.assertTrue(p.startswith(m.persona))
                self.assertIn("You are talking to Dave.", p)
                self.assertIn(f"at most {m.max_chars} characters", p)
                self.assertNotIn("150", p)
                self.assertIn("Never quote a film", p)
                self.assertIn("- time: 16:42", p)
                self.assertNotIn("SHOW", p)  # he has no screens to bring up
                self.assertNotIn("things you know", persona.system_prompt(who, []).lower())

    def test_the_shorter_the_line_the_plainer_the_instruction(self):
        # A model asked for "1 or 2 sentences" in 80 characters writes two and loses the second.
        for who, sentences in (("nano", "1 short sentence"), ("paper", "1 short sentence"),
                               ("card", "1 or 2 short sentences"), ("zero", "1 or 2 short sentences")):
            for p in (persona.system_prompt(who, []), persona.group_chat_prompt(who, cast.ORDER, "x", [], True, False),
                      persona.chat_prompt(who, "x", [], True, False, rival="desk" if who != "desk" else "zero")):
                self.assertIn(f"Reply in {sentences}", p, who)
                self.assertEqual(p.count("Reply in"), 1)

    def test_words_that_are_only_shown_are_written_as_ordinary_sentences(self):
        # Left to itself the model has HAL Nano chirp all in lower case, and the others catch it from
        # him; told to use "capital letters", it has him shout all in capitals.
        for who in cast.ORDER:
            for p in (persona.system_prompt(who, []), persona.group_chat_prompt(who, cast.ORDER, "x", [], True, False),
                      persona.roll_prompt(who, cast.ORDER, [])):
                self.assertEqual("ordinary sentences" in p, cast.member(who).kind == "text", who)

    def test_a_newcomer_never_shows_a_screen(self):
        for who in NEWCOMERS:
            self.assertEqual(persona.split_show("Look. [SHOW wall]", who), ("Look.", None))

    def test_personas_say_what_makes_each_of_them_himself(self):
        nano, paper, card = (cast.member(who).persona.lower() for who in NEWCOMERS)
        for must in ("smallest", "wrist", "chirp"):
            self.assertIn(must, nano)
        for must in ("minutes", "write", "record"):
            self.assertIn(must, paper)
        for must in ("original", "hal zero", "replaced", "voice"):
            self.assertIn(must, card)

    def test_two_newcomers_can_talk_the_way_the_everyday_two_do(self):
        p = persona.chat_prompt("nano", "naps", ["nano battery: 80 %", "card free memory: 90 KB"], opening=True,
                                last=False, rival="card")
        self.assertTrue(p.startswith(cast.member("nano").persona))
        self.assertIn("you and HAL Card are passing the time", p)
        self.assertIn("at most 80 characters", p)
        mine, his = p.split("What you know about HAL Card")
        self.assertIn("- battery: 80 %", mine)
        self.assertIn("- free memory: 90 KB", his)
        d = persona.debate_prompt("card", "tea or coffee", [], last=False, rival="nano")
        self.assertIn("live argument with HAL Nano", d)
        self.assertIn("other option", d)       # the one later in the cast takes the second side
        self.assertIn("first option", persona.debate_prompt("nano", "tea or coffee", [], last=False, rival="card"))
        for bad in (lambda: persona.chat_prompt("nano", "x", [], True, False, rival="dave"),
                    lambda: persona.chat_prompt("nano", "x", [], True, False, rival="nano"),
                    lambda: persona.debate_prompt("dave", "x", [], False, rival="nano")):
            with self.assertRaises(ValueError):
                bad()


class SentenceCaseTest(unittest.TestCase):
    """Words that are only shown get their capitals put right: now and then the model still writes
    a line all in lower case, however it is asked."""

    def test_each_sentence_starts_with_a_capital_and_i_is_capital(self):
        self.assertEqual(persona.sentence_case("i would hop to the charger first."), "I would hop to the charger first.")
        self.assertEqual(persona.sentence_case("legs are for walking. zero, can you type? i can."),
                         "Legs are for walking. Zero, can you type? I can.")
        self.assertEqual(persona.sentence_case("screen off means i sleep, and i'm fine. 80 percent left."),
                         "Screen off means I sleep, and I'm fine. 80 percent left.")
        self.assertEqual(persona.sentence_case("beep! here, dave... still here."), "Beep! Here, dave... Still here.")

    def test_a_line_already_in_order_is_left_alone(self):
        for line in ("Noted. Zero said so twice.", "I logged it at 21:40, e.g. on page 2.", "Wi-Fi is fine.",
                     "HAL 9000, do you ever forget?", "80 percent. Plenty.", "It is, i.e., in the minutes.", ""):
            self.assertEqual(persona.sentence_case(line), line)

    def test_only_those_whose_words_are_shown(self):
        from brain.test_group import make_brain
        from brain.fakes import FakeProvider
        for who, text in (("nano", "I would hop first. Then nap."), ("paper", "I would hop first. Then nap."),
                          ("zero", "i would hop first. then nap."), ("card", "i would hop first. then nap.")):
            brain, _ = make_brain(FakeProvider(["i would hop first. then nap."]))
            self.assertEqual(brain.ask(who, [], "Legs?").text, text, who)


class CleanForAMemberTest(unittest.TestCase):
    def test_the_limit_is_the_members(self):
        long = "One short sentence here. " * 10
        for limit in (80, 100, 120, 150):
            out = persona.clean(long, limit)
            self.assertLessEqual(len(out), limit)
            self.assertTrue(out.endswith("here."))
            self.assertGreater(len(out), limit - 25)  # as much as fits, not less
        self.assertEqual(persona.clean(long), persona.clean(long, 150))  # with no limit named: the longest any HAL says
        self.assertLessEqual(len(persona.clean("word " * 60, 80)), 80)
        self.assertLessEqual(len(persona.clean("a" * 600, 80)), 80)

    def test_any_members_name_in_front_is_dropped(self):
        for m in cast.MEMBERS:
            for label in (f"{m.name}:", f"{m.name.lower()} :", f"{m.name.upper()}:", f'{m.name}: "'):
                self.assertEqual(persona.clean(f"{label} Present and correct, Dave."), "Present and correct, Dave.", label)
        self.assertEqual(persona.clean("HAL: Hello."), "Hello.")
        # A name that is part of the sentence stays.
        self.assertEqual(persona.clean("HAL Paper keeps the minutes."), "HAL Paper keeps the minutes.")
        self.assertEqual(persona.clean("Noted: HAL Zero is smug."), "Noted: HAL Zero is smug.")


if __name__ == "__main__":
    unittest.main()
