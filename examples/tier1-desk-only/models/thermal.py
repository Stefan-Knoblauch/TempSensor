"""Lumped-parameter thermal model of the Tier 1 unit on a hot rail web.

Network (C = heat capacity J/K, G = conductance W/K):

  RAIL (boundary) --G_contact-- SLUG --G_pad-- SENSOR --G_leak-- ENCLOSURE
  RAIL --G_mounts-- HOUSING --G_collar-- SLUG          (unguarded design)
  ENCLOSURE = HOUSING interior (unguarded) or GUARD (guarded design, D-12):
  RAIL --G_pots-- GUARD --G_gh-- HOUSING;  GUARD --G_gi-- INTERIOR
  HOUSING --G_in-- INTERIOR --G_batt-- BATTERY/PCB
  HOUSING --(h_conv + h_rad)*A-- AIR (boundary);  HOUSING <- alpha*A_sun*G_solar
  AIR-POCKET sensor: --G_pocket_air-- AIR, --G_pocket_h-- HOUSING, --G_pocket_rail-- RAIL

Boundary conditions for a design hot day: air 25..45 degC sinusoid (min 05:00,
max 15:00); solar half-sine 06:00-18:00 peaking at 1000 W/m2; rail web =
air + 25 degC * solar (lagged 1 h), -1 degC at night (sky radiation).
Explicit Euler, dt = 0.5 s, three days to settle, evaluate day 3.
"""
import math

import params as p
from common import Check, md_table, checks_table

DAY = 86400.0


def boundary(t):
    h = (t % DAY) / 3600.0
    air = 35.0 + 10.0 * math.cos(2 * math.pi * (h - 15.0) / 24.0)   # max 15:00, min 03:00
    sol = max(0.0, math.sin(math.pi * (h - 6.0) / 12.0)) * 1000.0 if 6 <= h <= 18 else 0.0
    hl = h - 1.0                                                     # rail lags sun ~1 h
    sol_l = max(0.0, math.sin(math.pi * (hl - 6.0) / 12.0)) if 6 <= hl <= 18 else 0.0
    rail = air - 1.0 + 26.0 * sol_l                                  # continuous
    return air, sol, rail


CASES = {
    #               G_contact alpha  h_conv  G_leak  A_sun  G_pots
    "nominal":     (0.20,     0.35,  10.0,   0.004,  0.004, 0.30),
    "worst":       (0.05,     0.80,   3.0,   0.008,  0.008, 0.10),
}


# Sensing-element variants: (C of element J/K, G_contact nominal, G_contact worst, guarded)
VARIANTS = {
    "A: sprung brass slug, unguarded": (5.7, 0.20, 0.05, False),
    "B: sprung brass slug, guarded": (5.7, 0.20, 0.05, True),
    "C: sensor on centre pot cup, guarded (D-12)": (28.0, 0.50, 0.15, True),
}
TAU_COMP_S = 150.0     # firmware lag-compensation constant (D-13); minimax over nominal/worst cases


def simulate(case, variant):
    _, alpha, h_conv, g_leak, a_sun, g_pots = CASES[case]
    c_elem, g_nom, g_worst, guarded = VARIANTS[variant]
    g_contact = g_nom if case == "nominal" else g_worst
    c_guard, g_gh, g_gi = 20.0, 0.03, 0.03         # 40 g steel sleeve; foam-lined  [E]
    # capacities (J/K)  [E]
    c_slug, c_sens, c_house, c_int, c_batt, c_pocket = c_elem, 0.3, 195.0, 2.0, 150.0, 0.5
    # conductances (W/K)  [E]
    g_pad, g_collar, g_mounts, g_in, g_batt = 0.15, 0.0125, 0.05, 0.10, 0.05
    a_house, h_rad = 0.035, 6.0
    g_pocket_air, g_pocket_h, g_pocket_rail = 0.010, 0.004, 0.002

    air0, _, rail0 = boundary(0)
    s = sn = ho = it = bt = pk = gd = rail0
    dt = 0.5
    out = []
    for i in range(int(3 * DAY / dt)):
        t = i * dt
        air, sol, rail = boundary(t)
        # the slug collar and sensor leak terminate on the guard if fitted, else on the housing
        enc_c, enc_l = (gd, gd) if guarded else (ho, it)
        q_s = g_contact * (rail - s) + g_pad * (sn - s) + g_collar * (enc_c - s)
        q_sn = g_pad * (s - sn) + g_leak * (enc_l - sn)
        q_ho = (g_mounts * (rail - ho) + g_in * (it - ho)
                + (h_conv + h_rad) * a_house * (air - ho) + alpha * a_sun * sol
                + g_pocket_h * (pk - ho))
        q_it = g_in * (ho - it) + g_batt * (bt - it)
        q_gd = 0.0
        if guarded:
            q_gd = (g_pots * (rail - gd) + g_gh * (ho - gd) + g_gi * (it - gd)
                    + g_collar * (s - gd) + g_leak * (sn - gd))
            q_ho += g_gh * (gd - ho)
            q_it += g_gi * (gd - it)
        else:
            q_ho += g_collar * (s - ho)
            q_it += g_leak * (sn - it)
        q_bt = g_batt * (it - bt)
        q_pk = g_pocket_air * (air - pk) + g_pocket_h * (ho - pk) + g_pocket_rail * (rail - pk)
        s += q_s * dt / c_slug
        sn += q_sn * dt / c_sens
        ho += q_ho * dt / c_house
        it += q_it * dt / c_int
        bt += q_bt * dt / c_batt
        pk += q_pk * dt / c_pocket
        gd += q_gd * dt / c_guard
        if t >= 2 * DAY and i % int(600 / dt) == 0:                   # 10-min samples, day 3
            out.append((t, air, rail, sn, ho, pk))
    tau = c_slug / g_contact + c_sens / g_pad      # dominant element + sensor stage
    return out, tau


def run():
    checks, rows = [], []
    sensor_budget = p.INSTALLED_ERROR_BUDGET_C.value - p.TMP117_ACC_C.value
    for variant in VARIANTS:
      for case in CASES:
        out, tau = simulate(case, variant)
        label = f"{variant} / {case}"
        final = variant.startswith("C")
        err = [sn - rail for (_, _, rail, sn, _, _) in out]
        comp = [out[k][3] + TAU_COMP_S * (out[k][3] - out[k - 1][3]) / 600.0 - out[k][2]
                for k in range(1, len(out))]
        hot = [sn - rail for (_, _, rail, sn, _, _) in out if rail >= 40.0]
        pocket_bias = max(pk - air for (_, air, _, _, _, pk) in out)
        house_max = max(ho for (_, _, _, _, ho, _) in out)
        rail_max = max(r for (_, _, r, _, _, _) in out)
        rows.append((label, f"{rail_max:.1f}", f"{house_max:.1f}", f"{max(map(abs, err)):.3f}",
                     f"{max(map(abs, comp)):.3f}", f"{tau:.0f}", f"{pocket_bias:+.1f}"))
        if not final:
            continue                                   # earlier variants shown for the record
        checks.append(Check("T1-01", f"Mounting error with lag compensation, {label}",
                            f"{max(map(abs, comp)):.3f} °C", f"<= {sensor_budget:.1f} °C",
                            max(map(abs, comp)) <= sensor_budget))
        checks.append(Check("T1-08", f"Housing peak temperature, {label}",
                            f"{house_max:.1f} °C", "<= 85 °C", house_max <= 85.0))
    table = md_table(["Case", "Rail max (°C)", "Housing max (°C)", "Max |error| all day (°C)",
                      "Max |error| with lag comp. (°C)", "Time constant (s)",
                      "Near-rail air bias vs air, max (°C)"], rows)
    md = ("### Thermal: rail reading error and housing temperature\n\n"
          "Cases: nominal (good contact 0.20 W/K, grey cap α 0.35, 10 W/m²K wind, 0.004 m² sunlit) and "
          "worst (rusty contact 0.05 W/K, rust body α 0.80, still air 3 W/m²K, 0.008 m² sunlit, "
          f"double internal leakage).\n\n{table}\n\n"
          f"Error budget: {p.INSTALLED_ERROR_BUDGET_C.value} °C installed = TMP117 "
          f"{p.TMP117_ACC_C.value} °C + mounting ≤ {sensor_budget:.1f} °C (linear sum).\n\n"
          f"{checks_table(checks)}\n")
    return {"md": md, "checks": checks}


if __name__ == "__main__":
    print(run()["md"])
