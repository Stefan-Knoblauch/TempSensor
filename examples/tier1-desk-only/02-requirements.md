# Tier 1 Requirements Baseline, rev B (desk-only example)

Rev A is §3 of [`docs/design/tier1-core-design.md`](../../docs/design/tier1-core-design.md). Rev B adds what the analyses forced (marked **changed** or **new**). Verification column: **An** = analysis model, **FW** = firmware automated test, **PF** = platform automated test, **DR** = design review (inspection of this repository). Results are in [`verification/report.md`](verification/report.md).

## 1. Requirements and verification status

| ID | Requirement | Rev B change | Verified by | Desk status |
|---|---|---|---|---|
| T1-01 | Rail channel -30..+90 °C, ±0.5 °C installed (TMP117 0.2 + mounting 0.3) | Mounting error met via D-12/D-13 | An (thermal), FW (lag comp.) | Verified by analysis; residual R-1 |
| T1-02 | Near-rail air channel, labelled "near-rail air" | Model shows +4 to +8 °C warm bias in sun; label confirmed essential | An (thermal) | Verified (behaviour characterised) |
| T1-03 | Sample every 10 min | — | FW | Verified by test |
| T1-04 | Reporting cadence | **changed (D-11):** LoRa every 10 min (n = 2); LTE-M 4 h below 25 °C, 1 h normal, 20 min in HOT | FW, An (energy) | Verified |
| T1-05 | Alarms first, ≤5 min where a link exists | — | FW | Verified (alarm sent on the tick it is raised) |
| T1-06 | ≥30-day log, back-fill, no loss or duplication | — | FW, PF | Verified by test |
| T1-07 | Life: LoRa ≥7 yr; LTE-M ≥3 yr in good coverage | Met with D-11 | An (energy) | Verified by analysis; residual R-3 |
| T1-08 | Electronics -20..+85 °C | Housing peak 64 °C (worst) | An (thermal) | Verified by analysis |
| T1-09 | IP67 min, IP68 target | Vent added (D-20) | DR | Design only; residual R-5 |
| T1-10 | Survive EN 50125-3 on-rail vibration/shock | **changed (D-14):** vertical capture by hook and head stop | An (retention) | Verified by analysis; residual R-2 |
| T1-11 | Retention on rusty/painted/curved web | **changed:** criterion is now 1.5 × 3σ inertial load per axis, replacing "10 × weight" | An (retention) | Verified by analysis; residual R-2 |
| T1-12 | Mass ≤0.6 kg; fits Transnet profiles | One SKU, hook slider (D-15) | An (fit, mass) | Verified by analysis; residual R-6 |
| T1-13 | TN-01..TN-06 Transnet-derived | Interpretation D-23 | DR (hardware §2) | Design only |
| T1-14 | Isolation barrier 4 kV DC | — | DR | Design only; residual R-5 |
| T1-15 | EMC EN 50121-4 / -2 | — | DR | Not verifiable desk-only; residual R-4 |
| T1-16 | No 2G/3G; EU868 within ICASA limits | — | An (LoRa duty cycle), DR | Verified |
| T1-17 | Detachment/movement alarm with location hint | — | FW | Verified by test (location via network metadata) |
| T1-18 | Firmware lock | — | FW | Verified by test |
| T1-19 | Exterior free of copper, brass, aluminium | Slug removed; all metal internal or magnet faces | DR | Verified by inspection of the design |
| T1-20 | Three alarm levels per site, mandatory config | Config extended to 18 B (D-11) | FW, PF | Verified by test |
| T1-21 | **new:** LoRa nodes within 1.1 km of a 6 m gateway (1.8 km at 15 m) | D-16 | An (link) | Siting rule; residual R-3 |
| T1-22 | **new:** rail channel lag-compensated, τ = 150 s | D-13 | FW, An | Verified |
| T1-23 | **new:** hook and head stop carry ≥765 N; hook root ≤20 MPa | D-14 | An (retention) | Verified by analysis |

## 2. Requirements removed or replaced
- "Retention ≥ 10 × weight in shear" is replaced by T1-11 rev B: it under-stated the vertical design load by about 13×.
- "Sprung brass contact slug" is a design solution, not a requirement, and is superseded by D-12.

## 3. Interfaces fixed in this revision
- Payload v0 with the 18-byte config downlink (see `firmware/README.md`), cross-checked between the firmware and the platform codec through generated vectors.

## 4. Residual risks (cannot be retired desk-only)

| ID | Risk | Closest desk mitigation done | What would retire it |
|---|---|---|---|
| R-1 | Real contact conductance through rust differs from the 0.05–0.50 W/K range | Two-case model; lag compensation tuned minimax | Hot-plate rig, then field comparison |
| R-2 | EN 50125-3 values or axis mapping wrong; magnet creep under real traffic | Per-axis 1.5 × 3σ design; geometric capture | Confirm the standard; shaker test; field |
| R-3 | Rail-level RF loss differs from the 10 dB assumption; LTE-M session energy | Conservative margins; siting rule | Range test; coverage survey |
| R-4 | EMC immunity to traction return-current fields | No external conductors, short flex, filtering | EN 50121-4 test |
| R-5 | Sealing and isolation workmanship | Vent, no penetrations, defined hipot | IP and hipot tests |
| R-6 | 48/57 kg/m dimensions from an unverified source | ±10 % window tolerance covered by the slider | Official drawings |
| R-7 | Interaction with axle counters and track circuits | Field-side mounting, ≥2 m clearance rule | Signalling test |
| R-8 | Theft rate | Low-value design, lock, alarms | Pilot deployment |
