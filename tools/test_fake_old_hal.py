import importlib.util
import io
import pathlib
import unittest
import wave

spec = importlib.util.spec_from_file_location("fake_old_hal", pathlib.Path(__file__).with_name("fake_old_hal.py"))
fake = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fake)

TOKEN = "0123456789abcdef0123456789abcdef"


def make_wav(seconds=1.0, rate=22050, width=2) -> bytes:
    out = io.BytesIO()
    with wave.open(out, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(width)
        w.setframerate(rate)
        w.writeframes(b"\x00" * int(seconds * rate) * width)
    return out.getvalue()


class FakeDeskHalTest(unittest.TestCase):
    def test_hello_version(self):
        self.assertEqual([fake.hello_version(m) for m in ("old", "v2", "ok", "nofetch", "freeze")], [1, 2, 3, 3, 3])

    def test_private_addresses_only(self):
        for yes in ("10.0.0.1", "172.16.0.14", "172.31.255.254", "192.168.1.20"):
            self.assertTrue(fake.is_private_ipv4(yes), yes)
        for no in ("8.8.8.8", "127.0.0.1", "172.32.0.1", "10.0.0", "10.0.0.256", "010.0.0.1", "a.b.c.d", ""):
            self.assertFalse(fake.is_private_ipv4(no), no)

    def test_parse_fetch(self):
        self.assertEqual(fake.parse_fetch(f"FETCH 2 192.168.1.20 9100 {TOKEN}".split()),
                         (2, "192.168.1.20", 9100, TOKEN))
        for bad in (f"FETCH 2 8.8.8.8 9100 {TOKEN}", f"FETCH 2 192.168.1.20 80 {TOKEN}", f"FETCH 0 192.168.1.20 9100 {TOKEN}",
                    "FETCH 2 192.168.1.20 9100 short", f"FETCH 2 192.168.1.20 9100 {TOKEN.upper()}",
                    f"FETCH 2 192.168.1.20 9100 {TOKEN} extra", "FETCH 2 192.168.1.20 9100", f"SAY 2 192.168.1.20 9100 {TOKEN}"):
            self.assertIsNone(fake.parse_fetch(bad.split()), bad)

    def test_wav_ms(self):
        self.assertEqual(fake.wav_ms(make_wav(1.0)), 1000)
        self.assertEqual(fake.wav_ms(make_wav(2.5, rate=16000)), 2500)
        for junk in (b"", b"hello", make_wav(1.0)[:30], make_wav(1.0, width=1)):
            self.assertEqual(fake.wav_ms(junk), 0)

    def test_download_gives_up_quietly(self):
        self.assertEqual(fake.download("127.0.0.1", 9, TOKEN), 0)  # nothing listens on the discard port

    def test_download_from_something_that_is_not_a_web_server(self):
        import socket
        import threading
        listener = socket.create_server(("127.0.0.1", 0))

        def serve():
            conn, _ = listener.accept()
            conn.recv(65536)
            conn.sendall(b"SSH-2.0-OpenSSH_9.9\r\n")
            conn.close()
            listener.close()

        threading.Thread(target=serve, daemon=True).start()
        self.assertEqual(fake.download("127.0.0.1", listener.getsockname()[1], TOKEN), 0)

    def test_flood_mode_pours_lines_out(self):
        import asyncio
        import contextlib
        import io as _io

        async def run():
            server = await asyncio.start_server(lambda r, w: fake.serve(r, w, "flood", 1500), "127.0.0.1", 0)
            reader, writer = await asyncio.open_connection("127.0.0.1", server.sockets[0].getsockname()[1])
            first = await asyncio.wait_for(reader.readline(), 2)
            lines = [await asyncio.wait_for(reader.readline(), 2) for _ in range(500)]  # far more than HAL Zero takes
            writer.close()
            server.close()
            await server.wait_closed()
            return first, lines

        with contextlib.redirect_stdout(_io.StringIO()) as said:
            first, lines = asyncio.run(run())
        self.assertEqual(first, b"HELLO OLD 3\n")
        self.assertEqual(set(lines), {b"PONG\n"})
        self.assertLess(len(said.getvalue().splitlines()), 20)  # it doesn't print every line it pours out


def make_wav8(seconds=1.0, rate=11025) -> bytes:
    """Unsigned 8-bit mono, as the brain renders HAL Card's lines."""
    out = io.BytesIO()
    with wave.open(out, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(1)
        w.setframerate(rate)
        w.writeframes(b"\x80" * int(seconds * rate))
    return out.getvalue()


class FriendsTest(unittest.TestCase):
    """The same stand-in playing HAL Nano, HAL Paper or HAL Card (--as)."""

    def test_who_says_what_on_connecting(self):
        self.assertEqual(fake.hello_lines("desk", "ok"), ["HELLO OLD 3"])
        self.assertEqual(fake.hello_lines("desk", "v2"), ["HELLO OLD 2"])
        self.assertEqual(fake.hello_lines("nano", "ok"), ["HELLO OLD 4", "IAM nano"])
        self.assertEqual(fake.hello_lines("paper", "ok"), ["HELLO OLD 4", "IAM paper"])
        self.assertEqual(fake.hello_lines("card", "busy"), ["HELLO OLD 4", "IAM card"])

    def test_who_has_a_voice(self):
        self.assertEqual([fake.voiced(w) for w in ("desk", "nano", "paper", "card")], [True, False, False, True])

    def test_now(self):
        self.assertEqual(fake.parse_now("NOW 3 paper Noted. For the record."), (3, "paper", "Noted. For the record."))
        self.assertEqual(fake.parse_now("NOW 12 zero Two  spaces inside."), (12, "zero", "Two  spaces inside."))
        self.assertEqual(fake.parse_now("NOW 99 card " + "x" * 150), (99, "card", "x" * 150))
        for bad in ("NOW", "NOW 1 nano", "NOW 0 nano Hi.", "NOW 100 nano Hi.", "NOW 1 dave Hi.", "NOW  1 nano Hi.",
                    "NOW 1 nano  Leading.", "NOW 1 nano Trailing. ", "NOW 1 nano Caf\u00e9.", "NOW 1 nano " + "x" * 151,
                    "SAY live 1"):
            self.assertIsNone(fake.parse_now(bad), bad)

    def test_how_long_a_line_of_words_takes(self):
        # The same sums as the firmware (lib/hal_core: hal_chirp, hal_minutes), so the timing is true to life.
        self.assertEqual(fake.chirp_ms("Hi."), 2500)            # the floor
        self.assertEqual(fake.chirp_ms("a" * 80), 80 * 52)      # a tone and a gap a letter
        self.assertEqual(fake.chirp_ms("ab cd"), 2500)
        self.assertEqual(fake.chirp_ms("a" * 60 + "? " + "b" * 20), 60 * 52 + 270 + 60 + 20 * 52)
        self.assertEqual(fake.reading_ms("Noted."), 4000)       # the floor
        self.assertEqual(fake.reading_ms("x" * 100), 7000)
        self.assertEqual(fake.reading_ms("x" * 150), 9750)
        self.assertEqual(fake.words_ms("nano", "a" * 80), 4160)
        self.assertEqual(fake.words_ms("paper", "x" * 100), 7000)

    def test_card_takes_small_8_bit_clips_only(self):
        self.assertEqual(fake.clip_ms("card", make_wav8(2.0)), 2000)
        self.assertEqual(fake.clip_ms("card", make_wav(1.0)), 0)                 # 16-bit: not what he is sent
        self.assertEqual(fake.clip_ms("card", make_wav8(9.5)), 0)                # 104,769 bytes: over his 96 KB
        self.assertEqual(fake.clip_ms("card", make_wav8(8.8)), 8800)
        self.assertEqual(fake.clip_ms("desk", make_wav(1.0)), 1000)              # the desk HAL is as he was
        self.assertEqual(fake.clip_ms("desk", make_wav8(1.0)), 0)

    def test_each_has_his_own_readings(self):
        for who in ("desk", "nano", "paper", "card"):
            self.assertTrue(fake.state_for(who).startswith("STATE up="), who)
            self.assertTrue(fake.sensors_for(who).startswith("SENSORS light="), who)
            self.assertLessEqual(len(fake.state_for(who)), 128)
        self.assertIn("light=-1", fake.sensors_for("nano"))    # no light sensor on the wrist
        self.assertIn("light=67", fake.sensors_for("desk"))
        self.assertEqual(len({fake.sensors_for(w) for w in ("desk", "nano", "paper", "card")}), 4)


if __name__ == "__main__":
    unittest.main()
