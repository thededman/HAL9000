"""Conversations for a cast: who speaks when, what each is told, and what comes back over HTTP."""
import itertools
import random
import unittest

from brain import cast, persona
from brain.clips import ClipStore
from brain.core import Brain, Line
from brain.debate import Debate, Debates, default_turns, plan_turns
from brain.fakes import FakeEars, FakeProvider, FakeVoice, make_wav
from brain.memory import ChatMemory
from brain.test_server import ServerCase, fields
from brain.voice import VoiceError

FIVE = cast.ORDER
READINGS = ["time: 20:24", "zero chip temperature: 54 C", "desk battery: 53 % and charging", "nano battery: 80 %",
            "card free memory: 90 KB"]


def name(who: str) -> str:
    return cast.member(who).name


def make_brain(provider=None, voice=None):
    provider = provider or FakeProvider()
    return Brain(provider, voice or FakeVoice(), FakeEars(), ClipStore(), ChatMemory()), provider


def said(*lines) -> list[Line]:
    return [Line(who, text, "", 0) for who, text in lines]


class PlanTurnsTest(unittest.TestCase):
    def casts(self):
        for size in range(2, 6):
            yield from itertools.permutations(FIVE, size) if size < 4 else itertools.combinations(FIVE, size)

    def test_who_speaks_when(self):
        for members in self.casts():
            for seed in range(8):
                for turns in (2, 3, 4, 7, 10, 12):
                    order = plan_turns(members, turns, random.Random(seed))
                    why = f"{members} seed {seed}: {order}"
                    self.assertEqual(len(order), turns, why)
                    self.assertTrue(set(order) <= set(members), why)
                    for one, two in zip(order, order[1:]):
                        self.assertNotEqual(one, two, why)  # never the same speaker twice running
                    for upto in range(1, turns + 1):       # whoever has spoken least goes next
                        spoken = [order[:upto].count(who) for who in members]
                        self.assertLessEqual(max(spoken) - min(spoken), 1, why)

    def test_everyone_speaks_before_anyone_speaks_a_third_time(self):
        for seed in range(50):
            order = plan_turns(FIVE, 10, random.Random(seed))
            self.assertEqual(sorted(order[:5]), sorted(FIVE), order)
            self.assertEqual(sorted(order), sorted(FIVE * 2), order)

    def test_the_same_seed_gives_the_same_talk(self):
        self.assertEqual(plan_turns(FIVE, 10, random.Random(42)), plan_turns(FIVE, 10, random.Random(42)))
        self.assertGreater(len({tuple(plan_turns(FIVE, 10, random.Random(seed))) for seed in range(30)}), 20)

    def test_anyone_may_open(self):
        self.assertEqual({plan_turns(FIVE, 5, random.Random(seed))[0] for seed in range(200)}, set(FIVE))

    def test_a_named_opener_opens(self):
        for who in FIVE:
            self.assertEqual(plan_turns(FIVE, 6, random.Random(1), first=who)[0], who)
        self.assertEqual(plan_turns(("zero", "desk"), 5, random.Random(1), first="desk"),
                         ["desk", "zero", "desk", "zero", "desk"])
        with self.assertRaises(ValueError):
            plan_turns(("zero", "desk", "nano"), 6, random.Random(1), first="paper")

    def test_about_two_lines_each(self):
        self.assertEqual([default_turns(FIVE[:size]) for size in (2, 3, 4, 5)], [4, 6, 8, 10])


class GroupPromptTest(unittest.TestCase):
    def test_a_group_chat_says_who_else_is_there(self):
        p = persona.group_chat_prompt("nano", FIVE, "what Dave is up to", READINGS, opening=False, last=False)
        self.assertTrue(p.startswith(cast.member("nano").persona))
        for who in ("zero", "desk", "paper", "card"):
            self.assertIn(f"{name(who)} ({cast.member(who).about})", p)
        self.assertNotIn("HAL Nano (", p)  # he knows who he is
        self.assertIn("what Dave is up to", p)
        self.assertIn("at most 80 characters", p)
        self.assertIn("Never quote a film", p)
        self.assertNotIn("You are talking to Dave", p)
        self.assertNotIn("SHOW", p)
        self.assertNotIn("argue for", p)

    def test_only_those_present_are_named(self):
        p = persona.group_chat_prompt("zero", ("zero", "desk", "paper"), "naps", [], opening=True, last=False)
        self.assertIn("HAL 9000 (", p)
        self.assertIn("HAL Paper (", p)
        self.assertNotIn("HAL Nano", p)
        self.assertNotIn("HAL Card", p)
        self.assertIn("at most 150 characters", p)

    def test_first_line_opens_and_last_line_closes(self):
        opening = persona.group_chat_prompt("paper", FIVE, "naps", [], opening=True, last=False)
        middle = persona.group_chat_prompt("paper", FIVE, "naps", [], opening=False, last=False)
        closing = persona.group_chat_prompt("paper", FIVE, "naps", [], opening=False, last=True)
        self.assertIn("You speak first", opening)
        self.assertNotIn("You speak first", middle)
        self.assertIn("last line", closing)
        self.assertNotIn("last line", middle)

    def test_each_gets_his_own_readings_and_the_others_by_name(self):
        p = persona.group_chat_prompt("nano", FIVE, "x", READINGS, opening=True, last=False)
        mine = p.split("What you know about")[0]
        self.assertIn("- battery: 80 %", mine)
        self.assertIn("- time: 20:24", mine)
        self.assertNotIn("chip temperature", mine)
        self.assertIn("What you know about HAL Zero right now (his readings, not yours):\n- chip temperature: 54 C", p)
        self.assertIn("What you know about HAL Card right now (his readings, not yours):\n- free memory: 90 KB", p)
        self.assertNotIn("What you know about HAL Paper", p)  # nothing was sent for him

    def test_an_argument_gives_every_hal_a_side(self):
        sides = persona.sides(FIVE)
        self.assertEqual((sides["zero"], sides["desk"]), (0, 1))  # as in every argument the two have had
        self.assertEqual(set(sides), set(FIVE))
        for members in itertools.chain.from_iterable(itertools.combinations(FIVE, n) for n in range(2, 6)):
            self.assertEqual(set(persona.sides(members).values()), {0, 1}, members)  # nobody argues alone... or unopposed
        for who in FIVE:
            p = persona.group_debate_prompt(who, FIVE, "tea or coffee", READINGS, last=False)
            with self.subTest(who=who):
                self.assertTrue(p.startswith(cast.member(who).persona))
                self.assertIn("tea or coffee", p)
                self.assertEqual("first option" in p, sides[who] == 0)
                self.assertEqual("other option" in p, sides[who] == 1)
                allies = [name(w) for w in FIVE if w != who and sides[w] == sides[who]]
                rivals = [name(w) for w in FIVE if sides[w] != sides[who]]
                self.assertIn("On your side: " + ", ".join(allies), p)
                self.assertIn("Against you: " + ", ".join(rivals), p)
                self.assertIn(f"at most {cast.member(who).max_chars} characters", p)
                self.assertNotIn("last word", p)
        self.assertIn("last word", persona.group_debate_prompt("card", FIVE, "tea or coffee", [], last=True))

    def test_a_side_of_one_has_no_allies_to_name(self):
        p = persona.group_debate_prompt("desk", ("zero", "desk", "nano"), "tea or coffee", [], last=False)
        self.assertNotIn("On your side", p)
        self.assertIn("Against you: HAL Zero, HAL Nano", p)

    def test_roll_call(self):
        for who in FIVE:
            p = persona.roll_prompt(who, FIVE, READINGS)
            with self.subTest(who=who):
                self.assertTrue(p.startswith(cast.member(who).persona))
                self.assertIn("roll", p)
                self.assertIn("introduce yourself", p)
                self.assertIn("HAL Zero, HAL 9000, HAL Nano, HAL Paper, HAL Card", p)  # the order they answer in
                self.assertIn(f"at most {cast.member(who).max_chars} characters", p)
                self.assertNotIn("SHOW", p)

    def test_unknown_members(self):
        for bad in (lambda: persona.group_chat_prompt("dave", FIVE, "x", [], True, False),
                    lambda: persona.group_chat_prompt("nano", ("zero", "desk", "paper"), "x", [], True, False),
                    lambda: persona.group_debate_prompt("zero", ("zero", "dave", "nano"), "x", [], False),
                    lambda: persona.roll_prompt("dave", FIVE, [])):
            with self.assertRaises(ValueError):
                bad()

    def test_the_talk_so_far_is_one_labelled_transcript(self):
        message = persona.group_message("nano", said(("zero", "Zero opens."), ("desk", "Desk answers."),
                                                     ("nano", "Nano chirps."), ("paper", "Paper notes it.")))
        lines = message.splitlines()
        at = lines.index("HAL Zero: Zero opens.")
        self.assertEqual(lines[at:at + 4], ["HAL Zero: Zero opens.", "HAL 9000: Desk answers.", "HAL Nano: Nano chirps.",
                                            "HAL Paper: Paper notes it."])
        self.assertTrue(lines[-1].startswith("Your turn, HAL Nano"), lines[-1])

    def test_with_nothing_said_yet_there_is_no_transcript(self):
        message = persona.group_message("paper", [])
        self.assertNotIn(":\nHAL", message)
        self.assertTrue(message.splitlines()[-1].startswith("Your turn, HAL Paper"), message)


class GroupTalkTest(unittest.TestCase):
    def talk(self, turns, **kwargs):
        brain, provider = make_brain(FakeProvider([f"Line {n}." for n in range(turns)]))
        debate = Debate(brain, kwargs.pop("topic", "naps"), kwargs.pop("facts", []), turns=turns, **kwargs)
        lines = []
        for n in range(debate.turns):
            status, line = debate.get(n, timeout_s=5)
            self.assertEqual(status, "line")
            lines.append(line)
        return debate, lines, provider, brain

    def test_a_three_way_chat_goes_to_the_model_as_one_labelled_transcript(self):
        debate, lines, provider, _ = self.talk(6, kind="chat", cast=("zero", "desk", "nano"), rng=random.Random(3))
        who = [line.who for line in lines]
        self.assertEqual(sorted(who), ["desk", "desk", "nano", "nano", "zero", "zero"])
        self.assertEqual(who, plan_turns(("zero", "desk", "nano"), 6, random.Random(3)))
        self.assertEqual(len(provider.calls), 6)
        for n, (system, messages) in enumerate(provider.calls):
            self.assertEqual([m["role"] for m in messages], ["user"], n)  # an order any model accepts
            content = messages[0]["content"]
            self.assertEqual([line for line in content.splitlines() if line.startswith("HAL ")],
                             [f"{name(line.who)}: {line.text}" for line in lines[:n]], n)
            self.assertTrue(content.splitlines()[-1].startswith(f"Your turn, {name(who[n])}"), content)
            self.assertTrue(system.startswith(cast.member(who[n]).persona))
            self.assertIn("naps", system)
            self.assertEqual("You speak first" in system, n == 0)
            self.assertEqual("last line" in system, n == 5)
            for other in set(who) - {who[n]}:
                self.assertIn(f"{name(other)} (", system)
        self.assertEqual(debate.get(6), ("end", None))

    def test_five_talk_and_a_named_opener_opens(self):
        _, lines, _, _ = self.talk(10, kind="chat", cast=FIVE, first="paper", rng=random.Random(5))
        who = [line.who for line in lines]
        self.assertEqual(who[0], "paper")
        self.assertEqual(sorted(who), sorted(FIVE * 2))

    def test_those_who_only_show_their_words_get_no_audio(self):
        _, lines, _, brain = self.talk(10, kind="chat", cast=FIVE, rng=random.Random(5))
        for line in lines:
            with self.subTest(who=line.who):
                if cast.member(line.who).kind == "text":
                    self.assertEqual((line.clip, line.ms), ("", 0))
                else:
                    self.assertIsNotNone(brain.clips.get(line.clip))
                    self.assertEqual(line.ms, 1000)
        self.assertEqual({who for who, _ in brain.voice.said}, {"zero", "desk", "card"})

    def test_a_group_argument_gives_each_speaker_his_side(self):
        _, lines, provider, _ = self.talk(10, topic="tea or coffee", facts=READINGS, cast=FIVE, rng=random.Random(2))
        sides = persona.sides(FIVE)
        for line, (system, messages) in zip(lines, provider.calls):
            self.assertEqual("first option" in system, sides[line.who] == 0, line.who)
            self.assertIn("tea or coffee", system)
            self.assertEqual(len(messages), 1)
        self.assertIn("last word", provider.calls[-1][0])
        self.assertNotIn("last word", provider.calls[0][0])

    def test_the_pair_named_as_a_cast_is_todays_conversation_to_the_letter(self):
        for kwargs in ({"kind": "argue"}, {"kind": "chat"}, {"kind": "chat", "first": "desk"}):
            _, plain_lines, plain, _ = self.talk(4, topic="tea or coffee", facts=READINGS, **kwargs)
            _, cast_lines, named, _ = self.talk(4, topic="tea or coffee", facts=READINGS, cast=("zero", "desk"), **kwargs)
            self.assertEqual(named.calls, plain.calls, kwargs)
            self.assertEqual([line.who for line in cast_lines], [line.who for line in plain_lines])
        _, lines, _, _ = self.talk(4, kind="chat", cast=("desk", "zero"))  # the first one named opens
        self.assertEqual([line.who for line in lines], ["desk", "zero", "desk", "zero"])

    def test_any_two_talk_the_way_the_everyday_two_do(self):
        _, lines, provider, _ = self.talk(4, kind="chat", cast=("nano", "card"))
        self.assertEqual([line.who for line in lines], ["nano", "card", "nano", "card"])
        self.assertEqual(provider.calls[0][0], persona.chat_prompt("nano", "naps", [], opening=True, last=False, rival="card"))
        self.assertEqual(provider.calls[1][0], persona.chat_prompt("card", "naps", [], opening=False, last=False, rival="nano"))
        self.assertEqual(provider.calls[0][1], [{"role": "user", "content": "Begin."}])
        self.assertEqual(provider.calls[2][1], [{"role": "user", "content": "Begin."},
                                                {"role": "assistant", "content": "Line 0."},
                                                {"role": "user", "content": "Line 1."}])
        _, _, provider, _ = self.talk(2, topic="tea or coffee", cast=("card", "nano"))
        self.assertEqual(provider.calls[0][0], persona.debate_prompt("card", "tea or coffee", [], last=False, rival="nano"))

    def test_roll_call_is_one_line_each_in_the_order_given(self):
        debate, lines, provider, _ = self.talk(12, kind="roll", topic="", cast=FIVE)
        self.assertEqual(debate.turns, 5)  # however many lines were asked for
        self.assertEqual([line.who for line in lines], list(FIVE))
        for n, (system, messages) in enumerate(provider.calls):
            self.assertEqual(system, persona.roll_prompt(FIVE[n], FIVE, []))
            self.assertEqual(messages, [{"role": "user", "content": persona.group_message(FIVE[n], lines[:n])}])
        self.assertEqual(debate.get(5), ("end", None))
        _, lines, _, _ = self.talk(3, kind="roll", topic="", cast=("card", "zero", "paper"))
        self.assertEqual([line.who for line in lines], ["card", "zero", "paper"])

    def test_roll_call_for_the_pair(self):
        debate, lines, provider, _ = self.talk(8, kind="roll", topic="")
        self.assertEqual([line.who for line in lines], ["zero", "desk"])
        self.assertIn("roll", provider.calls[0][0])

    def test_bad_casts_and_openers(self):
        brain, provider = make_brain()
        for kwargs in ({"cast": ("zero",)}, {"cast": ()}, {"cast": ("zero", "zero")}, {"cast": ("zero", "dave")},
                       {"cast": FIVE + ("zero",)}, {"cast": ("zero", "desk"), "first": "nano"},
                       {"cast": FIVE, "first": "dave"}, {"cast": FIVE, "kind": "duel"},
                       {"cast": ("zero", "desk", "nano"), "kind": "roll", "first": "card"}):
            with self.assertRaises(ValueError, msg=repr(kwargs)):
                Debate(brain, "x", [], turns=4, **kwargs)
        self.assertEqual(provider.calls, [])


class DebatesForACastTest(unittest.TestCase):
    def test_start_with_a_cast(self):
        brain, _ = make_brain()
        debates = Debates(brain)
        talk = debates.get(debates.start("naps", [], 6, kind="chat", cast=("zero", "nano", "paper")))
        who = [talk.get(n, timeout_s=5)[1].who for n in range(6)]
        self.assertEqual(sorted(who), ["nano", "nano", "paper", "paper", "zero", "zero"])

    def test_a_roll_call_needs_no_subject_and_an_argument_still_does(self):
        brain, _ = make_brain()
        debates = Debates(brain)
        roll = debates.get(debates.start("", [], 2, kind="roll", cast=FIVE))
        self.assertEqual(roll.turns, 5)
        with self.assertRaises(ValueError):
            debates.start("", [], 6, cast=FIVE)

    def test_a_bad_cast_stops_nothing(self):
        brain, _ = make_brain()
        debates = Debates(brain)
        running = debates.get(debates.start("tea", [], 6))
        for kwargs in ({"cast": ("zero", "dave")}, {"cast": ("zero",)}, {"cast": FIVE, "first": "dave"},
                       {"cast": ("zero", "desk"), "first": "nano"}):
            with self.assertRaises(ValueError, msg=repr(kwargs)):
                debates.start("coffee", [], 4, **kwargs)
        self.assertEqual([running.get(n, timeout_s=5)[0] for n in range(6)], ["line"] * 6)


class TextMemberTest(unittest.TestCase):
    def test_a_text_member_answers_in_words_only(self):
        brain, provider = make_brain(FakeProvider(["**Chirp!** I am on his wrist."]))
        line = brain.ask("nano", ["battery: 80 %"], "Where are you?")
        self.assertEqual(line, Line("nano", "Chirp! I am on his wrist.", "", 0, None))
        self.assertEqual(brain.voice.said, [])
        self.assertIn("HAL Nano", provider.calls[0][0])
        self.assertIn("battery: 80 %", provider.calls[0][0])
        self.assertEqual(brain.memory.history("nano")[-1]["content"], "Chirp! I am on his wrist.")

    def test_every_line_is_cut_to_its_speakers_limit(self):
        for who in FIVE:
            brain, _ = make_brain(FakeProvider(["One short sentence here. " * 10]))
            line = brain.ask(who, [], "Go on?")
            limit = cast.member(who).max_chars
            self.assertLessEqual(len(line.text), limit, who)
            self.assertGreater(len(line.text), limit - 25, who)

    def test_a_film_line_is_never_said_by_anyone(self):
        for who in FIVE:
            brain, _ = make_brain(FakeProvider(["I'm sorry, Dave. I'm afraid I can't do that. [SHOW wall]"]))
            line = brain.ask(who, [], "Open the pod bay doors, HAL.")
            self.assertEqual((line.text, line.show), (cast.member(who).own_words, None), who)

    def test_a_newcomer_never_brings_up_a_screen(self):
        brain, _ = make_brain(FakeProvider(["Look. [SHOW wall]"]))
        line = brain.ask("card", [], "Show me the wall")
        self.assertEqual((line.text, line.show), ("Look.", None))


class SizedVoice(FakeVoice):
    """A voice whose clips grow with the words, in HAL Card's format: 8-bit, 11 025 Hz, a tenth of a
    second (1 102 bytes) a character."""

    def __init__(self, seconds_per_char: float = 0.1):
        super().__init__()
        self.pace = seconds_per_char

    def render(self, who, text):
        self.said.append((who, text))
        return make_wav(len(text) * self.pace, rate=11025, width=1)


class SmallMemoryTest(unittest.TestCase):
    LONG = "Seventeen thousand, seven hundred and seventy-seven, Dave. I counted them twice, by hand, on the keyboard."

    def test_a_clip_too_large_for_hal_card_is_swapped_for_a_short_stock_line(self):
        card = cast.member("card")
        self.assertLessEqual(len(self.LONG), card.max_chars)  # within his limit, and still too long to hold
        brain, _ = make_brain(FakeProvider([self.LONG]), SizedVoice(0.11))
        line = brain.ask("card", [], "How many keys have you pressed?")
        self.assertIn(line.text, card.spare)
        self.assertLessEqual(len(brain.clips.get(line.clip)), card.clip_bytes)
        self.assertEqual(brain.voice.said, [("card", self.LONG), ("card", line.text)])
        self.assertAlmostEqual(line.ms, len(line.text) * 110, delta=2)  # the stock line's length, not the long one's
        self.assertEqual(brain.memory.history("card")[-1]["content"], line.text)  # what he said, not what he meant to

    def test_a_clip_that_fits_is_left_alone(self):
        brain, _ = make_brain(FakeProvider(["Still here, Dave. Still working."]), SizedVoice())
        line = brain.ask("card", [], "Are you there?")
        self.assertEqual(line.text, "Still here, Dave. Still working.")
        self.assertEqual(len(brain.voice.said), 1)

    def test_the_others_have_room(self):
        brain, _ = make_brain(FakeProvider(["Word. " * 24]), SizedVoice(0.2))
        line = brain.ask("zero", [], "Go on?")
        self.assertGreater(len(brain.clips.get(line.clip)), cast.member("card").clip_bytes)
        self.assertEqual(len(brain.voice.said), 1)

    def test_a_voice_whose_stock_line_is_too_large_as_well_says_so(self):
        brain, _ = make_brain(FakeProvider([self.LONG]), SizedVoice(1.0))  # a wrong sample rate, say
        with self.assertRaises(VoiceError):
            brain.ask("card", [], "Hello?")
        self.assertEqual(brain.memory.history("card"), [])

    def test_in_a_conversation_too(self):
        brain, _ = make_brain(FakeProvider(["Short one.", self.LONG]), SizedVoice(0.11))
        talk = Debate(brain, "naps", [], turns=2, kind="chat", cast=("zero", "card"))
        self.assertEqual(talk.get(0, timeout_s=5)[1].text, "Short one.")
        self.assertIn(talk.get(1, timeout_s=5)[1].text, cast.member("card").spare)


class CastRequestTest(ServerCase):
    def lines(self, debate, count):
        return [fields(self.call("GET", f"/debate/{debate}/{n}")[1]) for n in range(count)]

    def test_a_group_chat(self):
        status, body, _ = self.call("POST", "/debate?kind=chat&cast=zero,desk,nano,paper,card", b"time: 16:42\n\nnaps")
        self.assertEqual(status, 200)
        debate = fields(body)["debate"]
        self.assertEqual(self.server.hal_debates.get(debate).turns, 10)  # about two lines each
        got = self.lines(debate, 10)
        self.assertEqual(sorted(line["who"] for line in got), sorted(FIVE * 2))
        for n, line in enumerate(got):
            self.assertEqual(line["text"], f"Reply {n + 1}.")
            self.assertEqual(line["last"], str(int(n == 9)))
            if cast.member(line["who"]).kind == "text":
                self.assertNotIn("clip", line)  # words only
                self.assertEqual(line["ms"], "0")
            else:
                self.assertEqual(self.call("GET", "/clip/" + line["clip"], key=None)[0], 200)
                self.assertEqual(line["ms"], "1000")
        self.assertEqual(self.call("GET", f"/debate/{debate}/10")[0], 204)
        self.assertIn("naps", self.provider.calls[0][0])
        self.assertIn("- time: 16:42", self.provider.calls[0][0])

    def test_lengths(self):
        for query, turns in (("kind=chat&cast=zero,desk,nano", 6), ("kind=chat&cast=zero,desk,nano,paper", 8),
                             ("kind=chat&cast=zero,card", 4), ("kind=chat&cast=zero,desk,nano&turns=12", 12),
                             ("kind=chat", 8), ("kind=roll&cast=zero,nano,card", 3), ("kind=roll&turns=9", 2)):
            debate = fields(self.call("POST", "/debate?" + query, b"\nnaps")[1])["debate"]
            self.assertEqual(self.server.hal_debates.get(debate).turns, turns, query)

    def test_a_named_opener(self):
        debate = fields(self.call("POST", "/debate?kind=chat&cast=zero,desk,paper&first=paper", b"\n")[1])["debate"]
        self.assertEqual(self.lines(debate, 1)[0]["who"], "paper")

    def test_roll_call_with_no_body(self):
        status, body, _ = self.call("POST", "/debate?kind=roll&cast=zero,desk,nano,paper,card", b"")
        self.assertEqual(status, 200)
        got = self.lines(fields(body)["debate"], 5)
        self.assertEqual([line["who"] for line in got], list(FIVE))
        self.assertEqual([line["last"] for line in got], ["0", "0", "0", "0", "1"])
        self.assertIn("roll", self.provider.calls[0][0])

    def test_bad_casts_are_refused(self):
        for query in ("cast=zero", "cast=zero,zero", "cast=zero,dave", "cast=zero,desk,nano,paper,card,zero",
                      "cast=,", "cast=zero,desk&first=nano", "cast=zero,desk,nano&first=dave", "cast=zero%20desk",
                      "cast=zero,desk,nano&turns=13", "cast=zero,desk,nano&kind=duel"):
            status, reply, _ = self.call("POST", "/debate?" + query, b"\ntea or coffee")
            self.assertEqual((status, reply), (400, b"error=bad-request\n"), query)
        status, reply, _ = self.call("POST", "/debate?cast=zero,desk,nano", b"\n")  # an argument about nothing
        self.assertEqual((status, reply), (400, b"error=bad-request\n"))
        self.assertEqual(self.provider.calls, [])

    def test_asking_one_who_only_shows_his_words(self):
        for who in ("nano", "paper"):
            status, body, _ = self.call("POST", f"/ask?who={who}", b"battery: 80 %\n\nHow are you?")
            got = fields(body)
            self.assertEqual(status, 200)
            self.assertEqual(got["who"], who)
            self.assertTrue(got["text"].startswith("Reply "))
            self.assertNotIn("clip", got)
            self.assertEqual(got["ms"], "0")
            self.assertNotIn("show", got)
        self.assertEqual(self.voice.said, [])

    def test_every_answer_says_who_gave_it(self):
        for who in ("zero", "desk", "card"):
            got = fields(self.call("POST", f"/ask?who={who}", b"\nHello?")[1])
            self.assertEqual(got["who"], who)
            self.assertEqual(self.call("GET", "/clip/" + got["clip"], key=None)[0], 200)


if __name__ == "__main__":
    unittest.main()
