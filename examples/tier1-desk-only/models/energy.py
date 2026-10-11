"""Battery-life model for both Tier 1 SKUs."""
import params as p
from common import Check, md_table, checks_table
from lora import charge_per_uplink_mah

HOURS = 24.0


def life_years(daily_mah: float) -> float:
    yearly = daily_mah * 365 + p.SELF_DISCHARGE_PER_YR.value * p.CELL_NOMINAL_MAH.value
    return min(p.CELL_USABLE_MAH.value / yearly, p.CELL_CALENDAR_LIMIT_YR.value)


def lora_daily_mah(sf: int) -> float:
    sleep = p.SLEEP_UA.value / 1000 * HOURS
    uplinks = 24 * 60 / p.SAMPLE_INTERVAL_MIN.value + 1          # + daily status
    return sleep + uplinks * charge_per_uplink_mah(sf) / p.DCDC_EFFICIENCY.value


def ltem_daily_mah(session_mah: float, coverage_factor: float, hot_batch_min: float,
                   cool_mode: bool = False) -> float:
    """hot_batch_min: reporting interval while in HOT mode (10 = every sample).
    cool_mode: batch every COOL_BATCH_MIN while rail < COOL_THRESHOLD_C."""
    sleep = (p.SLEEP_UA.value + p.LTEM_PSM_FLOOR_UA.value) / 1000 * HOURS
    if cool_mode:
        cf = p.COOL_FRACTION.value
        normal = p.LTEM_BATCH_PER_DAY.value * (1 - cf) + (HOURS * 60 / p.COOL_BATCH_MIN.value) * cf
    else:
        normal = p.LTEM_BATCH_PER_DAY.value
    normal += 1                                                    # + daily status
    hot_hours_per_day = p.HOT_DAYS_PER_YR.value * p.HOT_HOURS_PER_DAY.value / 365
    extra_per_hot_hour = 60 / hot_batch_min - 1                    # hourly one already counted
    sessions = normal + hot_hours_per_day * extra_per_hot_hour
    return sleep + sessions * session_mah * coverage_factor / p.DCDC_EFFICIENCY.value


def run():
    checks, rows = [], []
    for sf in range(7, 13):
        d = lora_daily_mah(sf)
        rows.append(("LoRa", f"SF{sf}", "-", f"{d:.2f}", f"{life_years(d):.1f}"))
        if sf <= 8:
            checks.append(Check("T1-07", f"LoRa SKU life at SF{sf}", f"{life_years(d):.1f} yr",
                                ">= 7 yr", life_years(d) >= 7))
    lo, hi = p.LTEM_SESSION_MAH_GOOD
    # Option A: hourly batch all year, every sample in HOT (original concept).
    # Option B (adopted, D-11): 4-hourly batch when rail < 25 degC, hourly otherwise,
    # HOT reports every 20 min. Alarms are immediate in both.
    options = (("A", 10, False), ("B (adopted)", 20, True))
    for opt, hot_min, cool in options:
        for label, sess, cov in (("good, typical", (lo + hi) / 2, 1.0),
                                 ("good, worst", hi, 1.0),
                                 ("deep (x3), typical", (lo + hi) / 2, p.LTEM_DEEP_FACTOR.value)):
            d = ltem_daily_mah(sess, cov, hot_min, cool)
            rows.append((f"LTE-M opt {opt}", label, f"{hot_min} min", f"{d:.2f}", f"{life_years(d):.1f}"))
            if label.startswith("good") and opt.startswith("B"):
                checks.append(Check("T1-07", f"LTE-M SKU life, option B, {label}",
                                    f"{life_years(d):.1f} yr", ">= 3 yr", life_years(d) >= 3))
    table = md_table(["SKU", "Case", "HOT reporting", "mAh/day", "Life (yr)"], rows)
    md = (f"### Battery life\n\nUsable {p.CELL_USABLE_MAH.value:.0f} mAh of a "
          f"{p.CELL_NOMINAL_MAH.value:.0f} mAh cell; self-discharge "
          f"{100*p.SELF_DISCHARGE_PER_YR.value:.0f} %/yr; life capped at "
          f"{p.CELL_CALENDAR_LIMIT_YR.value:.0f} yr calendar. HOT season "
          f"{p.HOT_DAYS_PER_YR.value:.0f} days x {p.HOT_HOURS_PER_DAY.value:.0f} h.\n\n"
          f"{table}\n\n{checks_table(checks)}\n")
    return {"md": md, "checks": checks}


if __name__ == "__main__":
    print(run()["md"])
