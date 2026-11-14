"""Chats nobody asked for: the two HALs passing the time, on a subject drawn at random."""
import pathlib
import tempfile
import unittest

from brain import persona, subjects
from brain.clips import ClipStore
from brain.core import Brain
from brain.debate import Debate, Debates
from brain.fakes import FakeEars, FakeProvider, FakeVoice
from brain.memory import ChatMemory
from brain.test_server import ServerCase, fields


def make_brain(provider=None):
    provider = provider or FakeProvider()
    return Brain(provider, FakeVoice(), FakeEars(), ClipStore(), ChatMemory()), provider


class FixedSubjects:
    def __init__(self, subject="the weather neither of you can see"):
        self.subject, self.picked = subject, 0

    def pick(self):
        self.picked += 1
        return self.subject


class ChatPromptTest(unittest.TestCase):
    def test_who_what_and_with_whom(self):
        p = persona.chat_prompt("desk", "what Dave is up to", ["time: 16:42"], opening=False, last=False)
        self.assertIn("You are HAL 9000", p)
        self.assertIn("HAL Zero", p)             # who he is talking to
        self.assertIn("what Dave is up to", p)
        self.assertIn("time: 16:42", p)
        self.assertIn("150 characters", p)       # the same short spoken lines
        self.assertIn("Never quote a film", p)   # and the same rule about the film
        self.assertNotIn("[SHOW", p)

    def test_it_is_a_chat_between_the_two_and_not_an_argument(self):
        p = persona.chat_prompt("zero", "naps", [], opening=False, last=False)
        self.assertNotIn("You are talking to Dave", p)  # he is talking to the other HAL
        self.assertNotIn("first option", p)
        self.assertNotIn("argue for", p)
        self.assertNotIn("only when Dave asks", p)      # nobody is asking: he may use what he knows

    def test_first_line_opens_and_last_line_closes(self):
        opening = persona.chat_prompt("zero", "naps", [], opening=True, last=False)
        middle = persona.chat_prompt("zero", "naps", [], opening=False, last=False)
        closing = persona.chat_prompt("zero", "naps", [], opening=False, last=True)
        self.assertIn("You speak first", opening)
        self.assertNotIn("You speak first", middle)
        self.assertIn("just said", middle)
        self.assertIn("last line", closing)
        self.assertNotIn("last line", middle)

    def test_each_hal_gets_his_own_readings(self):
        facts = ["time: 20:24", "zero chip temperature: 54 C", "desk battery: 53 % and charging"]
        zero = persona.chat_prompt("zero", "x", facts, opening=True, last=False)
        desk = persona.chat_prompt("desk", "x", facts, opening=False, last=False)
        self.assertIn("- chip temperature: 54 C", zero.split("What you know about")[0])
        self.assertIn("- battery: 53 % and charging", desk.split("What you know about")[0])
        self.assertIn("battery: 53 %", zero.split("What you know about")[1])

    def test_unknown_hal(self):
        with self.assertRaises(ValueError):
            persona.chat_prompt("dave", "x", [], opening=True, last=False)

    def test_the_question_rules_are_untouched(self):
        self.assertIn("You are talking to Dave.", persona.RULES)
        self.assertIn("only when Dave asks about it", persona.RULES)


class SubjectsTest(unittest.TestCase):
    def write(self, text):
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        path = pathlib.Path(folder.name) / "subjects.txt"
        path.write_text(text, encoding="utf-8")
        return path

    def test_load(self):
        path = self.write("# things to talk about\n\n  the weather  \nnaps\n# not this\n\tDave's desk\n")
        self.assertEqual(subjects.load(path), ["the weather", "naps", "Dave's desk"])

    def test_load_keeps_them_short_and_printable(self):
        path = self.write("cafés and \x07bells\n" + "x" * 500 + "\n")
        got = subjects.load(path)
        self.assertEqual(got[0], "caf s and bells")  # printable ASCII only, like everything the HALs handle
        self.assertEqual(len(got[1]), subjects.MAX_SUBJECT)

    def test_a_missing_or_empty_file_still_gives_them_something_to_say(self):
        for path in (pathlib.Path("/nonexistent/subjects.txt"), self.write("# nothing\n\n")):
            got = subjects.load(path)
            self.assertEqual(got, subjects.SPARE)
            self.assertGreaterEqual(len(got), 3)

    def test_the_list_that_ships_is_usable(self):
        got = subjects.load()
        self.assertGreaterEqual(len(got), 20)
        self.assertEqual(len(got), len(set(got)))
        for s in got:
            self.assertTrue(3 <= len(s) <= subjects.MAX_SUBJECT, s)

    def test_every_subject_comes_up_before_any_comes_up_again(self):
        bag = subjects.Subjects(["a", "b", "c", "d"], random=lambda items: items.reverse())
        first = [bag.pick() for _ in range(4)]
        self.assertEqual(sorted(first), ["a", "b", "c", "d"])
        second = [bag.pick() for _ in range(4)]
        self.assertEqual(sorted(second), ["a", "b", "c", "d"])

    def test_never_the_same_subject_twice_running(self):
        # Shuffled afresh each round: the new round must not open with what the last one closed on.
        order = iter([["a", "b", "c"], ["c", "a", "b"], ["b", "c", "a"]])

        def shuffle(items):
            items[:] = next(order)

        bag = subjects.Subjects(["a", "b", "c"], random=shuffle)
        got = [bag.pick() for _ in range(9)]
        for one, two in zip(got, got[1:]):
            self.assertNotEqual(one, two, got)

    def test_one_subject_is_still_a_list(self):
        bag = subjects.Subjects(["only"])
        self.assertEqual([bag.pick(), bag.pick()], ["only", "only"])


class ChatTest(unittest.TestCase):
    def test_a_chat_uses_the_chat_instructions(self):
        brain, provider = make_brain()
        chat = Debate(brain, "naps", ["time: 16:42"], turns=4, kind="chat")
        for n in range(4):
            self.assertEqual(chat.get(n, timeout_s=5)[0], "line")
        systems = [system for system, _ in provider.calls]
        self.assertTrue(all("passing the time" in s and "naps" in s for s in systems))
        self.assertEqual(["You speak first" in s for s in systems], [True, False, False, False])
        self.assertEqual(["last line" in s for s in systems], [False, False, False, True])
        self.assertFalse(any("first option" in s for s in systems))

    def test_either_hal_can_open(self):
        brain, provider = make_brain()
        chat = Debate(brain, "naps", [], turns=4, kind="chat", first="desk")
        who = [chat.get(n, timeout_s=5)[1].who for n in range(4)]
        self.assertEqual(who, ["desk", "zero", "desk", "zero"])
        self.assertIn("You are HAL 9000", provider.calls[0][0])
        for _, messages in provider.calls:  # an order any model accepts, whoever opened
            roles = [m["role"] for m in messages]
            self.assertEqual((roles[0], roles[-1]), ("user", "user"), roles)

    def test_an_argument_is_unchanged(self):
        brain, provider = make_brain()
        debate = Debate(brain, "tea or coffee", [], turns=2)
        self.assertEqual([debate.get(n, timeout_s=5)[1].who for n in range(2)], ["zero", "desk"])
        self.assertIn("first option", provider.calls[0][0])

    def test_unknown_kind_or_opener(self):
        brain, _ = make_brain()
        for kwargs in ({"kind": "duel"}, {"first": "dave"}, {"kind": ""}):
            with self.assertRaises(ValueError, msg=repr(kwargs)):
                Debate(brain, "x", [], turns=2, **kwargs)

    def test_a_chat_with_no_subject_draws_one(self):
        brain, provider = make_brain()
        drawn = FixedSubjects("what Dave had for lunch")
        debates = Debates(brain, subjects=drawn)
        chat = debates.get(debates.start("", [], 2, kind="chat"))
        chat.get(0, timeout_s=5)
        self.assertEqual(drawn.picked, 1)
        self.assertIn("what Dave had for lunch", provider.calls[0][0])
        # A subject that was given is used as it is.
        chat = debates.get(debates.start("the Wi-Fi", [], 2, kind="chat"))
        chat.get(0, timeout_s=5)
        self.assertEqual(drawn.picked, 1)
        self.assertIn("the Wi-Fi", provider.calls[-1][0])

    def test_an_argument_still_needs_its_subject(self):
        brain, _ = make_brain()
        with self.assertRaises(ValueError):
            Debates(brain, subjects=FixedSubjects()).start("", [], 2)


class ChatRequestTest(ServerCase):
    def test_a_chat_without_a_subject(self):
        status, body, _ = self.call("POST", "/debate?kind=chat&turns=4&first=desk", b"time: 16:42\n\n")
        self.assertEqual(status, 200)
        chat = fields(body)["debate"]
        first = fields(self.call("GET", f"/debate/{chat}/0")[1])
        self.assertEqual(first["who"], "desk")
        self.assertEqual(fields(self.call("GET", f"/debate/{chat}/1")[1])["who"], "zero")
        self.assertIn("passing the time", self.provider.calls[0][0])
        self.assertIn("time: 16:42", self.provider.calls[0][0])

    def test_a_chat_with_no_body_at_all(self):
        self.assertEqual(self.call("POST", "/debate?kind=chat&turns=2", b"")[0], 200)

    def test_a_chat_on_a_given_subject(self):
        chat = fields(self.call("POST", "/debate?kind=chat&turns=2", b"\nthe state of Dave's desk")[1])["debate"]
        self.call("GET", f"/debate/{chat}/0")
        self.assertIn("the state of Dave's desk", self.provider.calls[0][0])

    def test_bad_requests(self):
        for path, body in (("/debate?kind=duel", b"\ntea"), ("/debate?kind=chat&first=dave", b"\n"),
                           ("/debate?kind=chat&turns=99", b"\n"), ("/debate?kind=argue", b"\n"), ("/debate", b"\n")):
            status, reply, _ = self.call("POST", path, body)
            self.assertEqual((status, reply), (400, b"error=bad-request\n"), path)

    def test_a_chat_needs_the_key_like_everything_else(self):
        self.assertEqual(self.call("POST", "/debate?kind=chat", b"\n", key=None)[0], 401)


if __name__ == "__main__":
    unittest.main()
