"""Platform-side codec for the Tier 1 payload format v0 (see firmware/README.md).

Written independently of the firmware from the specification, and cross-checked
against the firmware's generated test vectors (test_codec.py).
"""
import struct

NA = -32768  # 0x8000: not available / sensor fault


def _t(v):
    return None if v == NA else v / 100.0


def decode_record(b: bytes) -> dict:
    rail, air, flags = struct.unpack(">hhB", b)
    return {"rail_cC": rail, "air_cC": air, "flags": flags,
            "rail_c": _t(rail), "air_c": _t(air),
            "attached": bool(flags & 0x01), "lid_closed": bool(flags & 0x02),
            "hot_mode": bool(flags & 0x04), "alarm_level": (flags >> 3) & 0x03,
            "low_battery": bool(flags & 0x20), "sensor_fault": bool(flags & 0x40)}


def decode_uplink(b: bytes) -> dict:
    if not b:
        raise ValueError("empty payload")
    t = b[0]
    if t == 0x01:
        if len(b) < 9:
            raise ValueError("short batch header")
        _, seq, base, interval, n = struct.unpack(">BHIBB", b[:9])
        if len(b) != 9 + 5 * n:
            raise ValueError(f"batch length {len(b)} != {9 + 5 * n}")
        recs = [decode_record(b[9 + 5 * i: 14 + 5 * i]) for i in range(n)]
        for i, r in enumerate(recs):
            r["time"] = base + i * interval * 60
        return {"type": t, "seq": seq, "base_time": base, "interval_min": interval, "n": n,
                "records": recs}
    if t == 0x02:
        if len(b) != 13:
            raise ValueError("alarm length")
        _, seq, time, code = struct.unpack(">BHIB", b[:8])
        return {"type": t, "seq": seq, "time": time, "code": code, "record": decode_record(b[8:13])}
    if t == 0x03:
        if len(b) != 15:
            raise ValueError("status length")
        (_, seq, time, batt, energy, fw, resets, txfail, lq) = struct.unpack(">BHIBHHBBB", b)
        return {"type": t, "seq": seq, "time": time, "battery_pct": batt, "energy_mAh": energy,
                "fw_version": fw, "reset_count": resets, "tx_fail_count": txfail,
                "link_quality": lq}
    if t == 0x04:
        if len(b) != 12:
            raise ValueError("install length")
        (_, seq, time, hw, fw, att, batt) = struct.unpack(">BHIBHBB", b)
        return {"type": t, "seq": seq, "time": time, "hw_rev": hw, "fw_version": fw,
                "attached": att, "battery_pct": batt}
    raise ValueError(f"unknown uplink type 0x{t:02x}")


def encode_config(l1, l2, l3, hot, rate_of_rise, hysteresis, batch_period_min,
                  cool_threshold, cool_batch_min, hot_batch_min) -> bytes:
    """All temperatures in centi-degC. Validates as the device does."""
    if not (l1 < l2 < l3):
        raise ValueError("levels must satisfy L1 < L2 < L3")
    if hysteresis > 500:
        raise ValueError("hysteresis > 5 degC")
    if batch_period_min not in (10, 20, 30, 60):
        raise ValueError("batch_period_min")
    if cool_batch_min not in (60, 120, 240):
        raise ValueError("cool_batch_min")
    if hot_batch_min not in (10, 20, 30):
        raise ValueError("hot_batch_min")
    if not cool_threshold < hot:
        raise ValueError("cool_threshold must be below hot_threshold")
    return struct.pack(">BhhhhHHBhBB", 0x81, l1, l2, l3, hot, rate_of_rise, hysteresis,
                       batch_period_min, cool_threshold, cool_batch_min, hot_batch_min)


class SiteStream:
    """Ingest for one device: de-duplicates by seq, merges back-filled records by time,
    and reports gaps in the 10-minute series."""

    def __init__(self):
        self.seen_seq = set()
        self.records = {}          # time -> record
        self.alarms = []

    def ingest(self, payload: bytes) -> bool:
        msg = decode_uplink(payload)
        key = (msg["type"], msg["seq"], msg.get("time", msg.get("base_time")))
        if key in self.seen_seq:
            return False           # duplicate delivery
        self.seen_seq.add(key)
        if msg["type"] == 0x01:
            for r in msg["records"]:
                self.records.setdefault(r["time"], r)
        elif msg["type"] == 0x02:
            self.alarms.append(msg)
        return True

    def gaps(self, start, end, step=600):
        return [t for t in range(start, end + 1, step) if t not in self.records]
