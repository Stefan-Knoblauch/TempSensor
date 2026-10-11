# Decision and Assumption Register (Tier 1, desk-only example)

In this example nobody outside the design team is consulted and nothing is field-tested, so every open item is closed by a recorded designer decision. Each entry says what drove it (a model result, a standard, or judgement) and what would reopen it in a real programme.

## Carried over from the main design track

| ID | Decision | Driver |
|---|---|---|
| D-01 | Tier 1 core first; LoRa and LTE-M SKUs on one baseboard | Product owner |
| D-02 | Design to the Transnet excerpts held (BBC1040, CEE 0059.84, CEE.0228.96); EN 50122-1 where silent | Product owner |

## New in this example

| ID | Decision | Driver (evidence) | Reopen if |
|---|---|---|---|
| D-10 | **Desk-only closure rule.** Open items that would need an operator, lab or field are closed by documented assumptions. Verification is by analysis, simulation and automated tests only. Anything that cannot be verified that way is listed as a residual risk in `02-requirements.md` §4. | Scope of this example | Programme moves to hardware |
| D-11 | **LTE-M reporting cadence:** batch every 4 h while rail < 25 °C, hourly otherwise, every 20 min in HOT; alarms always immediate. Sampling stays at 10 min. | Energy model: the original hourly/every-sample scheme gives 2.5 yr worst-case good coverage (fails ≥ 3 yr); adopted scheme gives 3.9 yr. | Coverage or session-energy data differ |
| D-12 | **Rail sensor on the back of the centre pot magnet's steel cup**, inside a steel thermal guard bridge tied to the outer pots. Replaces the sprung brass slug. | Thermal model: slug unguarded 2.07 °C error; slug guarded 0.61 °C; sensor-on-pot guarded 0.58 °C raw (0.27 °C with D-13; 0.24 °C with FEM-derived inputs, D-29). Mounting budget 0.3 °C. | 3-D thermal FEA disagrees |
| D-13 | **Lag compensation** in firmware: T = raw + 150 s × dT/dt. | Thermal model minimax over nominal/worst contact: 0.25 / 0.27 °C. | Measured time constant differs |
| D-14 | **Retention by geometric capture**: rail-foot hook plus rail-head stop carry vertical loads; magnets carry pull-off; PU friction coat on the two outer pots (centre pot bare for thermal contact). | Retention model: vertical design load 765 N vs 223 N magnet friction; "10 × weight" rule under-states load ~13×. | EN 50125-3 values confirmed lower |
| D-15 | **Envelope** 110 × 62 × 38 mm, hook slider 14–50 mm in 1 mm detents, one SKU for 48 kg, 57 kg and 60E1 rail. | Fit model including ±10 % window tolerance. | Official profile drawings differ |
| D-16 | **LoRa siting rule:** nodes within 1.1 km of a 6 m gateway (1.8 km with a 15 m mast); ADR capped at SF10. | Link model: SF9 range 1.14 km (6 m), 1.80 km (15 m), with 10 dB shadow loss and 10 dB fade margin. Energy is not the limit (≥ 7 yr even at SF12). | Range data differ |
| D-17 | **No DC-DC converter:** both modules run directly from the Li-SOCl<sub>2</sub> cell with an HLC pulse buffer. Model keeps a 0.85 efficiency factor as margin. | Removes quiescent current and EMI source | Module supply range insufficient at end of life |
| D-18 | ~~Attachment sensing by a Hall switch behind an outer pot.~~ **Revised:** a 3-D linear Hall sensor (TMAG5273 class, I²C) 2 mm (±0.3 mm) beside the outer rim of an end pot at face level, position chosen by a sweep. The port layer reports "attached" when the vector distance from the factory-recorded detached field exceeds 10 mT; the accelerometer orientation is a second input. | Magnetostatic FEM: behind the pot the field changes only 13→17 mT; beside the rim \|B\| is non-monotonic with gap, so no fixed threshold works. The vector distance stays ≥ 22.8 mT across gaps 0.05–1 mm, 20/80 °C and the placement tolerance; reference drift ≤ 4.8 mT. (At the first-choice 3 mm position, the interpolated result was 19.4 mT, a fail.) | 3-D FEM with the neighbouring pots gives less separation |
| D-19 | **Lid tamper** by a reed switch and a magnet in the lid (no housing penetration). | IP, simplicity | — |
| D-20 | **Pressure-equalising ePTFE vent** in the housing. | Daily 30–70 °C cycling would pump moisture past seals | — |
| D-21 | **Example site configuration** (for tests and examples only): stress-free temperature 30 °C (midway between -5 and 65 °C rail extremes, per SA practice); L1 = 55 °C, L2 = 60 °C, L3 = 65 °C; hysteresis 1 °C; HOT 35 °C; cool 25 °C; rate-of-rise 8 °C/h. | SA neutral-temperature practice (research §1); level spacing follows the UK three-level structure | Transnet supplies site values |
| D-22 | **Rail profiles:** 48 and 57 kg/m dimensions taken from a supplier's ISCOR table (unverified); 60E1 from EN 13674-1. | Only data available | Official drawings obtained |
| D-23 | **Bonding interpretation:** passive magnets touching the rail are not "bonding to the return rail"; the 4 kV isolation barrier covers a stricter reading anyway. | BBC1040 excerpt | Transnet rules otherwise |
| D-24 | **Installation wire-brushes the three contact spots** to bare metal. Worst-case gap budget falls from 0.6 to 0.4 mm. | Magnetostatic FEM: on rail steel at 80 °C, pull at 0.6 mm is 116 N/pot (analytic fit said 149 N), giving 348 N total vs 378 N pull-off load (fail). At 0.4 mm: 453 N (pass). It also improves thermal contact. | Installers skip it (procedure and training) |
| D-25 | **Hook lip lever 6 mm** (was 10 mm). | Modal: first mode 1.26 kHz inside the assumed band → 2.71 kHz; root stress 15.3 → 9.2 MPa. | Band upper limit confirmed higher |
| D-26 | **PCB centre standoff** in addition to the corner screws. | Modal: 1.24 kHz → 4.96 kHz | — |
| D-27 | **D-cell in a rigid PA cradle with a 0.5 mm Shore-90 PU liner** (not soft EPDM pads); the cell stays replaceable. | Modal: 0.89 kHz → 5.66 kHz | — |
| D-28 | **LTE-M send back-off** for data/status when the link state is unknown (10, 20, 40, 80, 120 min, then every 120 min); alarms still every tick. Reset persistence of state class, config, token, seq block and energy (A/B slots with CRC). | Energy model with 10 % outage time: retrying every tick → 1.4 yr; back-off → 3.3 yr. Persistence keeps a stolen unit LOCKED across a battery pull. | — |
| D-29 | **Thermal inputs from geometry.** The contact conductance (0.35 / 0.19 W/K) and capacity (23 J/K) of the sensing pot come from the axisymmetric thermal FEM with a 3000 / 500 W/m²K contact film. The firmware τ = 150 s is kept: error 0.25 °C vs 0.23 °C at the new optimum of 130 s, both within budget. | Thermal FEM + lumped model | Measured contact film differs |
