"""Housing fit on Transnet rail profiles and cross-section drawing (SVG).

Profile data: height, head width, foot width, web thickness. 48/57 kg/m values
are from a supplier table of ISCOR-standard rails (unverified); 60E1 from EN
13674-1. The web window (head underside to foot top, at the web face) is
estimated as 0.59 x height [E] and checked with a -10 % tolerance.
"""
import os

from common import Check, md_table, checks_table

PROFILES = {   # name: (height, head, foot, web) mm
    "SA 48 kg/m": (150.0, 68.0, 127.0, 14.0),
    "SA 57 kg/m": (165.0, 71.2, 140.0, 16.0),
    "60E1 (UIC60)": (172.0, 72.0, 150.0, 16.5),
}
WINDOW_FACTOR = 0.59      # [E] web window / rail height
WINDOW_TOL = 0.10         # [E] tolerance on the window estimate

# Unit geometry (D-15, D-30) from the single geometry source
from geometry import BODY_H, BODY_D, BODY_L
STOP_CLEAR = 1.5          # head-stop clearance under head (installed)
HOOK_MIN, HOOK_MAX = 14.0, 50.0   # hook slider extension range below body
HOOK_DETENT_STEP = 1.0    # detent pitch; the slider is set to the nearest detent then screwed


def window(h):
    return WINDOW_FACTOR * h


def rail_polygon(h, head, foot, web, x0, y0, s):
    """Simplified rail section: x right = field side, y up. Returns SVG points."""
    hf_e, hf_w, hh = 0.07 * h, 0.14 * h, 0.27 * h
    pts = [(-foot / 2, 0), (foot / 2, 0), (foot / 2, hf_e), (web / 2, hf_w), (web / 2, h - hh),
           (head / 2, h - hh + 0.04 * h), (head / 2, h), (-head / 2, h), (-head / 2, h - hh + 0.04 * h),
           (-web / 2, h - hh), (-web / 2, hf_w), (-foot / 2, hf_e)]
    return " ".join(f"{x0 + x * s:.1f},{y0 - y * s:.1f}" for x, y in pts)


def svg(results, path):
    s, w_each = 1.5, 300
    parts = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{w_each * len(results)}" height="420" '
             'font-family="sans-serif" font-size="12">',
             '<rect width="100%" height="100%" fill="white"/>']
    for i, (name, (h, head, foot, web), r) in enumerate(results):
        x0, y0 = 120 + i * w_each, 380
        parts.append(f'<polygon points="{rail_polygon(h, head, foot, web, x0, y0, s)}" '
                     'fill="#b9bec4" stroke="#333"/>')
        hf_w = 0.14 * h
        foot_top = hf_w
        bx, by = x0 + web / 2 * s, y0 - (foot_top + r["hook"]) * s
        parts.append(f'<rect x="{bx:.1f}" y="{by - BODY_H * s:.1f}" width="{BODY_D * s:.1f}" '
                     f'height="{BODY_H * s:.1f}" fill="#8a4b2a" stroke="#222"/>')
        parts.append(f'<rect x="{bx:.1f}" y="{by:.1f}" width="{10 * s:.1f}" height="{r["hook"] * s:.1f}" '
                     'fill="#5a5a5a" stroke="#222"/>')
        parts.append(f'<text x="{x0 - 90}" y="20" font-weight="bold">{name}</text>')
        parts.append(f'<text x="{x0 - 90}" y="36">window {r["win"]:.0f} mm, hook set {r["hook"]:.0f} mm</text>')
    parts.append('<text x="10" y="408" fill="#444">Field side to the right. Brown: unit body; grey bar: '
                 'hook slider resting on foot top. Simplified sections, not to drawing accuracy.</text>')
    parts.append("</svg>")
    with open(path, "w") as f:
        f.write("\n".join(parts))


def run(svg_path=None):
    rows, checks, results = [], [], []
    for name, (h, head, foot, web) in PROFILES.items():
        for tol, tag in ((0.0, "nominal"), (-WINDOW_TOL, "-10 %"), (WINDOW_TOL, "+10 %")):
            win = window(h) * (1 + tol)
            hook = win - BODY_H - STOP_CLEAR
            ok = HOOK_MIN <= hook <= HOOK_MAX
            checks.append(Check("T1-12", f"{name}, window {tag}: hook setting in slider range",
                                f"{hook:.1f} mm", f"{HOOK_MIN:.0f}–{HOOK_MAX:.0f} mm", ok))
            if tol == 0.0:
                overhang = (head - web) / 2
                protrude = BODY_D - overhang
                rows.append((name, f"{win:.0f}", f"{hook:.0f}", f"{overhang:.1f}", f"{protrude:.1f}"))
                results.append((name, (h, head, foot, web), {"win": win, "hook": hook}))
                checks.append(Check("T1-12", f"{name}: unit stays inside foot overhang (clear of ballast-side edge)",
                                    f"{BODY_D:.0f} mm", f"<= {(foot - web) / 2:.1f} mm",
                                    BODY_D <= (foot - web) / 2))
    if svg_path:
        svg(results, svg_path)
    table = md_table(["Profile", "Web window (mm)", "Hook setting (mm)", "Head overhang (mm)",
                      "Protrusion beyond head edge (mm)"], rows)
    md = ("### Fit on rail profiles\n\n"
          f"Body {BODY_L:.0f} × {BODY_H:.0f} × {BODY_D:.0f} mm (L × H × D); hook slider "
          f"{HOOK_MIN:.0f}–{HOOK_MAX:.0f} mm in {HOOK_DETENT_STEP:.0f} mm detents; head-stop clearance "
          f"{STOP_CLEAR} mm. Web window estimated at {WINDOW_FACTOR} × rail height, checked at ±10 %.\n\n"
          f"{table}\n\nThe unit protrudes beyond the head edge on the **field side only**, below head "
          "level, so it is outside the wheel/flange zone.\n\n"
          "![Fit on rail profiles](fit_profiles.svg)\n\n" + checks_table(checks) + "\n")
    return {"md": md, "checks": checks}


if __name__ == "__main__":
    print(run(os.path.join(os.path.dirname(__file__), "..", "verification", "fit_profiles.svg"))["md"])
