"""Axisymmetric conduction model of the sensing pot (pot C) on the rail web.

Replaces two lumped inputs of thermal.py variant C with values derived from
geometry and a contact coefficient:
  G_contact  rail -> sensor-pad location (W/K), via the rim and magnet faces
  C_elem     heat capacity of cup + magnet (J/K)
Steady state, finite volumes on an (r, z) grid. Face z = 0 touches the rail
through a contact film (Robin, h_c); the TMP117 pad (5 x 5 mm, 0.5 mm silicone,
k = 3 W/m.K) is a Robin patch on the back face; other faces adiabatic (their
small losses are the separate leak terms in thermal.py).
"""
import json
import math
import os

import numpy as np
from scipy.sparse import coo_matrix
from scipy.sparse.linalg import spsolve

from common import Check, md_table, checks_table
from fem_magnet import R_CUP, R_CUP_IN, R_MAG, H_MAG, T_BACK

K_STEEL, K_NDFEB, K_GAP = 50.0, 9.0, 0.2          # W/m.K [R typ.]; gap = epoxy fill
RHO_C_STEEL = 7850 * 460.0                         # J/m3.K
RHO_C_NDFEB = 7500 * 440.0
PAD_AREA = 25e-6                                   # m2
H_PAD = 3.0 / 0.5e-3                               # W/m2.K
R_PAD = math.sqrt(PAD_AREA / math.pi)
# Contact film coefficient through brushed rail (D-24) [E]:
#   nominal 3000 W/m2K: ~0.1 mm residual scale (k ~0.3) with metal contact spots under 330 N
#   worst    500 W/m2K: ~0.1 mm still-air film over most of the face (seat mismatch)
H_CONTACT = {"nominal": 3000.0, "worst": 500.0}
CACHE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "fem_thermal_cache.json")


def conductance(h_c, n=0.1e-3):
    z_top = H_MAG + T_BACK
    r = np.arange(0, R_CUP + n / 2, n)
    z = np.arange(0, z_top + n / 2, n)
    nr, nz = len(r) - 1, len(z) - 1                # cells
    rc, zc = (r[:-1] + r[1:]) / 2, (z[:-1] + z[1:]) / 2
    R, Z = np.meshgrid(rc, zc, indexing="ij")
    k = np.full((nr, nz), K_STEEL)
    k[(R < R_MAG) & (Z < H_MAG)] = K_NDFEB
    k[(R >= R_MAG) & (R < R_CUP_IN) & (Z < H_MAG)] = K_GAP
    idx = np.arange(nr * nz).reshape(nr, nz)
    rows, cols, vals = [], [], []
    diag = np.zeros((nr, nz))
    rhs = np.zeros((nr, nz))
    # radial faces between cell i and i+1: area 2*pi*r_face*dz, harmonic k
    kf = 2 * k[:-1] * k[1:] / (k[:-1] + k[1:])
    g_r = kf * 2 * math.pi * r[1:-1, None] * n / n
    # axial faces between cell j and j+1: area 2*pi*rc*dr
    kz = 2 * k[:, :-1] * k[:, 1:] / (k[:, :-1] + k[:, 1:])
    g_z = kz * 2 * math.pi * rc[:, None] * n / n
    for a, b, g in ((idx[:-1], idx[1:], g_r), (idx[:, :-1], idx[:, 1:], g_z)):
        rows += [a.ravel(), b.ravel()]; cols += [b.ravel(), a.ravel()]
        vals += [-g.ravel(), -g.ravel()]
    np.add.at(diag, (slice(0, nr - 1), slice(None)), g_r)
    np.add.at(diag, (slice(1, nr), slice(None)), g_r)
    np.add.at(diag, (slice(None), slice(0, nz - 1)), g_z)
    np.add.at(diag, (slice(None), slice(1, nz)), g_z)
    # rail contact on z = 0 (series: half-cell conduction + film), T_rail = 1
    area_r = 2 * math.pi * rc * n
    contact = (rc < R_MAG) | (rc >= R_CUP_IN)          # magnet face and cup rim
    g_c = np.where(contact, 1 / (1 / (h_c * area_r) + (n / 2) / (k[:, 0] * area_r)), 0.0)
    diag[:, 0] += g_c
    rhs[:, 0] += g_c * 1.0
    # pad on the back face, T_sensor = 0
    g_p = np.where(rc < R_PAD, 1 / (1 / (H_PAD * area_r) + (n / 2) / (k[:, -1] * area_r)), 0.0)
    diag[:, -1] += g_p
    rows.append(idx.ravel()); cols.append(idx.ravel()); vals.append(diag.ravel())
    A = coo_matrix((np.concatenate(vals), (np.concatenate(rows), np.concatenate(cols))),
                   shape=(nr * nz, nr * nz)).tocsr()
    t = spsolve(A, rhs.ravel()).reshape(nr, nz)
    q = float(np.sum(g_p * t[:, -1]))                  # W per K of rail-sensor difference
    g_pad = H_PAD * PAD_AREA
    g_contact = 1 / (1 / q - 1 / g_pad)                # strip the pad, as thermal.py adds it
    vol = 2 * math.pi * rc[:, None] * n * n * np.ones((nr, nz))
    cap = float(np.sum(np.where(k == K_NDFEB, RHO_C_NDFEB, np.where(k == K_STEEL, RHO_C_STEEL, 0)) * vol))
    return g_contact, cap


def run():
    rows, res = [], {}
    for case, h in H_CONTACT.items():
        g, c = conductance(h)
        g_fine, _ = conductance(h, n=0.05e-3)
        res[case] = g
        rows.append((case, f"{h:.0f}", f"{g:.3f}", f"{g_fine:.3f}", f"{c:.1f}", f"{c / g:.0f}"))
    cap = conductance(H_CONTACT["nominal"])[1]
    with open(CACHE, "w") as f:
        json.dump({"g_contact_nominal": res["nominal"], "g_contact_worst": res["worst"],
                   "c_elem": cap}, f, indent=1)
    grid_ok = all(abs(float(r[2]) - float(r[3])) / float(r[3]) < 0.03 for r in rows)
    checks = [Check("—", "Thermal FEM grid convergence (0.10 vs 0.05 mm cells)",
                    "within 3 %" if grid_ok else "not converged", "< 3 %", grid_ok)]
    md = ("### Thermal FEM of the sensing pot\n\n"
          "Axisymmetric steady conduction through pot C (steel cup k 50, NdFeB k 9, epoxy gap k 0.2) "
          "from the rail contact film to the TMP117 pad. Results replace the assumed lumped values in "
          "the thermal model (previously G_contact 0.50 / 0.15 W/K, C 28 J/K).\n\n"
          + md_table(["Case", "Contact film h (W/m²K)", "G_contact (W/K)", "G_contact, fine grid (W/K)",
                      "C_elem (J/K)", "C/G (s)"], rows)
          + "\n\n" + checks_table(checks) + "\n")
    return {"md": md, "checks": checks}


if __name__ == "__main__":
    print(run()["md"])
