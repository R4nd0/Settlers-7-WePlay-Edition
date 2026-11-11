"""Authored gathering records; no game data, sessions or identity admission."""
from dataclasses import replace
from pathlib import Path
import random
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools/gold"))
import gathering_codec as codec
from login_server import Reader


class GatheringTests(unittest.TestCase):
    def setUp(self):
        self.value = codec.Gathering((1, 2, 3, 4, 5, 6, 7), (8, 9), "Local", b"attributes")

    def test_scalar_order_and_raw_attribute_tail(self):
        # Native base writer order: +04,+08,+0c,+10(u16),+12(u16),+14..+20.
        # The dynamic buffer follows the description without another length.
        literal = bytes.fromhex(
            "110044796e616d6963476174686572696e67003600000032000000"
            "0100000002000000030000000800090004000000050000000600000007000000"
            "06004c6f63616c0061747472696275746573")
        self.assertEqual(codec.encode(self.value), literal)
        self.assertEqual(codec.decode(literal, Reader), self.value)

    def test_empty_unicode_and_bounded_attribute_roundtrips(self):
        for description in (None, "", "WePlay", "Grüße 水"):
            for size in (0, 1, 140, codec.MAX_ATTRIBUTES):
                value = replace(self.value, description=description, attributes=b"x" * size)
                self.assertEqual(codec.decode(codec.encode(value), Reader), value)

    def test_lengths_classes_terminators_and_utf8_are_exact(self):
        data = codec.encode(self.value)
        size_offset = 19
        for end in range(len(data)):
            with self.assertRaises((ValueError, UnicodeError)):
                codec.decode(data[:end], Reader)
        for size in (0, 33, 0xffffffff, len(data)):
            invalid = data[:size_offset] + struct.pack("<I", size) + data[size_offset + 4:]
            with self.assertRaises(ValueError):
                codec.decode(invalid, Reader)
        for invalid in (data + b"trailing", data.replace(b"DynamicGathering", b"UnknownGathering"),
                        data[:18] + b"x" + data[19:], data[:61] + b"\xff" + data[62:]):
            with self.assertRaises((ValueError, UnicodeError)):
                codec.decode(invalid, Reader)

    def test_field_bounds_do_not_truncate_or_normalize(self):
        for value in (replace(self.value, words=(-1,) * 7),
                      replace(self.value, words=(0x100000000,) * 7),
                      replace(self.value, shorts=(0x10000, 1)),
                      replace(self.value, description="x" * 1025),
                      replace(self.value, description="embedded\0text"),
                      replace(self.value, attributes=b"x" * 8193)):
            with self.assertRaises(ValueError):
                codec.encode(value)
        value = replace(self.value, words=(0xffffffff,) * 7, shorts=(0xffff, 0xffff))
        self.assertEqual(codec.decode(codec.encode(value), Reader), value)

    def test_nested_stream_length_and_null_string_are_distinct(self):
        null = replace(self.value, description=None, attributes=b"")
        empty = replace(null, description="")
        self.assertNotEqual(codec.encode(null), codec.encode(empty))
        for value in (null, empty):
            data = codec.encode(value)
            self.assertEqual(codec.decode(data, Reader), value)
            for size in (0, len(data), 0xffffffff):
                bad = data[:23] + struct.pack("<I", size) + data[27:]
                with self.assertRaises(ValueError):
                    codec.decode(bad, Reader)

    def test_malformed_input_is_bounded(self):
        randomizer = random.Random(710111)
        for _ in range(10000):
            value = randomizer.randbytes(randomizer.randrange(256))
            try:
                result = codec.decode(value, Reader)
            except (ValueError, UnicodeError):
                continue
            base = result.base if isinstance(result, codec.SessionGathering) else result
            self.assertLessEqual(len(base.attributes), codec.MAX_ATTRIBUTES)
        with self.assertRaises(ValueError):
            codec.decode(b"x" * (codec.MAX_RECORD + 1), Reader)

    def test_native_s7_session_variants_and_exact_boolean_domain(self):
        base = replace(self.value, attributes=b"")
        session = codec.SessionGathering(base, ("map", "hash", "template", "host"),
            (1, 2, 3, 4, 5, 6), (False, True, False, True))
        for value in (session, replace(session, matchmaking_word=0xffffffff)):
            data = codec.encode(value)
            self.assertEqual(codec.decode(data, Reader), value)
            for end in range(len(data)):
                with self.assertRaises((ValueError, UnicodeError)):
                    codec.decode(data[:end], Reader)
        data = codec.encode(session)
        with self.assertRaises(ValueError):
            codec.decode(data[:-1] + b"\xff", Reader)
        with self.assertRaises(ValueError):
            codec.encode(replace(session, flags=(False, True, 2, True)))
        with self.assertRaises(ValueError):
            codec.encode(replace(session, base=self.value))


if __name__ == "__main__":
    unittest.main()
