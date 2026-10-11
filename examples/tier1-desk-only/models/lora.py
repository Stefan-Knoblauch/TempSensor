"""LoRa airtime, duty cycle and rail-level link budget (EU868, BW125).

Airtime: Semtech AN1200.13. Path loss: two-ray ground reflection beyond the
crossover distance (valid here: node antenna ~0.15 m above ballast, so the
crossover is ~33 m), plus a fixed excess loss for rail-head shadowing.
"""
import math

import params as p
from common import Check, md_table, checks_table


def airtime_s(sf: int, payload_b: int) -> float:
    bw = p.LORA_BW_HZ.value
    tsym = (2 ** sf) / bw
    de = 1 if sf >= 11 else 0                     # low data-rate optimise at BW125
    crc, h = 1, 0                                 # CRC on, explicit header
    num = 8 * payload_b - 4 * sf + 28 + 16 * crc - 20 * h
    n_payload = 8 + max(math.ceil(num / (4 * (sf - 2 * de))) * (p.LORA_CR.value + 4), 0)
    return (p.LORA_PREAMBLE_SYM.value + 4.25) * tsym + n_payload * tsym


def phy_payload_b() -> int:
    return int(p.APP_PAYLOAD_B.value + p.LORAWAN_OVERHEAD_B.value)


def charge_per_uplink_mah(sf: int) -> float:
    """TX + two empty RX windows + processing, in mAh."""
    bw = p.LORA_BW_HZ.value
    tx = p.LORA_TX_MA.value * airtime_s(sf, phy_payload_b())
    # RX1 at the uplink SF, RX2 at the network's RX2 data rate (EU868 default SF12)
    rx_sym = p.LORA_RX_WINDOW_SYM.value
    rx = p.LORA_RX_MA.value * rx_sym * ((2 ** sf) + (2 ** int(p.LORA_RX2_SF.value))) / bw
    return (tx + rx + p.LORA_WAKE_MAS.value) / 3600.0


def path_loss_db(d_m: float) -> float:
    lam = 3e8 / p.FREQ_HZ.value
    ht, hr = p.NODE_HEIGHT_M.value, p.GW_HEIGHT_M.value
    d_cross = 4 * math.pi * ht * hr / lam
    if d_m <= d_cross:                            # free space below crossover
        return 20 * math.log10(4 * math.pi * d_m / lam)
    return 40 * math.log10(d_m) - 20 * math.log10(ht) - 20 * math.log10(hr)


def max_range_m(sf: int, gw_height_m: float = None) -> float:
    if gw_height_m is not None:                   # what-if on gateway height
        delta = 20 * math.log10(gw_height_m / p.GW_HEIGHT_M.value)
    else:
        delta = 0.0
    budget = (p.TX_ERP_DBM.value + 2.15          # ERP -> EIRP
              + p.NODE_ANT_GAIN_DBI.value + p.GW_ANT_GAIN_DBI.value
              - p.GW_CABLE_LOSS_DB.value - p.LORA_SENS_DBM[sf]
              - p.EXCESS_LOSS_DB.value - p.FADE_MARGIN_DB.value + delta)
    lo, hi = 1.0, 1e5                             # bisection on path loss
    for _ in range(100):
        mid = (lo + hi) / 2
        lo, hi = (mid, hi) if path_loss_db(mid) < budget else (lo, mid)
    return lo


def run():
    per_day = 24 * 60 / p.SAMPLE_INTERVAL_MIN.value
    rows, checks = [], []
    for sf in range(7, 13):
        t = airtime_s(sf, phy_payload_b())
        day = t * per_day
        rows.append((f"SF{sf}", f"{t*1000:.1f}", f"{day:.1f}", f"{100*day/86400:.3f}",
                     "yes" if day <= p.FAIR_USE_S_PER_DAY.value else "no",
                     f"{charge_per_uplink_mah(sf)*1000:.1f}",
                     f"{max_range_m(sf):.0f}", f"{max_range_m(sf, 15.0):.0f}"))
        checks.append(Check("T1-16", f"EU868 duty cycle at SF{sf}, 144 uplinks/day",
                            f"{100*day/86400:.3f} %", "<= 1 %",
                            day / 86400 <= p.DUTY_CYCLE_LIMIT.value))
    table = md_table(["SF", "Airtime (ms)", "Airtime/day (s)", "Duty (%)",
                      "Within 30 s/day fair use", "Charge/uplink (µAh)",
                      "Range, 6 m GW (m)", "Range, 15 m GW (m)"], rows)
    md = (f"### LoRa airtime, duty cycle and range\n\nPHY payload {phy_payload_b()} B "
          f"(app {int(p.APP_PAYLOAD_B.value)} B + LoRaWAN {int(p.LORAWAN_OVERHEAD_B.value)} B), "
          f"{int(per_day)} uplinks/day.\n\n{table}\n\n"
          "Range includes 10 dB rail-shadow excess loss and 10 dB fade margin; node antenna "
          f"{p.NODE_HEIGHT_M.value} m above ballast.\n\n{checks_table(checks)}\n")
    return {"md": md, "checks": checks}


if __name__ == "__main__":
    print(run()["md"])
