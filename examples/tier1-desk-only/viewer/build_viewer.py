#!/usr/bin/env python3
"""Build the interactive 3-D viewer (rail-sensor-3d.html) from the design sources.

Inputs: models/geometry.py (every part, position, explode vector, function text),
models/fit.py (rail profile), models/mass.py (mass budget), parts_sourcing.json
(South African suppliers and prices). Output: a single self-contained HTML file
that loads three.js from a CDN and needs no other files.
"""
import base64
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "models"))

import fit  # noqa: E402
import geometry  # noqa: E402
import mass  # noqa: E402
import thermal  # noqa: E402

# geometry 'source' key -> parts_sourcing.json id
SOURCE_MAP = {
    "housing": "housing_sls", "oring": "oring", "screws": "security_screws", "vent": "vent",
    "pot": "pot_magnet_32", "pad": "thermal_pad", "tmp117": "tmp117", "guard": "guard_bridge",
    "tmag5273": "tmag5273", "cell": "lisocl2_d", "hlc": "hlc", "pcb": "pcb", "rak3172": "rak3172",
    "nrf9151": "nrf9151", "lis2dw12": "lis2dw12", "mx25r": "mx25r8035f", "reed": "reed_switch",
    "lidmag": "lid_magnet", "ant868": "ant_868", "antlte": "ant_ltem",
}

LINE = {
    "facts": [
        ["Route", "Sishen (Northern Cape) to Saldanha Bay, 861 km, single track with passing loops"],
        ["Traffic", "Iron-ore export, 342-wagon trains (375 since 2019)"],
        ["Axle load", "30 t"],
        ["Traction", "50 kV AC overhead, Class 15E electric locomotives"],
        ["Gauge", "1 067 mm (Cape gauge)"],
    ],
    "note": "Line facts from public sources (Wikipedia, Railway Gazette, BusinessTech). The rail section "
            "(60 kg/m class), sleeper spacing, mast spacing and the train's livery are illustrative, not "
            "Transnet drawings.",
}


def main():
    with open(os.path.join(HERE, "parts_sourcing.json")) as f:
        sourcing = json.load(f)
    by_id = {p["id"]: p for p in sourcing["parts"]}
    h, head, foot, web = fit.PROFILES["60E1 (UIC60)"]
    total_g = sum(gm for _, gm in mass.ITEMS)
    mount = [c for c in thermal.run()["checks"] if c.req == "T1-01"]
    mount_err = max(float(c.value.split()[0]) for c in mount)
    data = {
        "meta": {"subtitle": "Tier 1 · desk-only design rev B", "mass": total_g, "mountErr": f"{mount_err:.2f}"},
        "unit": geometry.as_json(),
        "rail": {"h": h, "head": head, "foot": foot, "web": web},
        "site": {"levels": [55, 60, 65]},
        "line": LINE,
        "sourcing": sourcing,
        "sourceMap": {k: by_id.get(v) for k, v in SOURCE_MAP.items()},
    }
    missing = [k for k, v in data["sourceMap"].items() if v is None]
    if missing:
        raise SystemExit(f"sourcing ids missing for: {missing}")
    with open(os.path.join(HERE, "template.html")) as f:
        html = f.read()
    html = html.replace("/*__DATA__*/null", json.dumps(data, ensure_ascii=False))
    with open(os.path.join(HERE, "assets", "bigetron-logo.png"), "rb") as f:
        html = html.replace("__LOGO__", "data:image/png;base64," + base64.b64encode(f.read()).decode())
    logo_uri = "data:image/png;base64," + base64.b64encode(open(os.path.join(HERE, "assets", "bigetron-logo.png"), "rb").read()).decode()
    site = html.replace("<!--__SIGNOUT__-->", '<a class="signout" href="/logout">Sign out</a>')
    html = html.replace("<!--__SIGNOUT__-->", "")
    out = os.path.join(HERE, "rail-sensor-3d.html")
    with open(out, "w") as f:
        f.write(html)
    # Vercel site at the repository root: index.html behind middleware.js, plus login.html
    repo = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
    with open(os.path.join(repo, "index.html"), "w") as f:
        f.write(site)
    with open(os.path.join(HERE, "login-template.html")) as f:
        login = f.read().replace("__LOGO__", logo_uri)
    with open(os.path.join(repo, "login.html"), "w") as f:
        f.write(login)
    # Published-page variant: the host supplies doctype/html/head/body, charset and viewport.
    frag = re.sub(r"<!doctype html>\s*<html[^>]*>\s*<head>\s*", "", html, flags=re.I)
    frag = re.sub(r'<meta charset="utf-8">\s*<meta name="viewport"[^>]*>\s*', "", frag)
    frag = frag.replace("</head>\n<body>\n", "").replace("</body>\n</html>", "")
    with open(os.path.join(HERE, "rail-sensor-3d.artifact.html"), "w") as f:
        f.write(frag)
    print(f"wrote {out} ({len(html) / 1024:.0f} KB, {len(geometry.PARTS)} parts) + artifact variant")


if __name__ == "__main__":
    main()
