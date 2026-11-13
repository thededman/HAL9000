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


if __name__ == "__main__":
    unittest.main()
