"""Mass budget for the Tier 1 unit (D-12, D-14, D-15)."""
import params as p
from common import Check, md_table, checks_table

# grams, [E] from part datasheets / volume x density
ITEMS = [
    ("Housing body + top cap, PA6-GF30/ASA, 3 mm wall", 150),
    ("3 x Ø32 NdFeB SH pot magnets (steel cup)", 180),
    ("Steel thermal guard bridge", 40),
    ("Li-SOCl2 D-cell", 95),
    ("Hybrid layer capacitor", 10),
    ("PCB + module + sensors + antenna", 22),
    ("Hook slider, PA6-GF30", 25),
    ("Lid, gasket, vent, security screws", 30),
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
