import unittest

from brain.persona import (FALLBACK, MAX_CHARS, SCREENS, clean, debate_prompt, other, parse_body, split_show,
                           system_prompt)


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

    def test_clean_keeps_one_line(self):
        self.assertEqual(clean("First.\nSecond.\r\n\tThird."), "First. Second. Third.")

    def test_stock_line_fits(self):
        self.assertLessEqual(len(FALLBACK), MAX_CHARS)
        self.assertEqual(clean(FALLBACK), FALLBACK)


class ShowTest(unittest.TestCase):
    def test_split_show(self):
        self.assertEqual(split_show("Here they are, Dave. [SHOW sensors]", "desk"), ("Here they are, Dave.", "sensors"))
        self.assertEqual(split_show("Here. [SHOW sensors]", "zero"), ("Here.", None))  # not his screen
        self.assertEqual(split_show("No. [SHOW rm -rf] [SHOW wall]", "zero"), ("No.", "wall"))
        self.assertEqual(split_show("Nothing to show.", "zero"), ("Nothing to show.", None))
        self.assertEqual(split_show("[show Wall] Look.", "desk"), ("Look.", "wall"))  # any case, anywhere

    def test_screens_per_hal(self):
        self.assertIn("diagnostics", SCREENS["zero"])
        self.assertNotIn("diagnostics", SCREENS["desk"])
        self.assertIn("chess", SCREENS["desk"])
        self.assertNotIn("chess", SCREENS["zero"])
        for both in ("cards", "wall", "unit", "visual"):
            self.assertIn(both, SCREENS["zero"])
            self.assertIn(both, SCREENS["desk"])


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
        self.assertEqual(len(facts), 40)
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
