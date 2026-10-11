# Tier 1 Hardware Design (desk-only example, rev B)

Implements decisions D-12 to D-29. Rev B adds the results of the magnetostatic, thermal and modal analyses. Numbers marked → come from the models in `../models` (see `../verification/report.md`).

## 1. Mechanical arrangement

```
        RAIL HEAD (overhang 27-28 mm)
  ══════════════════════════╗
                            ║ ← head stop (moulded rib, 1.5 mm clearance)
   web │ ┌──────────────────╨──────────────────────────────┐
       │ │ [pot A]      [pot C = sensing]      [pot B]     │  ← 3 × Ø32 NdFeB SH pots on the web face,
       │ │  PU coat      bare face              PU coat    │    in a row along the rail (110 mm body)
       │ │    └────── steel guard bridge ──────┘           │  ← guard ties A and B, covers C's back
       │ │             (TMP117 #1 on C's back,             │
       │ │              behind 4 kV silicone pad)          │
       │ │  ─────────── foam wall ───────────              │
       │ │  PCB + module      D-cell + HLC     [air pocket]│  ← TMP117 #2 in louvred pocket, lower face
       │ └──────────────┬──────────────────────────────────┘
       │                │ hook slider (14-50 mm, 1 mm detents, locked by security screw)
  ═════╧════════════════╧══ RAIL FOOT (top surface)
```

- **Body:** PA6-GF30, UV-stabilised, light grey (D-31), 2.5 mm walls; white ASA top cap shading the sensing pot (D-12). 114 × 62 × 53 mm, pots at 35 mm pitch (D-30). All parts are placed in `models/geometry.py`; the packaging check finds no clashes for either SKU. Mass → 536 g from geometry (limit 600 g). Interactive 3-D view: `viewer/rail-sensor-3d.html`.
- **Magnets:** 3 × Ø32 mm NdFeB SH-grade (≥150 °C) steel pot magnets, about 34 kgf rated each (cup back ≥4 mm so it does not saturate). They sit on short elastomer seats so each pot lies flat on the curved web. Outer pots A and B carry a 0.1 mm PU friction coat; the centre pot C is bare (thermal contact). → calibrated FEM pull on rail steel at 80 °C and 0.4 mm worst gap (brushed spots, D-24): 453 N vs 378 N pull-off design load; friction 174 N vs 135 N longitudinal design load.
- **Sensing pot C (D-12):** TMP117 #1 bonded to the back face of C's steel cup through a 0.5 mm silicone pad (≥4 kV breakdown, ~3 W/m·K). A steel guard bridge (40 g) is bolted with thermal compound to threaded studs on the backs of A and B (D-32) and covers C, so the sensor's surroundings sit at rail temperature. Foam lining between guard and body. → thermal FEM: contact conductance 0.35 / 0.19 W/K, capacity 23 J/K; mounting error 0.25 / 0.24 °C (nominal / worst) with lag compensation.
- **Vertical capture (D-14, D-25):** a moulded head-stop rib on the body top, plus a PA6-GF30 hook slider with 10 mm root thickness and a 6 mm lip lever bearing on the foot top. → design load 765 N, root stress 9.2 MPa vs 20 MPa fatigue allowable at 80 °C; first mode 2.7 kHz.
- **Installation:** wire-brush three contact spots on the web to bare metal (D-24), set the slider to the profile mark (48 / 57 / 60), place the unit on the field side mid-crib, let the magnets pull it onto the web, slide it up under the head to the stop, then tighten the security screw to lock the slider.
- **Sealing and internal mounting:** lid O-ring (IP68 target), ePTFE pressure-equalising vent (D-20), no connectors and no cable glands. The cell sits in a rigid PA cradle with a 0.5 mm Shore-90 PU liner (D-27, first mode 5.7 kHz) and stays replaceable. The PCB has corner screws plus a centre standoff (D-26, 5.0 kHz) and is conformally coated.

## 2. Electrical isolation (TN-01 to TN-03)

| Path from rail-potential metal to circuit | Barrier | Rating basis |
|---|---|---|
| Pot C cup → TMP117 #1 | 0.5 mm silicone pad + flex coverlay | ≥4 kV DC (T1-14), 1.5 kV production hipot |
| Guard bridge / pots A, B → PCB, cell | ≥8 mm solid polymer + foam | creepage/clearance well above the 4 kV need [E] |
| Hook slider, head stop | Polymer only | no metal |
| Any external conductor | None exists | TN-03 |

The circuit therefore has no galvanic connection to rail or earth; it floats and follows rail potential only through stray capacitance.

## 3. Electronics

### 3.1 Block and power tree
```
 Li-SOCl₂ D-cell 3.6 V ─┬─ HLC (pulse buffer) ─┬─► module VDD (SKU-L: STM32WLE5 module, 1.8–3.6 V)
                        │                      │            (SKU-C: nRF9151 module, VBAT)
                        │                      ├─► TMP117 ×2, LIS2DW12, TMAG5273, SPI NOR (switched)
                        └─ divider + FET ──────┴─► ADC: battery voltage under load
 No DC-DC converter (D-17). Sensor and flash rails switched off by GPIO between samples.
```

### 3.2 Interfaces (both SKUs use the same baseboard nets)

| Net | Function | Notes |
|---|---|---|
| I2C_SCL / I2C_SDA | TMP117 #1 (addr 0x48, on 60 mm flex), TMP117 #2 (0x49) | 100 Ω series + ESD diodes at the flex entry |
| SPI_x 4-wire | 8 Mbit ultra-low-power SPI NOR (log ≥ 4 608 records + config) | Deep power-down between writes |
| INT_IMU | LIS2DW12 wake-up / free-fall interrupt (movement, tamper) | Always-on, ~1 µA low-power mode |
| I2C (shared) | TMAG5273-class 3-D linear Hall sensor 2 mm (±0.3 mm, moulded pocket) beside the outer rim of end pot A, at face level (attachment, D-18 rev) | Wake-up-on-threshold mode, ~µA average; the port layer computes ‖B − B_ref‖ > 10 mT and feeds the core's `hall attached` input; B_ref stored at the factory |
| LID_REED | Reed switch to a magnet in the lid (D-19) | Pull-up switched with the sensor rail |
| VBAT_SENSE_EN / VBAT_ADC | Battery measurement under load | Enabled only during TX |
| SNS_PWR_EN | Switches power to sensors and flash | |
| RF | Internal antenna: 868 MHz FPC (SKU-L) or LTE-M FPC (SKU-C) | Mounted in the top cap, away from the steel |
| SIM | MFF2 eSIM (SKU-C only) | |
| SWD | Factory programming pads (Tag-Connect) | Internal only, sealed under the lid |

Both candidate modules provide one I2C, one SPI, at least 4 GPIOs with wake-up interrupts and one ADC input, which is all the baseboard needs. The Hall sensor's interrupt pin uses the GPIO freed by removing the Hall switch.

### 3.3 Electronics BOM
As in [`docs/design/tier1-core-design.md`](../../../docs/design/tier1-core-design.md) §5, with three changes: the brass slug and spring are removed; a steel guard bridge (≈US$0.60) and a reed switch with lid magnet (≈US$0.40) are added; and the Hall switch is replaced by a 3-D Hall sensor (≈+US$0.70). Net effect is roughly cost-neutral; the indicative BOM stays at about US$38–60 (SKU-L) and US$50–75 (SKU-C).

## 4. Firmware and data
See [`../firmware/README.md`](../firmware/README.md) for the state machine, payload format v0 (with the 18-byte config downlink from D-11) and the lag compensation (D-13); and [`../platform/codec.py`](../platform/codec.py) for the platform-side decoder.

## 5. Design analyses
Done (rev B), all rerun by `verification/run_all.py`:
1. Axisymmetric nonlinear magnetostatic FEM of one pot on rail steel (`models/fem_magnet.py`): pull vs gap at 20/80 °C, calibrated to the catalogue rating; Hall field at candidate positions.
2. Axisymmetric thermal FEM of the sensing pot (`models/fem_thermal.py`): contact conductance and capacity feeding the lumped day-cycle model.
3. Modal estimates of the hook lip, PCB and cell mount (`models/modal.py`).

Still open (desk-doable, need 3-D tools):
1. 3-D magnetostatic model with all three pots, the curved web and the guard bridge (R-11); the Hall margins are thin (22.8 vs 20 mT separation, 4.8 vs 5 mT drift). A cheap improvement to evaluate: scale B_ref by the magnet temperature coefficient using the rail reading.
2. 3-D thermal model including the guard bridge and housing.
3. Antenna simulation of the FPC antennas next to rail steel (to replace the assumed −5 dBi).
