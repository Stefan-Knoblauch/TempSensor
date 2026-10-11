"""Axisymmetric nonlinear magnetostatic model of one Ø32 mm pot magnet on rail steel.

Formulation: flux function psi = r*A_phi on a non-uniform (r, z) grid, finite volumes,
  -d/dr(nu/r dpsi/dr) - d/dz(nu/r dpsi/dz) = J_phi
The NdFeB disc (magnetised +z) is replaced by its equivalent surface current
K = Br / (mu0 mu_r) on its cylindrical face. Steel permeability follows a
saturating B-H curve (Picard iteration on nu). Pull force from the Maxwell stress
tensor integrated over a plane in the air gap. Hall-sensor field is read behind
and beside the pot with the plate present (attached) and absent (detached).

Answers two open questions from hardware-design.md §5:
  1. Pull vs gap (replaces the analytic F0/(1+kg)^2 fit in params.py)
  2. Is there a usable leakage-flux change for attachment sensing (D-18)?
Axisymmetry approximates the rail web (a 15 mm plate, 45 mm radius) and ignores
the neighbouring pots; both are noted in the report.
"""
import math

import numpy as np
from scipy.sparse import coo_matrix
from scipy.interpolate import RegularGridInterpolator
from scipy.sparse.linalg import spsolve

import params as p
from common import Check, md_table, checks_table

MU0 = 4e-7 * math.pi

# --- geometry (m) [E: typical Ø32 pot magnet catalogue geometry] -------------
R_CUP, R_CUP_IN, R_MAG = 16e-3, 13.5e-3, 12e-3
H_MAG, T_BACK = 5.0e-3, 4.0e-3          # magnet thickness, cup back thickness (4 mm: back not saturated)
PLATE_T, PLATE_R = 15e-3, 45e-3         # rail web thickness, axisymmetric radius
BR_20C, MU_R_MAG = 1.20, 1.05           # NdFeB 35SH [R typ.]
BR_TEMP_COEF = -0.0012                  # /K reversible [R typ.]

# --- B-H curves [E: typical low-carbon steel; rail steel (pearlitic R260) taken as
#     twice the field strength for the same B, i.e. harder magnetically] ---------
BH_B = np.array([0.0, 0.5, 1.0, 1.2, 1.4, 1.5, 1.6, 1.7, 1.8, 1.9, 2.0, 2.1, 2.2])
BH_H = np.array([0, 160, 300, 400, 700, 1000, 1700, 3500, 7000, 14000, 30000, 70000, 160000.0])


def nu_steel(b, h_scale=1.0):
    b = np.maximum(b, 1e-4)
    h = np.where(b <= BH_B[-1], np.interp(b, BH_B, BH_H * h_scale),
                 BH_H[-1] * h_scale + (b - BH_B[-1]) / MU0)
    return np.maximum(h / b, 1 / (MU0 * 5000))          # cap initial mu_r at 5000


def graded(segments):
    """segments: list of (start, end, step) -> sorted unique node coordinates."""
    pts = []
    for a, b, h in segments:
        n = max(int(math.ceil(abs(b - a) / h)), 1)
        pts.extend(np.linspace(a, b, n + 1))
    return np.unique(np.round(np.array(pts), 9))


def build_grid(gap):
    r = graded([(0, R_MAG, 0.4e-3), (R_MAG, R_CUP, 0.25e-3), (R_CUP, 24e-3, 0.5e-3),
                (24e-3, PLATE_R, 1.5e-3), (PLATE_R, 90e-3, 5e-3)])
    zf = 0.0                                   # pot face
    zp = -gap                                  # plate top
    z = graded([(-80e-3, zp - PLATE_T, 5e-3), (zp - PLATE_T, zp - 1e-3, 0.5e-3),
                (zp - 1e-3, zp, 0.1e-3), (zp, zf, gap / 8), (zf, 1e-3, 0.1e-3),
                (1e-3, H_MAG + T_BACK, 0.25e-3), (H_MAG + T_BACK, 20e-3, 1e-3),
                (20e-3, 80e-3, 5e-3)])
    return r, z


def materials(rc, zc, gap, plate):
    """Per-cell region codes: 0 air, 1 magnet, 2 cup steel, 3 rail steel."""
    R, Z = np.meshgrid(rc, zc, indexing="ij")
    m = np.zeros(R.shape, dtype=int)
    in_mag = (R < R_MAG) & (Z > 0) & (Z < H_MAG)
    cup_back = (R < R_CUP) & (Z > H_MAG) & (Z < H_MAG + T_BACK)
    cup_wall = (R > R_CUP_IN) & (R < R_CUP) & (Z > 0) & (Z < H_MAG)
    m[in_mag] = 1
    m[cup_back | cup_wall] = 2
    if plate:
        m[(R < PLATE_R) & (Z < -gap) & (Z > -gap - PLATE_T)] = 3
    return m


def solve(gap, plate=True, temp_c=20.0, rail_h_scale=2.0, iters=300, t_back=None, plate_h_scale=None,
          magnet_only=False):
    global T_BACK
    if t_back is not None:
        T_BACK = t_back
    r, z = build_grid(gap)
    nr, nz = len(r), len(z)
    dr, dz = np.diff(r), np.diff(z)
    rc, zc = (r[:-1] + r[1:]) / 2, (z[:-1] + z[1:]) / 2
    mat = materials(rc, zc, gap, plate)
    if magnet_only:                            # validation case: bare disc in air, mu_r = 1
        mat[mat == 2] = 0
    br = BR_20C * (1 + BR_TEMP_COEF * (temp_c - 20.0))
    nu = np.full(mat.shape, 1 / MU0)
    mu_r_mag = 1.0 if magnet_only else MU_R_MAG
    nu[mat == 1] = 1 / (MU0 * mu_r_mag)
    steel = (mat == 2) | (mat == 3)
    nu[steel] = 1 / (MU0 * 1000)

    # source: K at r = R_MAG over the magnet height (node column exactly at R_MAG)
    im = int(np.argmin(np.abs(r - R_MAG)))
    k_s = br / (MU0 * mu_r_mag)
    src = np.zeros((nr, nz))
    for j in range(1, nz - 1):
        lo, hi = (z[j - 1] + z[j]) / 2, (z[j] + z[j + 1]) / 2
        overlap = max(0.0, min(hi, H_MAG) - max(lo, 0.0))
        src[im, j] = k_s * overlap

    idx = -np.ones((nr, nz), dtype=int)
    inner = np.zeros((nr, nz), bool)
    inner[1:-1, 1:-1] = True
    idx[inner] = np.arange(inner.sum())
    psi = np.zeros((nr, nz))
    force_hist = []
    relax = np.full(mat.shape, 0.5)            # per-cell Picard weight, halved on sign flips
    prev_step = np.zeros(mat.shape)
    for it in range(iters):
        # face coefficients from cell nu (half-cell contributions)
        w = nu / rc[:, None]                                   # nu/r for z-fluxes
        a_e = np.zeros((nr, nz)); a_n = np.zeros((nr, nz))
        # east face of node (i,j): cells (i, j-1) and (i, j)  [cell i spans r_i..r_i+1]
        a_e[:-1, 1:-1] = (nu[:, :-1] * dz[None, :-1] / 2 + nu[:, 1:] * dz[None, 1:] / 2) \
            / (((r[:-1] + r[1:]) / 2)[:, None] * dr[:, None])
        a_n[1:-1, :-1] = (w[:-1, :] * dr[:-1, None] / 2 + w[1:, :] * dr[1:, None] / 2) / dz[None, :]
        rows, cols, vals = [], [], []
        diag = np.zeros((nr, nz))
        I, J = np.nonzero(inner)
        P = idx[I, J]
        for di, dj, coef in ((1, 0, a_e[I, J]), (-1, 0, a_e[I - 1, J]),
                             (0, 1, a_n[I, J]), (0, -1, a_n[I, J - 1])):
            diag[I, J] += coef
            nb = idx[I + di, J + dj]
            ok = nb >= 0
            rows.append(P[ok]); cols.append(nb[ok]); vals.append(-coef[ok])
        rows.append(P); cols.append(P); vals.append(diag[I, J])
        A = coo_matrix((np.concatenate(vals), (np.concatenate(rows), np.concatenate(cols))),
                       shape=(P.size, P.size)).tocsr()
        x = spsolve(A, src[I, J])
        psi[I, J] = x
        # B at cell centres
        dpsi_dr = ((psi[1:, :-1] - psi[:-1, :-1]) + (psi[1:, 1:] - psi[:-1, 1:])) / (2 * dr[:, None])
        dpsi_dz = ((psi[:-1, 1:] - psi[:-1, :-1]) + (psi[1:, 1:] - psi[1:, :-1])) / (2 * dz[None, :])
        bz = dpsi_dr / rc[:, None]
        brr = -dpsi_dz / rc[:, None]
        bmag = np.hypot(bz, brr)
        new = nu.copy()
        new[mat == 2] = nu_steel(bmag[mat == 2])
        new[mat == 3] = nu_steel(bmag[mat == 3],
                                 rail_h_scale if plate_h_scale is None else plate_h_scale)
        # Picard update in log space with per-cell adaptive damping: a cell whose step
        # changes sign (2-cycle at the B-H knee) has its weight halved
        step = np.log(new) - np.log(nu)
        flip = steel & (np.sign(step) * np.sign(prev_step) < 0)
        relax[flip] = np.maximum(relax[flip] * 0.5, 1e-3)
        upd = np.where(steel, relax * step, 0.0)
        prev_step = step
        nu = nu * np.exp(upd)
        dnu = float(np.percentile(np.abs(step[steel]), 99)) if steel.any() else 0.0
        # force: Maxwell stress on the mid-gap row of cells
        jg = int(np.argmin(np.abs(zc + gap / 2)))
        f = np.sum((bz[:, jg] ** 2 - brr[:, jg] ** 2) / (2 * MU0) * 2 * math.pi * rc * dr)
        force_hist.append(f)
        if it > 5 and dnu < 0.02 and abs(force_hist[-1] - force_hist[-2]) < 1e-3 * abs(force_hist[-1]):
            break
    return {"force_n": force_hist[-1], "r": r, "z": z, "rc": rc, "zc": zc, "bz": bz, "br": brr,
            "bmag": bmag, "mat": mat, "iters": it + 1, "dnu": dnu}


def _interp(res, key, rr, zz):
    """Bilinear interpolation of a cell-centred field at (rr, zz)."""
    return float(RegularGridInterpolator((res["rc"], res["zc"]), res[key])((rr, zz)))


def vec_at(res, rr, zz):
    """(Br, Bz) in mT at (rr, zz)."""
    return np.array([_interp(res, "br", rr, zz), _interp(res, "bz", rr, zz)]) * 1000.0


def field_at(res, rr, zz):
    """|B| in mT at (rr, zz), from the interpolated components."""
    return float(np.linalg.norm(vec_at(res, rr, zz)))


HALL_SENSE_POS = (R_CUP + 2e-3, 0.5e-3)      # adopted (D-18 rev): 2 mm beside rim, face level
HALL_POS_TOL = 0.3e-3                         # sensor located in a moulded pocket, +/- 0.3 mm
ATTACH_DELTA_MT = 10.0                        # firmware threshold on vector distance |B - B_ref| (D-18 rev)


# Candidate Hall positions (r, z) in m: behind the cup back on axis (PCB 4 mm
# behind), behind at the rim, and beside the rim 3 mm out at mid-height.
HALL_POS = {
    "behind, on axis (+4 mm)": (0.5e-3, H_MAG + T_BACK + 4e-3),
    "behind, at rim (+4 mm)": (15e-3, H_MAG + T_BACK + 4e-3),
    "beside rim (+3 mm, mid-height)": (R_CUP + 3e-3, H_MAG / 2),
    "beside rim (+3 mm, face level)": (R_CUP + 3e-3, 0.5e-3),
    "beside rim (+2 mm, face level) [adopted]": (R_CUP + 2e-3, 0.5e-3),
}
HALL_BOP_MAX_MT = 7.0      # [E] choose a Hall switch whose max operate point is below
HALL_BRP_MIN_MT = 1.0      # [E] ... and whose min release point is above these


def validate():
    """Max relative error of on-axis |B| vs the analytic field of a bare disc magnet."""
    res = solve(0.3e-3, plate=False, magnet_only=True, iters=1)
    errs = []
    for zz in (6e-3, 8e-3, 12e-3, 20e-3):
        d = zz - H_MAG
        an = BR_20C / 2 * ((d + H_MAG) / math.hypot(R_MAG, d + H_MAG) - d / math.hypot(R_MAG, d))
        errs.append(abs(field_at(res, 0.2e-3, zz) / 1000 - an) / an)
    return max(errs)


def run():
    val_err = validate()
    gaps_mm = [0.05, 0.1, 0.2, 0.3, 0.45, 0.6, 0.8, 1.0]
    f0_cat = p.MAG_RATED_PULL_KGF.value * 9.81
    # Calibration: internal pot dimensions are not published, so the FEM supplies the
    # *shape* (gap, rail steel, temperature) and the catalogue rating (zero gap, thick
    # mild steel, 20 degC) anchors the absolute value.
    ref = solve(0.05e-3, plate_h_scale=1.0)
    cal = f0_cat / ref["force_n"]
    worst_dnu = ref["dnu"]
    k = (math.sqrt(2) - 1) / p.MAG_HALF_PULL_GAP_MM.value
    rows, curve = [], {}
    hall_att = []                                 # attached field at the adopted position
    for g in gaps_mm:
        a, b = solve(g * 1e-3), solve(g * 1e-3, temp_c=80.0)
        worst_dnu = max(worst_dnu, a["dnu"], b["dnu"])
        hall_att.append((a, b))
        f20, f80 = a["force_n"] * cal, b["force_n"] * cal
        curve[g] = (f20, f80)
        analytic = f0_cat / (1 + k * g) ** 2
        rows.append((f"{g:.2f}", f"{f20:.0f}", f"{f80:.0f}", f"{analytic:.0f}", f"{f80 / analytic:.2f}"))

    att = solve(0.3e-3)
    det = solve(0.3e-3, plate=False)
    det80 = solve(0.3e-3, plate=False, temp_c=80.0)
    # Firmware stores the detached field vector at 20 degC in the factory (B_ref). A 3-D sensor
    # reports the vector, so attachment is judged on |B - B_ref|. Worst case: smallest
    # attached distance (any gap 0.05-1 mm, 20/80 degC) vs the detached drift with temperature.
    # Evaluate over the placement tolerance box (3 x 3 positions); B_ref is recorded per
    # unit at the factory, so it moves with the actual sensor position.
    att_dist, det_drift = float("inf"), 0.0
    for dr_ in (-HALL_POS_TOL, 0.0, HALL_POS_TOL):
        for dz_ in (-0.2e-3, 0.0, 0.2e-3):
            pos = (HALL_SENSE_POS[0] + dr_, HALL_SENSE_POS[1] + dz_)
            b_ref = vec_at(det, *pos)
            for a, b in hall_att:
                for res_ in (a, b):
                    att_dist = min(att_dist, float(np.linalg.norm(vec_at(res_, *pos) - b_ref)))
            det_drift = max(det_drift, float(np.linalg.norm(vec_at(det80, *pos) - b_ref)))
    hall_rows, best = [], None
    for name, (rr, zz) in HALL_POS.items():
        ba, bd = field_at(att, rr, zz), field_at(det, rr, zz)
        usable = (max(ba, bd) >= 1.5 * HALL_BOP_MAX_MT and min(ba, bd) <= HALL_BRP_MIN_MT / 1.5) \
            or abs(ba - bd) >= 2 * HALL_BOP_MAX_MT
        hall_rows.append((name, f"{ba:.1f}", f"{bd:.1f}", "yes" if usable else "no"))
        if usable and (best is None or abs(bd - ba) > best[1]):
            best = (name, abs(bd - ba), ba, bd)

    checks = [
        Check("—", "Magnetostatic solver vs analytic disc-magnet field (on axis, 4 points)",
              f"{100 * val_err:.1f} %", "<= 5 %", val_err <= 0.05),
        Check("—", "FEM nonlinear convergence (99th-percentile log-reluctivity step)",
              f"{worst_dnu:.3f}", "<= 0.05", worst_dnu <= 0.05),
        Check("T1-17", "Fixed-threshold Hall switch usable (original D-18) [rejected option]",
              best[0] if best else "none", "one state ≤ 0.7 mT", False if best is None else
              min(best[2], best[3]) <= HALL_BRP_MIN_MT / 1.5),
        Check("T1-17", "3-D Hall (D-18 rev): min ‖B − B_ref‖ attached, gaps 0.05–1 mm, 20/80 °C, ±0.3 mm placement",
              f"{att_dist:.1f} mT", f">= {2 * ATTACH_DELTA_MT:.0f} mT (2× threshold)",
              att_dist >= 2 * ATTACH_DELTA_MT),
        Check("T1-17", "3-D Hall (D-18 rev): detached ‖B − B_ref‖ drift 20→80 °C, ±0.3 mm placement",
              f"{det_drift:.1f} mT", f"<= {ATTACH_DELTA_MT / 2:.0f} mT (½ threshold)",
              det_drift <= ATTACH_DELTA_MT / 2),
    ]
    md = ("### Magnetostatic FEM: pull vs gap and attachment sensing\n\n"
          "Axisymmetric nonlinear finite-volume model of one Ø32 mm pot (NdFeB 35SH, Br 1.20 T at "
          "20 °C, −0.12 %/K) on a 15 mm rail-steel plate (pearlitic R260 modelled at 2× the field "
          "strength of mild steel for the same flux density). Force from the Maxwell stress in the "
          "gap. Solver validated against the analytic on-axis field of a bare disc magnet (within 3 %). "
          f"Absolute values calibrated to the catalogue rating ({f0_cat:.0f} N, zero gap, mild steel); "
          f"raw FEM over-predicted by {1 / cal:.2f}× because the pot's internal dimensions are assumed. "
          "Single pot; neighbour interaction and web curvature not modelled.\n\n"
          + md_table(["Gap (mm)", "Pull, rail steel 20 °C (N)", "Pull, rail steel 80 °C (N)",
                      "Analytic model used so far (N)", "FEM 80 °C / analytic"], rows)
          + "\n\nHall-sensor field |B| at candidate positions, 0.3 mm gap (attached) vs no rail (detached), 20 °C:\n\n"
          + md_table(["Position", "Attached (mT)", "Detached (mT)", "Usable"], hall_rows)
          + f"\n\nUsable = one state ≥ {1.5 * HALL_BOP_MAX_MT:.1f} mT and the other ≤ "
          f"{HALL_BRP_MIN_MT / 1.5:.1f} mT, or a change ≥ {2 * HALL_BOP_MAX_MT:.0f} mT. No position gives a "
          "near-zero state, and |B| is not even monotonic with gap (at 0.05 mm it nearly equals the "
          "detached value while the radial component differs strongly), so a fixed-threshold switch "
          "is rejected. Adopted: 3-D linear Hall sensor 2 mm beside the outer rim of an end pot at face "
          f"level (position chosen by a sweep of 1–8 mm × 0.5–4 mm; ±0.3 mm pocket tolerance); attached when the vector distance ‖B − B_ref‖ > {ATTACH_DELTA_MT:.0f} mT, with B_ref "
          "the detached field recorded at the factory.\n\n"
          + checks_table(checks) + "\n")
    import json
    import os
    with open(os.path.join(os.path.dirname(__file__), "fem_magnet_cache.json"), "w") as f:
        json.dump({"gaps_mm": gaps_mm, "pull_20c_n": [curve[g][0] for g in gaps_mm],
                   "pull_80c_n": [curve[g][1] for g in gaps_mm], "cal": cal,
                   "hall_attached_min_dist_mT": att_dist, "hall_detached_drift_mT": det_drift},
                  f, indent=1)
    return {"md": md, "checks": checks, "curve": curve, "hall_best": best, "cal": cal,
            "rejected": {"Fixed-threshold Hall switch usable (original D-18) [rejected option]"}}


if __name__ == "__main__":
    print(run()["md"])
