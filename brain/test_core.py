import threading
import time
import unittest

from brain.clips import ClipStore
from brain.core import Brain, Line
from brain.debate import Debate, Debates
from brain.fakes import FakeEars, FakeProvider, FakeVoice, make_wav
from brain.memory import ChatMemory
from brain.providers import ProviderError


def make_brain(provider=None, ears=None):
    provider = provider or FakeProvider()
    brain = Brain(provider, FakeVoice(), ears or FakeEars(), ClipStore(), ChatMemory())
    return brain, provider


class Clock:
    def __init__(self):
        self.now = 1000.0

    def __call__(self):
        return self.now


class AskTest(unittest.TestCase):
    def test_ask(self):
        brain, provider = make_brain(FakeProvider(["**I am** a cool 47 C."]))
        line = brain.ask("zero", ["temp: 47 C"], "How hot are you?")
        self.assertEqual(line, Line("zero", "I am a cool 47 C.", line.clip, 1000, None))
        self.assertEqual(brain.clips.get(line.clip), brain.voice.wav)
        self.assertEqual(brain.voice.said, [("zero", "I am a cool 47 C.")])
        system, messages = provider.calls[0]
        self.assertIn("temp: 47 C", system)
        self.assertIn("HAL Zero", system)
        self.assertEqual(messages, [{"role": "user", "content": "How hot are you?"}])

    def test_ask_remembers(self):
        brain, provider = make_brain(FakeProvider(["First answer.", "Second answer."]))
        brain.ask("desk", [], "One?")
        brain.ask("desk", [], "Two?")
        self.assertEqual(provider.calls[1][1], [{"role": "user", "content": "One?"},
                                                {"role": "assistant", "content": "First answer."},
                                                {"role": "user", "content": "Two?"}])
        brain.ask("zero", [], "And you?")  # the other HAL has his own conversation
        self.assertEqual(provider.calls[2][1], [{"role": "user", "content": "And you?"}])

    def test_ask_show(self):
        brain, _ = make_brain(FakeProvider(["Look. [SHOW wall]", "No. [SHOW chess]"]))
        line = brain.ask("zero", [], "Show me the wall")
        self.assertEqual((line.text, line.show), ("Look.", "wall"))
        self.assertEqual(brain.voice.said[-1], ("zero", "Look."))  # the tag is never spoken
        self.assertIsNone(brain.ask("zero", [], "Chess?").show)    # not one of his screens

    def test_ask_survives_junk(self):
        for junk in ("", "<think>x</think>", "**" + "a" * 600, "\U0001F600\U0001F600", "*sighs*"):
            brain, _ = make_brain(FakeProvider([junk]))
            line = brain.ask("desk", [], "Hello?")
            self.assertTrue(1 <= len(line.text) <= 150, repr(junk))
            self.assertIsNotNone(brain.clips.get(line.clip))

    def test_a_film_line_is_never_spoken(self):
        from brain.persona import OWN_WORDS
        brain, provider = make_brain(FakeProvider(["I'm sorry, Dave. I'm afraid I can't do that. [SHOW wall]"] * 2))
        for who in ("desk", "zero"):
            line = brain.ask(who, [], "Open the pod bay doors, HAL.")
            self.assertEqual((line.text, line.show), (OWN_WORDS[who], None))
            self.assertEqual(brain.voice.said[-1], (who, OWN_WORDS[who]))
            self.assertEqual(brain.memory.history(who)[-1]["content"], OWN_WORDS[who])  # nor remembered

    def test_ask_model_down(self):
        brain, _ = make_brain(FakeProvider(fail_on={1}))
        with self.assertRaises(ProviderError):
            brain.ask("zero", [], "Hello?")
        self.assertEqual(brain.memory.history("zero"), [])
        self.assertEqual(brain.voice.said, [])
        self.assertEqual(brain.ask("zero", [], "Hello again?").text, "Reply 2.")  # and he recovers

    def test_ask_bad_who(self):
        brain, provider = make_brain()
        with self.assertRaises(ValueError):
            brain.ask("dave", [], "Hello?")
        self.assertEqual(provider.calls, [])

    def test_one_generation_at_a_time(self):
        brain, provider = make_brain(FakeProvider(delay_s=0.02))
        lines = []
        threads = [threading.Thread(target=lambda i=i: lines.append(brain.ask("zero" if i % 2 else "desk", [], f"q{i}")))
                   for i in range(4)]
        for t in threads:
            t.start()
        for t in threads:
            t.join(5)
        self.assertEqual(len(lines), 4)
        self.assertFalse(provider.overlapped)

    def test_hear(self):
        ears = FakeEars(" (wind) hello  there [BLANK_AUDIO] ")
        brain, _ = make_brain(ears=ears)
        self.assertEqual(brain.hear(make_wav(1.0)), "hello there")
        with self.assertRaises(ValueError):
            brain.hear(make_wav(1.0, rate=22050))
        self.assertEqual(ears.heard, 1)  # a bad recording never reaches the ears


class DebateTest(unittest.TestCase):
    def wait_calls(self, provider, n):
        for _ in range(n):
            self.assertTrue(provider.finished.acquire(timeout=5), "the model was not called in time")

    def test_debate_order(self):
        brain, provider = make_brain(FakeProvider(["Zero opens.", "Desk answers.", "Zero again.", "Desk ends."]))
        debate = Debate(brain, "who makes better tea", ["time: 16:42"], turns=4)
        got = [debate.get(n, timeout_s=5) for n in range(4)]
        self.assertEqual([status for status, _ in got], ["line"] * 4)
        self.assertEqual([line.who for _, line in got], ["zero", "desk", "zero", "desk"])
        self.assertEqual([line.text for _, line in got], ["Zero opens.", "Desk answers.", "Zero again.", "Desk ends."])
        self.assertEqual(debate.get(4), ("end", None))
        self.assertEqual(debate.get(-1), ("end", None))
        systems = [system for system, _ in provider.calls]
        self.assertTrue(all("who makes better tea" in s and "time: 16:42" in s for s in systems))
        self.assertEqual(["last word" in s for s in systems], [False, False, False, True])
        self.assertEqual(provider.calls[0][1], [{"role": "user", "content": "Begin."}])
        self.assertEqual(provider.calls[2][1], [{"role": "assistant", "content": "Zero opens."},
                                                {"role": "user", "content": "Desk answers."}])
        self.assertEqual(brain.memory.history("zero"), [])  # an argument isn't part of the chat

    def test_debate_never_shows_a_screen(self):
        brain, _ = make_brain(FakeProvider(["Look at this. [SHOW wall]"]))
        _, line = Debate(brain, "tea", [], turns=2).get(0, timeout_s=5)
        self.assertEqual((line.text, line.show), ("Look at this.", None))

    def test_debate_one_ahead(self):
        brain, provider = make_brain()
        debate = Debate(brain, "tea", [], turns=6)
        self.assertEqual(debate.get(0, timeout_s=5)[0], "line")
        self.wait_calls(provider, 2)  # lines 0 and 1
        time.sleep(0.05)
        self.assertEqual(len(provider.calls), 2)  # line 2 waits until line 1 is asked for
        self.assertEqual(debate.get(1, timeout_s=5)[0], "line")
        self.wait_calls(provider, 1)
        time.sleep(0.05)
        self.assertEqual(len(provider.calls), 3)
        debate.stop()

    def test_debate_wait(self):
        provider = FakeProvider()
        provider.gate = threading.Event()
        brain, _ = make_brain(provider)
        debate = Debate(brain, "tea", [], turns=2)
        self.assertEqual(debate.get(0, timeout_s=0.05), ("wait", None))
        provider.gate.set()
        self.assertEqual(debate.get(0, timeout_s=5)[0], "line")

    def test_debate_stop(self):
        brain, provider = make_brain()
        debate = Debate(brain, "tea", [], turns=6)
        debate.get(0, timeout_s=5)
        self.wait_calls(provider, 2)
        debate.stop()
        self.assertEqual(debate.get(2, timeout_s=1), ("end", None))
        self.assertEqual(debate.get(0, timeout_s=1)[0], "line")  # what was said stays said
        time.sleep(0.05)
        self.assertEqual(len(provider.calls), 2)

    def test_debate_stops_when_nobody_asks(self):
        clock = Clock()
        brain, provider = make_brain()
        debate = Debate(brain, "tea", [], turns=6, idle_s=120, clock=clock, poll_s=0.01)
        debate.get(0, timeout_s=5)
        self.wait_calls(provider, 2)
        clock.now += 121
        debate.join(5)
        self.assertEqual(debate.get(2, timeout_s=1), ("end", None))
        self.assertEqual(len(provider.calls), 2)

    def test_debate_model_down(self):
        brain, _ = make_brain(FakeProvider(fail_on={2}))
        debate = Debate(brain, "tea", [], turns=4)
        self.assertEqual(debate.get(0, timeout_s=5)[0], "line")
        self.assertEqual(debate.get(1, timeout_s=5), ("end", None))

    def test_debates_registry(self):
        clock = Clock()
        brain, _ = make_brain()
        debates = Debates(brain, keep_s=600, clock=clock)
        first = debates.start("tea", [], 2)
        self.assertRegex(first, r"^[0-9a-f]{16}$")
        self.assertIsNotNone(debates.get(first))
        for unknown in ("", "nope", "0" * 16, None):
            self.assertIsNone(debates.get(unknown))
        second = debates.start("coffee", [], 2)  # a new argument ends the one before
        self.assertEqual(debates.get(first).get(1, timeout_s=1)[0] in ("line", "end"), True)
        self.assertNotEqual(first, second)
        clock.now += 601
        self.assertIsNone(debates.get(first))
        self.assertIsNone(debates.get(second))

    def test_new_debate_stops_the_old_one(self):
        brain, provider = make_brain()
        debates = Debates(brain)
        first = debates.get(debates.start("tea", [], 6))
        first.get(0, timeout_s=5)
        self.wait_calls(provider, 2)
        debates.start("coffee", [], 2)
        self.assertEqual(first.get(3, timeout_s=1), ("end", None))


if __name__ == "__main__":
    unittest.main()
