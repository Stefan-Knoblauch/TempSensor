"""Packaging check: every internal part inside the housing cavity, no two parts clash.

Axis-aligned bounding boxes from geometry.py. Parts flagged inside=False (pots through
the front face, lid, cap, hook, vent...) are excluded from the cavity test but still
checked for clashes against internal parts. Touching (overlap <= 0.05 mm) is allowed.
The two radio SKUs are checked separately.
"""
import itertools

import geometry as g
from common import Check, md_table, checks_table

TOL = 0.05
SKIP = {"body", "cap", "lid", "oring", "screws", "vent", "louvre", "hook"}   # enclosure itself


def aabb(p):
    x, y, z = p["pos"]
    if p["shape"] in ("cyl", "pot"):
        r, h = p["d"] / 2, p["length"] / 2
        ext = {"x": (h, r, r), "y": (r, h, r), "z": (r, r, h)}[p["axis"]]
    else:
        ext = tuple(s / 2 for s in p["size"])
    return [(c - e, c + e) for c, e in zip((x, y, z), ext)]


def overlap(a, b):
    return all(min(a[i][1], b[i][1]) - max(a[i][0], b[i][0]) > TOL for i in range(3))


def check(parts, cavity):
    out_of_cavity, clashes = [], []
    for p in parts:
        if p["inside"] and p["id"] not in SKIP:
            box = aabb(p)
            for i, ax in enumerate("xyz"):
                lo, hi = cavity[ax]
                if box[i][0] < lo - TOL or box[i][1] > hi + TOL:
                    out_of_cavity.append(f"{p['id']} ({ax}: {box[i][0]:.1f}..{box[i][1]:.1f} vs {lo:.1f}..{hi:.1f})")
    inner = [p for p in parts if p["id"] not in SKIP]
    for a, b in itertools.combinations(inner, 2):
        if overlap(aabb(a), aabb(b)):
            clashes.append(f"{a['id']} × {b['id']}")
    return out_of_cavity, clashes


def run():
    checks, rows = [], []
    for sku in ("lora", "ltem"):
        parts = [p for p in g.PARTS if p["sku"] in ("both", sku)]
        oc, cl = check(parts, g.CAVITY)
        rows.append((sku, len(parts), "; ".join(oc) or "none", "; ".join(cl) or "none"))
        checks.append(Check("T1-12", f"Packaging, {sku.upper()} SKU: parts inside cavity",
                            f"{len(oc)} outside", "0", not oc))
        checks.append(Check("T1-12", f"Packaging, {sku.upper()} SKU: no clashes",
                            f"{len(cl)} clashes", "0", not cl))
    # Show why the original 38 mm deep envelope was rejected (D-30)
    old = dict(g.CAVITY, z=(g.WALL, 38.0 - g.LID_T))
    oc_old, _ = check([p for p in g.PARTS if p["sku"] in ("both", "lora")], old)
    md = ("### Packaging (3-D layout)\n\n"
          f"Body {g.BODY_L:.0f} × {g.BODY_H:.0f} × {g.BODY_D:.0f} mm; cavity "
          + ", ".join(f"{k} {v[0]:.1f}..{v[1]:.1f}" for k, v in g.CAVITY.items()) + " mm. "
          f"{len(g.PARTS)} parts from `models/geometry.py` (the same file drives the 3-D viewer).\n\n"
          + md_table(["SKU", "Parts", "Outside cavity", "Clashes"], rows)
          + f"\n\nOriginal 38 mm depth (rejected, D-30): {len(oc_old)} parts would not fit "
          f"({'; '.join(oc_old)}).\n\n" + checks_table(checks) + "\n")
    return {"md": md, "checks": checks}


if __name__ == "__main__":
    print(run()["md"])
