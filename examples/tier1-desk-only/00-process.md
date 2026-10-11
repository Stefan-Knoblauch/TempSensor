# Desk-Only Design Process (example)

This example runs the methodology in [`docs/design/design-methodology.md`](../../docs/design/design-methodology.md) for the Tier 1 core under one extra constraint: **no field testing, no lab work and no manual steps** (no operator meetings, no hand calculations, no CAD by hand). It is done seriously: every claim is backed by a runnable model or an automated test, and anything that cannot be shown that way is recorded as a residual risk rather than assumed away.

## How the gates change

| Gate | Normal programme | In this example |
|---|---|---|
| G0 Needs | Research + operator input | Research only |
| G1 Requirements | Reviewed with operator | Baseline from `tier1-core-design.md`, revised by analysis (`02-requirements.md`) |
| G2 Architecture | Design review | Same, recorded in decisions D-10 to D-23 |
| G3 Detailed design | Schematics, CAD, firmware | Hardware design description, parametric geometry, **working firmware core** and **platform codec** |
| G4 Lab qualification | Chamber, shaker, EMC, hipot | **Virtual qualification**: thermal, retention, energy, link and fit models; firmware HIL simulation |
| G5 Field trial | Pilot on a live line | **Removed.** Field-only questions go to the residual-risk list. |
| G6 Operator acceptance | Transnet approval | **Removed.** Interpretations recorded as decisions (D-23). |

## Rules followed
1. **One parameter file.** Every number used by a model is in `models/params.py` with an evidence tag, so a changed assumption flows through every result.
2. **Models drive decisions.** When a check fails, the design is changed, not the limit. Rejected options stay visible in the report (for example the unguarded slug and magnet-only retention).
3. **Independent cross-checks.** The platform codec is written from the specification, separately from the firmware, and both are tested against the same generated vectors.
4. **One command re-verifies everything:** `python3 verification/run_all.py` reruns all models and tests and regenerates `verification/report.md`.
5. **Residual risks are explicit.** Anything that can only be retired by hardware or field work is listed with the analysis that would come closest.

## What the analysis changed (compared with the concept in `tier1-core-design.md`)

| Concept assumption | What the models showed | Change |
|---|---|---|
| Sprung brass slug, PTFE-isolated | ~2 °C error because the cooler housing pulls heat from the slug | Sensor on the centre pot cup + thermal guard + lag compensation (D-12, D-13) |
| Magnets sized at 10 × weight in shear | On-rail vibration loads ~13× higher vertically | Geometric capture with a hook and head stop (D-14) |
| LoRa life limited by SF (~6 yr at SF9) | Energy is not the limit; range is (~1.1 km) | Siting rule instead of an SF limit for energy (D-16) |
| LTE-M hourly + every sample in HOT gives ≥ 3 yr | 2.5 yr in worst-case good coverage | Cool/normal/hot cadence (D-11) |
| One hook fits all rails | Window differs 88–101 mm across profiles | Hook slider with detents (D-15) |
