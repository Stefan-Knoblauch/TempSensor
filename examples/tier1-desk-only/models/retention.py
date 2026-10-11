"""Retention of the rail unit under EN 50125-3 on-rail vibration.

Axes on a web-mounted unit:
  lateral      = normal to the web  -> resisted by magnet pull (pull-off)
  longitudinal = along the rail     -> resisted by friction (mu * clamp force)
  vertical     = along the web face -> magnets alone resist only by friction;
                 the adopted design (D-14) captures the unit geometrically between
                 a polymer hook on the rail foot and a stop under the rail head.
Design load = mass * (3-sigma random peak [+ g for vertical]) * margin.
The EN 50125-3 values are unconfirmed and the axis mapping is assumed (worst
axis = vertical), so every check is shown per axis.
"""
import math

import params as p
from common import Check, md_table, checks_table

G = 9.81


def pull_at_gap_n(gap_mm: float) -> float:
    """Pot-magnet pull vs air gap, F(g) = F0 / (1 + k g)^2 with F(g_half) = F0 / 2."""
    k = (math.sqrt(2) - 1) / p.MAG_HALF_PULL_GAP_MM.value
    f0 = p.N_MAGNETS.value * p.MAG_RATED_PULL_KGF.value * G
    return f0 / (1 + k * gap_mm) ** 2


def run():
    m = p.MASS_KG.value
    pk = p.PEAK_FACTOR.value
    margin = p.RETENTION_MARGIN.value
    a = {ax: v.value * pk for ax, v in p.VIB_RMS_MS2.items()}
    clamp = pull_at_gap_n(p.GAP_WORST_MM.value)
    n, nc = p.N_MAGNETS.value, p.N_COATED_POTS.value
    mu_mix = (nc * p.MU_COATED.value + (n - nc) * p.MU_BARE_WORST.value) / n
    load = {"lateral": m * a["lateral"] * margin,
            "longitudinal": m * a["longitudinal"] * margin,
            "vertical": m * (a["vertical"] + G) * margin}

    rows = [
        ("Rated pull, zero gap", f"{pull_at_gap_n(0):.0f} N"),
        (f"Pull at worst gap {p.GAP_WORST_MM.value} mm", f"{clamp:.0f} N"),
        ("Friction, bare pot face (µ 0.15)", f"{clamp * p.MU_BARE_WORST.value:.0f} N"),
        ("Friction, PU-coated pot face (µ 0.5)", f"{clamp * p.MU_COATED.value:.0f} N"),
        (f"Friction, adopted mix ({int(nc)} coated + {int(n - nc)} bare, µ {mu_mix:.2f})", f"{clamp * mu_mix:.0f} N"),
        ("Design load, lateral (pull-off)", f"{load['lateral']:.0f} N"),
        ("Design load, longitudinal", f"{load['longitudinal']:.0f} N"),
        ("Design load, vertical (incl. weight)", f"{load['vertical']:.0f} N"),
        ("Old rule: 10 x weight in shear", f"{10 * m * G:.0f} N"),
    ]
    checks = [
        Check("T1-11", "Pull-off: magnet pull at worst gap vs lateral design load",
              f"{clamp:.0f} N", f">= {load['lateral']:.0f} N", clamp >= load["lateral"]),
        Check("T1-11", "Longitudinal: friction (bare face) vs design load [rejected option]",
              f"{clamp * p.MU_BARE_WORST.value:.0f} N", f">= {load['longitudinal']:.0f} N",
              clamp * p.MU_BARE_WORST.value >= load["longitudinal"]),
        Check("T1-11", "Longitudinal: friction (2 coated + 1 bare pot, D-14) vs design load",
              f"{clamp * mu_mix:.0f} N", f">= {load['longitudinal']:.0f} N",
              clamp * mu_mix >= load["longitudinal"]),
        Check("T1-10", "Vertical: magnet friction alone vs design load [rejected option]",
              f"{clamp * p.MU_COATED.value:.0f} N", f">= {load['vertical']:.0f} N",
              clamp * p.MU_COATED.value >= load["vertical"]),
    ]
    # Adopted: geometric capture. The hook and head stop must carry the vertical load.
    hook_req = load["vertical"]
    sigma = (6 * hook_req * p.HOOK_LEVER_MM.value / 1000
             / (p.HOOK_WIDTH_MM.value / 1000 * (p.HOOK_THICK_MM.value / 1000) ** 2)) / 1e6
    checks.append(Check("T1-10", f"Hook root bending stress at design load ({p.HOOK_THICK_MM.value:.0f} mm PA6-GF30)",
                        f"{sigma:.1f} MPa", f"<= {p.PA_GF_FATIGUE_MPA.value:.0f} MPa (fatigue, 80 °C)",
                        sigma <= p.PA_GF_FATIGUE_MPA.value))
    md = ("### Retention under on-rail vibration\n\n"
          f"Mass {m} kg; {int(p.N_MAGNETS.value)} × {p.MAG_RATED_PULL_KGF.value:.0f} kgf pot magnets; "
          f"3σ peaks of EN 50125-3 RMS values (vertical {p.VIB_RMS_MS2['vertical'].value}, lateral "
          f"{p.VIB_RMS_MS2['lateral'].value}, longitudinal {p.VIB_RMS_MS2['longitudinal'].value} m/s², "
          f"unconfirmed); margin {margin}×.\n\n"
          + md_table(["Quantity", "Value"], rows) + "\n\n" + checks_table(checks) +
          "\n\n**Result.** Magnets alone cannot hold the unit vertically against on-rail vibration "
          "(the old \"10 × weight\" rule under-states the load by about "
          f"{load['vertical'] / (10 * m * G):.0f}×). Adopted design (D-14): PU-coated pot faces for "
          "longitudinal friction on the two outer pots (centre pot left bare for thermal contact), and geometric capture between a rail-foot hook and a rail-head "
          f"stop. Derived requirement for mechanical design: hook and stop each carry ≥ {hook_req:.0f} N"
          "; the hook is sized for bending fatigue in the check above.\n")
    return {"md": md, "checks": checks, "hook_load_n": hook_req,
            "rejected": {"Longitudinal: friction (bare face) vs design load [rejected option]",
                         "Vertical: magnet friction alone vs design load [rejected option]"}}


if __name__ == "__main__":
    print(run()["md"])
