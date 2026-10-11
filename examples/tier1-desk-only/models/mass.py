"""Mass budget for the Tier 1 unit (D-12, D-14, D-15)."""
import geometry as g
import params as p
from common import Check, md_table, checks_table

# Polymer parts from geometry: shell area x wall x density, x1.4 for ribs, bosses, pockets
_front = g.BODY_L * (g.BODY_H - g.CAP_T) - 3 * 3.1416 * (g.POT_D / 2) ** 2
_sides = 2 * (g.BODY_H - g.CAP_T) * g.BODY_D + g.BODY_L * g.BODY_D
BODY_G = round((_front + _sides) * g.WALL / 1000 * 1.36 * 1.4)          # PA6-GF30 1.36 g/cm3
CAP_G = round(g.BODY_L * g.BODY_D * g.CAP_T / 1000 * 1.07)             # ASA
LID_G = round((g.BODY_L - 6) * (g.BODY_H - g.CAP_T - 6) * g.LID_T / 1000 * 1.36)

# grams, [E] from part datasheets / volume x density
ITEMS = [
    (f"Housing body (from geometry, {g.BODY_L:.0f}×{g.BODY_H:.0f}×{g.BODY_D:.0f} mm, incl. ribs)", BODY_G),
    ("Top cap, ASA (from geometry)", CAP_G),
    ("Lid, PA6-GF30 (from geometry)", LID_G),
    ("3 x Ø32 NdFeB SH pot magnets (steel cup)", 180),
    ("Steel thermal guard bridge", 40),
    ("Li-SOCl2 D-cell", 95),
    ("Hybrid layer capacitor", 10),
    ("PCB + module + sensors + antenna", 22),
    ("Hook slider, PA6-GF30", 25),
    ("Gasket, vent, security screws, lid magnet", 12),
    ("Potting / cell clamp / thermal pad / foam", 25),
]


def run():
    total = sum(g for _, g in ITEMS)
    limit = p.MASS_KG.value * 1000
    checks = [Check("T1-12", "Mass budget", f"{total} g", f"<= {limit:.0f} g", total <= limit)]
    md = ("### Mass budget\n\n" + md_table(["Item", "g"], ITEMS + [("**Total**", f"**{total}**")])
          + f"\n\nMargin {limit - total:.0f} g ({100 * (limit - total) / limit:.0f} %).\n\n"
          + checks_table(checks) + "\n")
    return {"md": md, "checks": checks, "total_g": total}


if __name__ == "__main__":
    print(run()["md"])
