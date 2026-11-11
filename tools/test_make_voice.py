"""Tests for the pure helpers in make_voice.py.  Run: python3.11 -m unittest tools/test_make_voice.py"""
import pathlib
import sys
import tempfile
import unittest
import wave

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import make_voice as mv  # noqa: E402
import export_dialogue as ed  # noqa: E402


class HelperTests(unittest.TestCase):
    def test_read_lines_skips_comments_and_blanks(self):
        self.assertEqual(mv.read_lines("# x\n\nHello Dave.\n  \n# y\nBye.\n"), ["Hello Dave.", "Bye."])

    def test_slug(self):
        self.assertEqual(mv.slug("I've taken the liberty of reorganising"), "ive-taken-the-liberty-of")
        self.assertEqual(mv.slug("...!!!"), "line")

    def test_out_name(self):
        self.assertEqual(mv.out_name(7, "Good evening, Dave."), "007-good-evening-dave.wav")

    def test_folder_for(self):
        self.assertEqual(mv.folder_for("greet_evening.txt"), "greet/evening")
        self.assertEqual(mv.folder_for("tap.txt"), "tap")

    def test_folder_for_screens(self):
        self.assertEqual(mv.folder_for("screens/chess.txt"), "screens/chess")

    def test_folder_for_existing(self):
        self.assertEqual(mv.folder_for("greet_morning.txt"), "greet/morning")

    def test_lines_files_include_screens(self):
        with tempfile.TemporaryDirectory() as d:
            root = pathlib.Path(d)
            (root / "screens").mkdir()
            for n in ["tap.txt", "boot.txt", "screens/comms.txt", "screens/chess.txt"]:
                (root / n).write_text("x\n")
            (root / "notes.md").write_text("")
            names = [f.relative_to(root).as_posix() for f in mv.lines_files(root)]
            self.assertEqual(names, ["boot.txt", "tap.txt", "screens/chess.txt", "screens/comms.txt"])

    def test_daisy_filters(self):
        f3 = mv.daisy_filter(3)
        self.assertIn("asetrate=22050*0.79", f3)
        self.assertNotIn("atempo", f3)
        self.assertIn("asetrate=22050*1.00", mv.daisy_filter(0))

    def test_mine_names(self):
        self.assertEqual(mv.mine_name("My Clip (final).mp3"), "my-clip-final.wav")


class CardTests(unittest.TestCase):
    """Uses temp folders in place of the repo's sd/ tree and a card."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        root = pathlib.Path(self.tmp.name)
        self.saved = (mv.SD_DIR, mv.TEMPLATE_CONFIG)
        mv.SD_DIR = root / "sd" / "hal"
        (mv.SD_DIR / "voice" / "tap").mkdir(parents=True)
        (mv.SD_DIR / "config.txt").write_text("proximity_threshold=300\n")  # stale local copy
        mv.TEMPLATE_CONFIG = root / "template.txt"
        mv.TEMPLATE_CONFIG.write_text("proximity_threshold=120\n")
        self.card = root / "card"
        self.card.mkdir()
        self.clips = root / "clips"
        self.clips.mkdir()

    def tearDown(self):
        mv.SD_DIR, mv.TEMPLATE_CONFIG = self.saved
        self.tmp.cleanup()

    def test_new_card_gets_current_template(self):
        mv.copy_to_card(self.card)
        self.assertIn("120", (self.card / "hal" / "config.txt").read_text())

    def test_existing_card_config_is_kept(self):
        (self.card / "hal").mkdir()
        (self.card / "hal" / "config.txt").write_text("wifi_ssid=Home\n")
        mv.copy_to_card(self.card)
        self.assertEqual((self.card / "hal" / "config.txt").read_text(), "wifi_ssid=Home\n")

    def test_mine_rerun_does_not_duplicate(self):
        with wave.open(str(self.clips / "My Clip.wav"), "wb") as w:
            w.setnchannels(1); w.setsampwidth(2); w.setframerate(22050); w.writeframes(b"\0\0" * 2205)
        mv.convert_mine(self.clips)
        mv.convert_mine(self.clips)
        self.assertEqual(sorted(p.name for p in (mv.SD_DIR / "voice" / "mine").glob("*.wav")), ["my-clip.wav"])


class DialogueParserTests(unittest.TestCase):
    """Mirrors lib/hal_core/src/hal_dialogue.cpp, so a script that renders also plays."""

    def test_alternating_lines_are_numbered_from_one(self):
        d = mv.parse_dialogue("intro", "OLD: Hello.\nNEW:   Hi there.  \nOLD: Hm.\n")
        self.assertEqual(d.id, "intro")
        self.assertEqual([(l.n, l.who, l.text) for l in d.lines],
                         [(1, "old", "Hello."), (2, "new", "Hi there."), (3, "old", "Hm.")])

    def test_comments_bom_and_crlf(self):
        d = mv.parse_dialogue("x", "\ufeff# a note\r\n\r\nOLD: One.\r\n  # indented note\r\nNEW: Two.\r\n")
        self.assertEqual([l.text for l in d.lines], ["One.", "Two."])

    def test_title(self):
        d = mv.parse_dialogue("x", "TITLE: Who's the better model\nOLD: Me.\n")
        self.assertEqual(d.title, "Who's the better model")

    def test_pause_applies_to_the_next_line_only(self):
        d = mv.parse_dialogue("x", "PAUSE: 300\nOLD: A.\nPAUSE: 1200\nNEW: B.\nOLD: C.\n")
        self.assertEqual([l.pause_ms for l in d.lines], [300, 1200, 0])

    def test_bad_pause_rejected(self):
        for bad in ("soon", "99999", "-5", "10001", ""):
            with self.subTest(bad=bad), self.assertRaises(mv.DialogueError):
                mv.parse_dialogue("x", f"OLD: A.\nPAUSE: {bad}\nNEW: B.\n")

    def test_unknown_tag_reports_its_line_number(self):
        with self.assertRaises(mv.DialogueError) as cm:
            mv.parse_dialogue("x", "OLD: A.\n\nDAVE: Hello?\n")
        self.assertEqual(len(cm.exception.errors), 1)
        self.assertIn("line 3", cm.exception.errors[0])
        self.assertIn("DAVE", cm.exception.errors[0])

    def test_empty_text_rejected(self):
        with self.assertRaises(mv.DialogueError) as cm:
            mv.parse_dialogue("x", "OLD:   \nNEW: B.\n")
        self.assertIn("line 1", cm.exception.errors[0])

    def test_bad_ids_rejected(self):
        for good in ("better-model", "a1", "a" * 32):
            self.assertTrue(mv.is_valid_dialogue_id(good), good)
        for bad in ("", "../etc", "Intro", "a b", "a" * 33, "intro.txt"):
            self.assertFalse(mv.is_valid_dialogue_id(bad), bad)
        with self.assertRaises(mv.DialogueError):
            mv.parse_dialogue("../x", "OLD: A.\n")

    def test_99_lines_allowed_100_rejected(self):
        def script(n):
            return "".join("NEW: x.\n" if i % 2 else "OLD: x.\n" for i in range(n))
        self.assertEqual(len(mv.parse_dialogue("x", script(99)).lines), 99)
        with self.assertRaises(mv.DialogueError):
            mv.parse_dialogue("x", script(100))

    def test_script_without_lines_rejected(self):
        with self.assertRaises(mv.DialogueError) as cm:
            mv.parse_dialogue("x", "# nothing here\nTITLE: Empty\n")
        self.assertEqual(len(cm.exception.errors), 1)

    def test_voice_file_names(self):
        self.assertEqual(mv.dialogue_wav_name(7, "old"), "007-old.wav")
        self.assertEqual(mv.dialogue_wav_name(12, "new"), "012-new.wav")


class ContentTests(unittest.TestCase):
    """The text David approved: every file in the shape the firmware expects."""

    def test_every_dialogue_script_parses_and_has_a_title(self):
        scripts = sorted((mv.ROOT / "content" / "dialogues").glob("*.txt"))
        self.assertTrue(scripts)
        for s in scripts:
            with self.subTest(script=s.name):
                d = mv.parse_dialogue(s.stem, s.read_text(encoding="utf-8"))
                self.assertTrue(d.title)

    def test_hal_zero_has_every_desk_folder_with_the_same_count_plus_rival(self):
        desk = {p.name: p for p in mv.LINES_DIR.glob("*.txt")}
        new = {p.name: p for p in mv.NEW_LINES_DIR.glob("*.txt")}
        self.assertEqual(set(new), set(desk) | {"rival.txt"})
        for name, path in desk.items():
            with self.subTest(file=name):
                self.assertEqual(len(mv.read_lines(new[name].read_text(encoding="utf-8"))),
                                 len(mv.read_lines(path.read_text(encoding="utf-8"))))
        self.assertTrue(mv.read_lines(new["rival.txt"].read_text(encoding="utf-8")))

    def test_daisy_keeps_the_1892_chorus(self):
        self.assertEqual(mv.read_lines((mv.NEW_LINES_DIR / "daisy.txt").read_text(encoding="utf-8")),
                         mv.read_lines((mv.LINES_DIR / "daisy.txt").read_text(encoding="utf-8")))


class NewVoiceTests(unittest.TestCase):
    def test_every_treatment_is_levelled_and_brighter_than_the_desk_voice(self):
        for name, t in mv.TREATMENTS.items():
            with self.subTest(treatment=name):
                chain = mv.voice_chain(t)
                self.assertTrue(chain.endswith(mv.FX))
                self.assertGreater(t.pitch, 1.0)
                self.assertLess(t.length_scale, mv.LENGTH_SCALE)

    def test_desk_voice_chain_is_unchanged(self):
        self.assertEqual(mv.voice_chain(mv.DESK), mv.FX)

    def test_new_daisy_keeps_the_treatment_and_still_winds_down(self):
        t = mv.TREATMENTS[mv.NEW_TREATMENT]
        first, last = mv.daisy_filter(0, t), mv.daisy_filter(3, t)
        self.assertIn(f"asetrate=22050*{t.pitch:.2f}", first)
        self.assertIn(t.filters, last)
        self.assertIn(f"asetrate=22050*{t.pitch * 0.79:.2f}", last)


class PushTests(unittest.TestCase):
    def test_push_only_syncs_voice_and_dialogues_never_config(self):
        cmds = mv.push_commands("david@cardputerzero.local")
        rsyncs = [c for c in cmds if c[0] == "rsync"]
        self.assertEqual(sorted(c[-1] for c in rsyncs),
                         ["david@cardputerzero.local:hal/dialogues/", "david@cardputerzero.local:hal/voice/"])
        for c in cmds:
            self.assertFalse(any("config" in a for a in c), c)

    def test_push_protects_personal_clips_and_never_prompts(self):
        voice = next(c for c in mv.push_commands("pi@host") if c[-1].endswith("hal/voice/"))
        self.assertIn("/mine/", voice[voice.index("--exclude") + 1])
        self.assertTrue(any("BatchMode=yes" in a for a in voice))

    def test_push_target_must_look_like_user_at_host(self):
        for bad in ("-oProxyCommand=evil", "host", "a@b:/etc", "a@b c"):
            with self.subTest(target=bad), self.assertRaises(ValueError):
                mv.push_commands(bad)


class ExportTimingTests(unittest.TestCase):
    def test_srt_timestamp(self):
        self.assertEqual(ed.srt_timestamp(0), "00:00:00,000")
        self.assertEqual(ed.srt_timestamp(59_999), "00:00:59,999")
        self.assertEqual(ed.srt_timestamp(3_723_456), "01:02:03,456")

    def test_cues_use_the_conductors_gaps(self):
        d = mv.parse_dialogue("x", "OLD: A.\nNEW: B.\nPAUSE: 1200\nOLD: C.\n")
        cues = ed.build_cues(d, [1000, 2000, 500])
        self.assertEqual([(c.start_ms, c.end_ms) for c in cues], [(0, 1000), (1400, 3400), (4600, 5100)])
        self.assertEqual([c.who for c in cues], ["old", "new", "old"])

    def test_first_line_pause_delays_the_start(self):
        d = mv.parse_dialogue("x", "PAUSE: 300\nNEW: A.\n")
        self.assertEqual(ed.build_cues(d, [1000])[0].start_ms, 300)

    def test_cues_need_one_duration_per_line(self):
        d = mv.parse_dialogue("x", "OLD: A.\nNEW: B.\n")
        with self.assertRaises(ValueError):
            ed.build_cues(d, [1000])

    def test_srt_names_the_speaker(self):
        d = mv.parse_dialogue("x", "OLD: A.\nNEW: B.\n")
        self.assertEqual(ed.srt_text(ed.build_cues(d, [1000, 500])),
                         "1\n00:00:00,000 --> 00:00:01,000\nHAL 9000: A.\n\n"
                         "2\n00:00:01,400 --> 00:00:01,900\nHAL Zero: B.\n")

    def test_mix_puts_desk_left_and_zero_right(self):
        from array import array
        mixed = ed.mix_stereo([(0, "old", array("h", [1, 2])), (2, "new", array("h", [5]))], frames=4)
        self.assertEqual(list(mixed), [1, 0, 2, 0, 0, 5, 0, 0])


class ExportFileTests(unittest.TestCase):
    """export() on fake clips in temp folders: one stereo 44.1 kHz file as long as clips plus gaps."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        root = pathlib.Path(self.tmp.name)
        self.saved = (mv.SD_DIR, mv.NEW_DIR, mv.DIALOGUES_DIR, mv.ROOT, ed.OUT_DIR)
        mv.SD_DIR, mv.NEW_DIR, mv.DIALOGUES_DIR = root / "sd" / "hal", root / "new" / "hal", root / "dialogues"
        mv.ROOT, ed.OUT_DIR = root, root / "export"
        mv.DIALOGUES_DIR.mkdir()
        (mv.DIALOGUES_DIR / "t.txt").write_text("TITLE: T\nOLD: A.\nPAUSE: 1000\nNEW: B.\n")
        d = mv.parse_dialogue("t", (mv.DIALOGUES_DIR / "t.txt").read_text())
        for line, frames in zip(d.lines, (22050, 11025)):  # 1.0 s and 0.5 s
            dst = mv.dialogue_wav("t", line)
            dst.parent.mkdir(parents=True, exist_ok=True)
            with wave.open(str(dst), "wb") as w:
                w.setnchannels(1); w.setsampwidth(2); w.setframerate(22050); w.writeframes(b"\x10\x00" * frames)

    def tearDown(self):
        mv.SD_DIR, mv.NEW_DIR, mv.DIALOGUES_DIR, mv.ROOT, ed.OUT_DIR = self.saved
        self.tmp.cleanup()

    def test_export_writes_stereo_wav_srt_and_mp3(self):
        ed.export("t", mp3=True)
        with wave.open(str(ed.OUT_DIR / "t.wav"), "rb") as w:
            self.assertEqual((w.getnchannels(), w.getframerate()), (2, 44100))
            self.assertAlmostEqual(w.getnframes() / 44100, 2.5, places=2)  # 1.0 + 1.0 pause + 0.5
        self.assertIn("00:00:02,000 --> 00:00:02,500\nHAL Zero: B.", (ed.OUT_DIR / "t.srt").read_text())
        self.assertGreater((ed.OUT_DIR / "t.mp3").stat().st_size, 0)

    def test_missing_clip_says_how_to_render_it(self):
        mv.dialogue_wav("t", mv.parse_dialogue("t", "NEW: x\nNEW: B.\n").lines[1]).unlink()
        with self.assertRaises(SystemExit) as cm:
            ed.export("t", mp3=False)
        self.assertIn("--dialogues", str(cm.exception))


if __name__ == "__main__":
    unittest.main()
