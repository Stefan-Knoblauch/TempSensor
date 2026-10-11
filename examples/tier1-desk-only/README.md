# Example: Tier 1 Core, Desk-Only Design

A worked example of the design methodology carried through detailed design for the Tier 1 rail unit, **with no field testing, no lab work and no manual steps**. Every result is produced by code in this folder and can be regenerated with one command. This is kept separate from the main design track (`docs/design/`), which still plans for lab and field work.

## Read in this order
1. [`00-process.md`](00-process.md): how the gates were adapted, and what the analysis changed.
2. [`01-decisions.md`](01-decisions.md): decision and assumption register (D-10 to D-29).
3. [`02-requirements.md`](02-requirements.md): requirements rev B, verification status and residual risks.
4. [`hardware/hardware-design.md`](hardware/hardware-design.md): mechanical, isolation and electronics design.
5. [`firmware/README.md`](firmware/README.md): portable firmware core (C99) with simulated-hardware tests.
6. [`platform/codec.py`](platform/codec.py): platform payload decoder and ingest, tested against firmware vectors.
7. [`verification/report.md`](verification/report.md): generated verification report.

## Re-run everything
```
python3 verification/run_all.py
```
Needs Python 3.10+ with numpy and scipy (`pip install -r requirements.txt`), gcc and make. Takes about a minute. Exits non-zero if any check or test fails.

## Layout
```
models/        analysis models and field solvers (fem_magnet.py, fem_thermal.py); inputs in models/params.py
firmware/      C99 firmware core, HAL abstraction, host tests, test-vector generator
platform/      Python codec + ingest, unit tests incl. firmware-vector cross-check
hardware/      hardware design description
verification/  run_all.py, generated report.md and fit_profiles.svg
```
