"""First natural frequencies of the parts most exposed to on-rail vibration.

The unit body is held against the web by ~450 N of magnetic preload through steel
contact, so while it stays in contact (checked in retention.py) its own rigid-body
modes are set by contact stiffness and lie far above the band. The parts that can
resonate are compliant sub-structures:
  - hook lip carrying the unit mass (cantilever)
  - PCB with components (plate)
  - D-cell in its cradle (mass on a liner)
Requirement [A]: first mode >= MODAL_SEPARATION x VIB_BAND_UPPER_HZ.
"""
import math

import params as p
from common import Check, md_table, checks_table

E_PA_GF_80C = 5.0e9        # PA6-GF30 conditioned, 80 degC [E]
E_FR4, NU_FR4, RHO_FR4 = 22e9, 0.15, 1850.0


def cantilever_hz(e, b, h, length, mass):
    k = 3 * e * (b * h ** 3 / 12) / length ** 3
    return math.sqrt(k / mass) / (2 * math.pi)


def plate_ss_hz(a, b, h, areal_mass):
    d = E_FR4 * h ** 3 / (12 * (1 - NU_FR4 ** 2))
    return math.pi / 2 * (1 / a ** 2 + 1 / b ** 2) * math.sqrt(d / areal_mass)


def liner_hz(e, area, t, mass):
    return math.sqrt(e * area / t / mass) / (2 * math.pi)


def run():
    m = p.MASS_KG.value
    hook = lambda lever_mm: cantilever_hz(E_PA_GF_80C, p.HOOK_WIDTH_MM.value / 1e3,
                                          p.HOOK_THICK_MM.value / 1e3, lever_mm / 1e3, m)
    pcb_mass = RHO_FR4 * 1.6e-3 + 0.020 / (0.050 * 0.040)        # board + 20 g components
    pcb = lambda a, b: plate_ss_hz(a, b, 1.6e-3, pcb_mass)
    cell = lambda e, t: liner_hz(e, 2 * 0.060 * 0.010, t, 0.095)
    target = p.MODAL_SEPARATION.value * p.VIB_BAND_UPPER_HZ.value
    items = [
        ("Hook lip, 10 mm lever (original)", hook(10.0), False),
        (f"Hook lip, {p.HOOK_LEVER_MM.value:.0f} mm lever (D-25)", hook(p.HOOK_LEVER_MM.value), True),
        ("PCB 50 × 40 mm, 4 corner screws", pcb(0.050, 0.040), False),
        ("PCB with centre standoff (D-26, ~25 × 20 mm panels)", pcb(0.025, 0.020), True),
        ("D-cell on 2 mm EPDM pads", cell(5e6, 2e-3), False),
        ("D-cell in rigid cradle, 0.5 mm Shore-90 PU liner (D-27)", cell(50e6, 0.5e-3), True),
    ]
    rows = [(n, f"{f:.0f}", "adopted" if a else "rejected") for n, f, a in items]
    checks = [Check("T1-10", f"First mode: {n}", f"{f:.0f} Hz", f">= {target:.0f} Hz", f >= target)
              for n, f, a in items if a]
    md = ("### Modal check of compliant parts\n\n"
          f"Band upper limit assumed {p.VIB_BAND_UPPER_HZ.value:.0f} Hz (EN 50125-3 spectrum not "
          f"confirmed); required first mode ≥ {target:.0f} Hz. Body rigid-body modes are not limiting "
          "while magnetic preload keeps steel contact (see retention).\n\n"
          + md_table(["Part", "First mode (Hz)", "Status"], rows) + "\n\n" + checks_table(checks) + "\n")
    return {"md": md, "checks": checks}


if __name__ == "__main__":
    print(run()["md"])
