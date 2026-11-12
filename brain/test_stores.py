import os
import re
import tempfile
import unittest

from brain.clips import ClipStore
from brain.config import read_env
from brain.memory import ChatMemory


class Clock:
    def __init__(self):
        self.now = 1000.0

    def __call__(self):
        return self.now


class ClipStoreTest(unittest.TestCase):
    def test_clip_round_trip(self):
        store = ClipStore()
        a, b = store.put(b"RIFFaaaa"), store.put(b"RIFFbbbb")
        self.assertRegex(a, r"^[0-9a-f]{32}$")
        self.assertNotEqual(a, b)
        self.assertEqual(store.get(a), b"RIFFaaaa")
        self.assertEqual(store.get(b), b"RIFFbbbb")
        self.assertEqual(store.get(a), b"RIFFaaaa")  # fetching doesn't use it up: a retry must work

    def test_clip_expires(self):
        clock = Clock()
        store = ClipStore(ttl_s=600, clock=clock)
        token = store.put(b"x")
        clock.now += 599
        self.assertEqual(store.get(token), b"x")
        clock.now += 2
        self.assertIsNone(store.get(token))

    def test_clip_unknown_or_malformed(self):
        store = ClipStore()
        store.put(b"x")
        for bad in ("../etc/passwd", "", "z" * 32, "0" * 32, "0" * 31, None, 7):
            self.assertIsNone(store.get(bad), repr(bad))

    def test_clip_store_caps_count(self):
        store = ClipStore(max_clips=3)
        tokens = [store.put(bytes([i])) for i in range(4)]
        self.assertIsNone(store.get(tokens[0]))
        self.assertEqual([store.get(t) for t in tokens[1:]], [b"\x01", b"\x02", b"\x03"])

    def test_clip_store_caps_bytes(self):
        store = ClipStore(max_bytes=100)
        tokens = [store.put(bytes([i]) * 40) for i in range(3)]
        self.assertIsNone(store.get(tokens[0]))
        self.assertIsNotNone(store.get(tokens[1]))
        self.assertIsNotNone(store.get(tokens[2]))

    def test_expired_clips_free_their_room(self):
        clock = Clock()
        store = ClipStore(ttl_s=10, max_clips=2, clock=clock)
        store.put(b"a")
        store.put(b"b")
        clock.now += 11
        fresh = [store.put(b"c"), store.put(b"d")]
        self.assertEqual([store.get(t) for t in fresh], [b"c", b"d"])


class ChatMemoryTest(unittest.TestCase):
    def test_memory_keeps_last_12(self):
        memory = ChatMemory()
        for i in range(8):
            memory.add("zero", f"q{i}", f"a{i}")
        history = memory.history("zero")
        self.assertEqual(len(history), 12)
        self.assertEqual(history[0], {"role": "user", "content": "q2"})
        self.assertEqual(history[-1], {"role": "assistant", "content": "a7"})
        self.assertEqual([m["role"] for m in history], ["user", "assistant"] * 6)

    def test_memory_is_per_hal(self):
        memory = ChatMemory()
        memory.add("zero", "q", "a")
        self.assertEqual(memory.history("desk"), [])
        self.assertEqual(len(memory.history("zero")), 2)

    def test_memory_forgets_after_idle(self):
        clock = Clock()
        memory = ChatMemory(idle_s=600, clock=clock)
        memory.add("desk", "q", "a")
        clock.now += 599
        self.assertEqual(len(memory.history("desk")), 2)
        clock.now += 2
        self.assertEqual(memory.history("desk"), [])
        memory.add("desk", "q2", "a2")  # a new conversation starts clean
        self.assertEqual(memory.history("desk")[0]["content"], "q2")

    def test_history_is_a_copy(self):
        memory = ChatMemory()
        memory.add("zero", "q", "a")
        memory.history("zero").clear()
        self.assertEqual(len(memory.history("zero")), 2)


class EnvTest(unittest.TestCase):
    def test_read_env(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, ".env")
            with open(path, "w") as f:
                f.write("# a comment\n\nHAL_BRAIN_PORT=9100\nQUOTED=\"two words\"\nSINGLE='x'\n"
                        "WITH_EQUALS=a=b=c\n  SPACED = padded  \n# OFF=1\nnot a setting\nEMPTY=\n")
            env = read_env(path)
        self.assertEqual(env, {"HAL_BRAIN_PORT": "9100", "QUOTED": "two words", "SINGLE": "x",
                               "WITH_EQUALS": "a=b=c", "SPACED": "padded", "EMPTY": ""})

    def test_read_env_missing_file(self):
        self.assertEqual(read_env("/nonexistent/hal/.env"), {})


if __name__ == "__main__":
    unittest.main()
