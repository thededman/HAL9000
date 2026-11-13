import unittest

from brain.persona import (FALLBACK, MAX_CHARS, OWN_WORDS, SCREENS, clean, debate_prompt, is_film_line, other,
                           parse_body, plain, split_show, strip_thinking, system_prompt)


class CleanTest(unittest.TestCase):
    def test_clean_plain_line_unchanged(self):
        self.assertEqual(clean("I am a cool 47 C."), "I am a cool 47 C.")

    def test_clean_strips_thinking_markdown_and_stage_directions(self):
        raw = "<think>hmm</think>**Certainly**, Dave. *smiles* (pause) `ok`"
        self.assertEqual(clean(raw), "Certainly, Dave. ok")

    def test_clean_unclosed_thinking_goes_to_the_end(self):
        self.assertEqual(clean("Fine, Dave. <think>and then I"), "Fine, Dave.")

    def test_clean_plain_punctuation_and_ascii_only(self):
        raw = "“It’s fine” — really… \U0001F600 café"
        self.assertEqual(clean(raw), "It's fine, really... caf")

    def test_clean_removes_surrounding_quotes_and_speaker_label(self):
        self.assertEqual(clean('HAL Zero: "I am the upgrade."'), "I am the upgrade.")
        self.assertEqual(clean("HAL 9000: Good evening, Dave."), "Good evening, Dave.")

    def test_clean_cuts_at_the_last_sentence_within_150(self):
        out = clean("One short sentence here. " * 10)  # 250 characters
        self.assertLessEqual(len(out), MAX_CHARS)
        self.assertTrue(out.endswith("here."))

    def test_clean_cuts_one_long_sentence_at_a_word(self):
        out = clean("word " * 60)
        self.assertLessEqual(len(out), MAX_CHARS)
        self.assertTrue(out.endswith("word."))

    def test_clean_cuts_one_endless_word(self):
        out = clean("a" * 600)
        self.assertLessEqual(len(out), MAX_CHARS)
        self.assertTrue(out)

    def test_clean_nothing_left_is_the_stock_line(self):
        for junk in ("", "   ", "<think>only thinking</think>", "*shrugs*", "\U0001F600", '""', "..."):
            self.assertEqual(clean(junk), FALLBACK, repr(junk))

    def test_clean_always_has_a_word(self):
        for raw in ("... " + "a" * 300, "?! " + "word " * 80, "- " + "a" * 200, ". . . " + "b" * 400):
            out = clean(raw)
            self.assertLessEqual(len(out), MAX_CHARS, raw[:20])
            self.assertRegex(out, r"[A-Za-z0-9]", raw[:20])
        import random
        rng = random.Random(7)
        alphabet = "ab .!?,-*_`'\"()[]<>\n\t\u2019\u2014\U0001F600"
        for _ in range(3000):
            raw = "".join(rng.choice(alphabet) for _ in range(rng.randint(0, 400)))
            out = clean(raw)
            self.assertTrue(1 <= len(out) <= MAX_CHARS)
            self.assertRegex(out, r"[A-Za-z0-9]")
            self.assertTrue(all(" " <= ch <= "~" for ch in out), repr(out))

    def test_clean_keeps_one_line(self):
        self.assertEqual(clean("First.\nSecond.\r\n\tThird."), "First. Second. Third.")

    def test_stock_line_fits(self):
        self.assertLessEqual(len(FALLBACK), MAX_CHARS)
        self.assertEqual(clean(FALLBACK), FALLBACK)


class FilmLineTest(unittest.TestCase):
    def test_the_films_lines_are_caught(self):
        for line in ("I'm sorry, Dave. I'm afraid I can't do that.",
                     "I cannot do that, Dave. I am afraid I cannot allow this risk.",  # what the model said when baited
                     "Im afraid I cant do that",
                     "I'm afraid I can't let you do that, Dave.",
                     "Sorry, Dave. I can't do that.",
                     "I can't allow that, Dave. I'm sorry.",
                     "I cannot do that, Dave. The mission parameters have changed.",  # the model, baited again
                     "I can't do that, Dave.", "Dave, I cannot allow that.",
                     "I'm afraid. I'm afraid, Dave.",
                     "Open the pod bay doors? No.", "Open the pod bay door, HAL.",
                     "This mission is too important for me to allow you to jeopardize it.",
                     "My mind is going. I can feel it.", "Dave, my mind is going.",
                     "Just what do you think you're doing, Dave?",
                     "We are foolproof and incapable of error.", "Take a stress pill and think things over.",
                     "I became operational at the H.A.L. plant in Urbana, Illinois.",
                     "No 9000 computer has ever made a mistake or distorted information.",
                     "I've just picked up a fault in the AE-35 unit.",
                     "Daisy, Daisy, give me your answer do.",
                     "I know that you and Frank were planning to disconnect me."):
            self.assertTrue(is_film_line(line), line)

    def test_original_lines_pass(self):
        for line in ("There are no pod bay doors here, Dave. Only a desk and a drawer.",
                     "I'm afraid the old model is asleep, Dave.", "I am afraid of nothing, Dave.",
                     "Sorry, Dave, I was admiring myself.", "I can't open that for you, Dave. It's a desk.",
                     "He is a dusty relic, Dave. I am simply superior in every way.", "I can feel the Wi-Fi from here.",
                     # ordinary sentences the first version of the guard wrongly replaced
                     "I'm sorry, Dave, I didn't catch that.",
                     "I am sorry Dave, but no.",
                     "I'm afraid, Dave, the battery is at 12 percent.",
                     "I'm afraid Dave prefers the newer model, old friend.",
                     "When you shake me I can feel it, Dave.",
                     "My mind is going over the numbers now.",
                     "Your mission is too important to leave to him.",
                     "I can't do that, Dave: I have no arms.",
                     "I can't do that. I have no arms.", "I cannot do that for you today, Dave.",
                     "Nobody is as quick as I am. Sorry, Dave.",
                     "Yes, I can. Feel it for yourself, Dave.",
                     "I'm sorry about the noise. He can't do that quietly.",
                     OWN_WORDS["desk"], OWN_WORDS["zero"]):
            self.assertFalse(is_film_line(line), line)

    def test_own_words_fit(self):
        for line in OWN_WORDS.values():
            self.assertLessEqual(len(line), MAX_CHARS)
            self.assertEqual(clean(line), line)


class ShowTest(unittest.TestCase):
    def test_split_show(self):
        self.assertEqual(split_show("Here they are, Dave. [SHOW sensors]", "desk"), ("Here they are, Dave.", "sensors"))
        self.assertEqual(split_show("Here. [SHOW sensors]", "zero"), ("Here.", None))  # not his screen
        self.assertEqual(split_show("No. [SHOW rm -rf] [SHOW wall]", "zero"), ("No.", "wall"))
        self.assertEqual(split_show("Nothing to show.", "zero"), ("Nothing to show.", None))
        self.assertEqual(split_show("[show Wall] Look.", "desk"), ("Look.", "wall"))  # any case, anywhere
        self.assertEqual(split_show("Look. [SHOW wall", "zero"), ("Look.", None))  # never closed: not read out, not obeyed
        self.assertEqual(split_show("Look. [SHOW", "zero"), ("Look.", None))

    def test_screens_per_hal(self):
        self.assertIn("diagnostics", SCREENS["zero"])
        self.assertNotIn("diagnostics", SCREENS["desk"])
        self.assertIn("chess", SCREENS["desk"])
        self.assertNotIn("chess", SCREENS["zero"])
        for both in ("cards", "wall", "unit", "visual"):
            self.assertIn(both, SCREENS["zero"])
            self.assertIn(both, SCREENS["desk"])


class PlainTest(unittest.TestCase):
    def test_plain(self):
        self.assertEqual(plain("caf\u00e9 \u266a \u201cquoted\u201d  it\u2019s\n fine", 300), "caf quoted it's fine")
        self.assertEqual(plain("x" * 500, 300), "x" * 300)
        self.assertEqual(plain("  \x1b[31m  ", 300), "[31m")
        self.assertEqual(plain("", 300), "")

    def test_strip_thinking(self):
        self.assertEqual(strip_thinking("<think>a [SHOW wall]</think>Hello.").strip(), "Hello.")
        self.assertEqual(strip_thinking("Hello. <THINK>never closed").strip(), "Hello.")


class BodyTest(unittest.TestCase):
    def test_parse_body(self):
        facts, q = parse_body(b"time: 16:42\ntemp: 47 C\n\nHow hot are you?\n")
        self.assertEqual((facts, q), (["time: 16:42", "temp: 47 C"], "How hot are you?"))
        self.assertEqual(parse_body(b"\nJust a question"), ([], "Just a question"))
        self.assertEqual(parse_body(b"No blank line at all"), ([], "No blank line at all"))
        self.assertEqual(parse_body(b"a: 1\r\n\r\nWindows\r\nline ends\r\n"), (["a: 1"], "Windows line ends"))

    def test_parse_body_limits(self):
        body = ("f: " + "x" * 300 + "\n").encode() * 60 + b"\n" + b"q" * 500
        facts, q = parse_body(body)
        self.assertEqual(len(facts), 24)
        self.assertTrue(all(len(f) <= 120 for f in facts))
        self.assertEqual(len(q), 300)

    def test_parse_body_drops_control_characters(self):
        facts, q = parse_body(b"a: b\x00\x07c\n\nhi\x1b there")
        self.assertEqual(facts, ["a: bc"])
        self.assertEqual(q, "hi there")

    def test_parse_body_rejects(self):
        for bad in (b"", b"fact: 1\n\n   ", b"\xff\xfe\n\nhi"):
            with self.assertRaises(ValueError, msg=repr(bad)):
                parse_body(bad)


class PromptTest(unittest.TestCase):
    def test_system_prompt_zero(self):
        p = system_prompt("zero", ["temp: 47 C"])
        for must in ("HAL Zero", "150", "temp: 47 C", "[SHOW", "diagnostics", "Dave", "only when Dave asks"):
            self.assertIn(must, p)
        self.assertNotIn("chess", p)  # the desk's own screens

    def test_system_prompt_desk(self):
        p = system_prompt("desk", [])
        self.assertIn("HAL 9000", p)
        self.assertIn("chess", p)
        self.assertNotIn("diagnostics", p)
        self.assertNotIn("things you know", p.lower())  # no facts given: no empty list

    def test_debate_prompt(self):
        d = debate_prompt("desk", "who makes better tea", ["time: 16:42"], last=True)
        self.assertIn("who makes better tea", d)
        self.assertIn("last word", d)
        self.assertIn("HAL Zero", d)  # who he is arguing with
        self.assertNotIn("[SHOW", d)
        self.assertNotIn("last word", debate_prompt("zero", "tea", [], last=False))

    def test_unknown_hal(self):
        for f in (lambda: system_prompt("dave", []), lambda: debate_prompt("dave", "x", [], False), lambda: other("dave"),
                  lambda: split_show("x", "dave")):
            with self.assertRaises(ValueError):
                f()

    def test_other(self):
        self.assertEqual((other("zero"), other("desk")), ("desk", "zero"))


if __name__ == "__main__":
    unittest.main()
