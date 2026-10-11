"""Platform codec tests, including cross-check against firmware-generated vectors."""
import json
import os
import struct
import unittest

import codec

VECTORS = os.path.join(os.path.dirname(__file__), "..", "firmware", "test", "payload_vectors.json")


def rec(rail, air, flags):
    return struct.pack(">hhB", rail, air, flags)


class FirmwareVectors(unittest.TestCase):
    """Every firmware vector must decode to exactly the fields the firmware claims."""

    @classmethod
    def setUpClass(cls):
        with open(VECTORS) as f:
            cls.vectors = json.load(f)

    def test_vectors_present_for_every_uplink_type(self):
        types = {bytes.fromhex(v["hex"])[0] for v in self.vectors}
        self.assertTrue({0x01, 0x02, 0x03, 0x04} <= types, types)

    def test_fields_match(self):
        for v in self.vectors:
            with self.subTest(v["name"]):
                b = bytes.fromhex(v["hex"])
                if b[0] & 0x80:            # downlink vectors (if any) are checked separately
                    continue
                got = codec.decode_uplink(b)
                for k, want in v["fields"].items():
                    if k == "records":
                        for g, w in zip(got["records"], want):
                            for rk, rv in w.items():
                                self.assertEqual(g[rk], rv, f"{v['name']} record.{rk}")
                    elif k == "record":
                        for rk, rv in want.items():
                            self.assertEqual(got["record"][rk], rv, f"{v['name']} record.{rk}")
                    else:
                        self.assertEqual(got[k], want, f"{v['name']}.{k}")

    def test_downlink_config_vector_matches_encoder(self):
        for v in self.vectors:
            b = bytes.fromhex(v["hex"])
            if b[0] != 0x81:
                continue
            f = v["fields"]
            mine = codec.encode_config(f["l1"], f["l2"], f["l3"], f["hot_threshold"],
                                       f["rate_of_rise"], f["hysteresis"], f["batch_period_min"],
                                       f["cool_threshold"], f["cool_batch_min"], f["hot_batch_min"])
            self.assertEqual(mine, b)


class Decode(unittest.TestCase):
    def test_batch_times_and_fault(self):
        b = struct.pack(">BHIBB", 1, 7, 1_700_000_000, 10, 2) + rec(5512, 3020, 0x07) + \
            rec(codec.NA, 3100, 0x43)
        m = codec.decode_uplink(b)
        self.assertEqual(m["records"][0]["rail_c"], 55.12)
        self.assertEqual(m["records"][1]["time"], 1_700_000_600)
        self.assertIsNone(m["records"][1]["rail_c"])
        self.assertTrue(m["records"][1]["sensor_fault"])
        self.assertEqual(m["records"][0]["alarm_level"], 0)

    def test_alarm_level_bits(self):
        r = codec.decode_record(rec(6100, 3000, 0b0001_1011))
        self.assertEqual(r["alarm_level"], 3)
        self.assertTrue(r["attached"] and r["lid_closed"])

    def test_length_errors(self):
        with self.assertRaises(ValueError):
            codec.decode_uplink(struct.pack(">BHIBB", 1, 1, 0, 10, 2) + rec(0, 0, 0))
        with self.assertRaises(ValueError):
            codec.decode_uplink(b"\x02" + b"\x00" * 5)
        with self.assertRaises(ValueError):
            codec.decode_uplink(b"\x7f")


class Config(unittest.TestCase):
    SITE = dict(l1=5500, l2=6000, l3=6500, hot=3500, rate_of_rise=800, hysteresis=100,
                batch_period_min=60, cool_threshold=2500, cool_batch_min=240, hot_batch_min=20)

    def test_example_site_config_is_18_bytes(self):
        self.assertEqual(len(codec.encode_config(**self.SITE)), 18)

    def test_rejects_invalid(self):
        for k, bad in (("l2", 5500), ("hysteresis", 501), ("batch_period_min", 15),
                       ("cool_batch_min", 30), ("hot_batch_min", 60), ("cool_threshold", 3500)):
            with self.subTest(k), self.assertRaises(ValueError):
                codec.encode_config(**{**self.SITE, k: bad})


class Ingest(unittest.TestCase):
    def batch(self, seq, base, n):
        b = struct.pack(">BHIBB", 1, seq, base, 10, n)
        return b + b"".join(rec(4000 + i, 3000, 3) for i in range(n))

    def test_overlapping_lora_batches_and_duplicates(self):
        s = codec.SiteStream()
        t0 = 1_700_000_000
        for i in range(1, 6):                       # n=2 batches overlap by one record
            self.assertTrue(s.ingest(self.batch(i, t0 + (i - 1) * 600, 2)))
        self.assertFalse(s.ingest(self.batch(3, t0 + 1200, 2)))   # duplicate delivery
        self.assertEqual(s.gaps(t0, t0 + 5 * 600), [])

    def test_gap_detection(self):
        s = codec.SiteStream()
        t0 = 1_700_000_000
        s.ingest(self.batch(1, t0, 2))
        s.ingest(self.batch(2, t0 + 3 * 600, 2))
        self.assertEqual(s.gaps(t0, t0 + 4 * 600), [t0 + 2 * 600])


if __name__ == "__main__":
    unittest.main()
