# Tier 1 Core: Design Specification (G1 baseline, G2 architecture, G3 start)

Status: **Draft 0.1, desk-based.** Parent document: [`design-methodology.md`](design-methodology.md). Evidence tags [R] / [E] / [A] / [F] as defined there. No prototype or field data exists yet.

## 0. Decisions recorded

| ID | Decision | Date |
|---|---|---|
| D-01 | Develop the **Tier 1 core** first. It is the shared platform for Tiers 2 and 3. Both radio SKUs (LoRaWAN and LTE-M) are carried on one baseboard. | 2026-10-11 |
| D-02 | Use the **Transnet specification excerpts we hold** (BBC1040, CEE 0059.84, CEE.0228.96, as quoted in tender documents) as binding design inputs. Where they are silent, apply EN 50122-1 values. The full documents should still be obtained, but the design does not wait for them. | 2026-10-11 |

## 1. Transnet design inputs and what they mean for Tier 1

These are the only Transnet requirements available to us. Each is turned into one or more derived requirements (TN-xx) that the Tier 1 design must meet.

| Source (excerpt) | Text / content as held | Derived requirement for Tier 1 |
|---|---|---|
| **BBC1040** (signalling specification) | "Equipment operating at less than 150 V shall not be deliberately earthed nor shall they be bonded to the return rail." | **TN-01** The device circuit has no intended galvanic connection to earth. **TN-02** The device circuit has no intended galvanic connection to the rail: the metal parts that touch the rail (magnet poles, contact slug) are isolated from the electronics by a rated insulation barrier (Section 4.3). **TN-03** No external cable of any kind leaves the unit. |
| **CEE 0059.84** "Earthing and Bonding 3kV DC Electrification" (referenced by BBC1040) | Signal rooms within 3 m of a DC track bond to the track only via a surge arrester. | **TN-04** The LoRaWAN gateway (LoRa SKU) is preferably sited more than 3 m from any DC track. If it is inside 3 m or in an existing relay room/signal room, it follows that room's existing earthing and surge-arrester arrangement; no new bond to rail is made by us. |
| **CEE.0228.96** "Earthing and bonding masts to rail" | Masts on DC sections are bonded to rail. | **TN-05** The LoRa gateway is **not** mounted on an OHTE mast unless Transnet approves it. If it is, the gateway is treated as being at rail potential and installed as such. Own pole or existing building is the default. |
| (No Transnet document held for AC sections) | Transnet also runs 25 kV AC (e.g. coal line) and 50 kV AC (Sishen–Saldanha ore line) [A, to confirm]. | **TN-06** On AC sections, apply EN 50122-1 touch-voltage values (60 V > 300 s; 785 V at 0.1 s, 2011 edition) [R]. Tier 1 design must not depend on DC-only assumptions. |

**Interpretation to confirm with Transnet:** the magnets and contact slug are passive metal parts that touch the rail and connect to nothing else. We read this as *not* "bonding to the return rail" in the electrification sense (bonding means connecting the rail to another conductive structure). TN-02 adds the isolation barrier so that even a strict reading is satisfied.

**System voltages used for insulation design [E, to confirm against EN 50163]:** 3 kV DC system, maximum non-permanent voltage 3.9 kV; 25 kV AC system, 29 kV maximum non-permanent.

## 2. Scope of the Tier 1 core

**In scope:** rail-mounted unit (housing, magnets, contact slug, near-rail air pocket, electronics, primary battery, firmware), the two radio SKUs, the payload format, and the minimum platform functions needed to commission a unit and raise alerts.

**Out of scope for now:** solar, LTO, ambient standoff shield, relay mesh, GNSS, mast, forecasting. These belong to Tier 2/3 and are added later as modules on this core.

## 3. Requirements baseline (Tier 1)

| ID | Requirement | Tag | Verification |
|---|---|---|---|
| T1-01 | Rail channel range -30 to +90 °C; resolution 0.01 °C; accuracy ±0.5 °C installed, ±0.2 °C sensor-only | R / F | Lab chamber; field |
| T1-02 | Near-rail air channel -20 to +70 °C, reported and labelled as "near-rail air", not ambient | R | Lab |
| T1-03 | Sample every 10 min, all year | A | Test |
| T1-04 | Reporting: LoRa SKU every 10 min; LTE-M SKU hourly batch, every sample sent immediately in HOT mode | R / A | Test |
| T1-05 | Alarm (threshold, tamper, detached, low battery) queued ahead of all other data; delivered ≤5 min where a link exists | A | Test; field |
| T1-06 | ≥30 days on-device log, back-filled on reconnection, no loss or duplication | R | HIL test |
| T1-07 | Life: LoRa SKU ≥7 years at SF7–SF8; LTE-M SKU ≥3 years in good coverage (Section 7) | E | Analysis; current profiling; field |
| T1-08 | Electronics operating range -20 to +85 °C; storage -30 to +85 °C | R | Lab |
| T1-09 | IP67 minimum; IP68 (1 m, 72 h) target | R | Lab |
| T1-10 | Survives EN 50125-3 on-rail vibration and shock without moving more than 5 mm on the web | R / E | Shake table; field |
| T1-11 | Retention ≥10× unit weight in shear on rusty, painted and curved web | E | Lab pull/shear; field |
| T1-12 | Mass ≤0.6 kg; fits the field-side web of Transnet rail profiles without touching head underside or foot top | A | CAD; trial fit |
| T1-13 | TN-01 to TN-06 (Section 1) | R | Design review; dielectric test |
| T1-14 | Isolation barrier rail-metal ↔ circuit: 4 kV DC for 60 s, no breakdown (type test); 1.5 kV DC 1 s (production test) | E / A | Hipot |
| T1-15 | EMC: EN 50121-4 immunity, EN 50121-2 emissions | R | Lab |
| T1-16 | No 2G/3G modules; LoRaWAN EU868 (ICASA SRD limits) or LTE-M/NB-IoT | R | Design review |
| T1-17 | Detachment or movement alarm, with last known location (LoRa: gateway RSSI; LTE-M: cell ID) | R | Test |
| T1-18 | Firmware lock: unit will not operate on another network or after unauthorised opening | R | Test |
| T1-19 | No copper, brass or aluminium visible on the exterior; unbranded; "no copper / no resale value / tracked" marking | R | Inspection |
| T1-20 | Three alarm levels per site (L1/L2/L3), set at commissioning; a site cannot go live without them | A | Test |

## 4. Architecture

### 4.1 Block diagram

```
          RAIL WEB (at rail potential)
   ───────────────────────────────────────────────
     │ magnet poles (×3)      │ brass contact slug
     │                        │
 ════╪════════ ISOLATION BARRIER (≥4 kV DC) ═╪════════════   ← TN-02 / T1-14
     │ (magnets potted in     │ thermal pad (dielectric)
     │  polymer, no PCB link) │
     │                  ┌─────┴──────┐
     │                  │ TMP117 #1  │ rail channel (on flex, 25 mm lead)
     │                  └─────┬──────┘
     │                        │ I²C
 ┌───┴────────────────────────┴────────────────────────────────┐
 │ BASEBOARD (common to both SKUs)                             │
 │  TMP117 #2 (near-rail air pocket)   LIS2DW12 accelerometer  │
 │  Hall sensor (attachment)           1 MB SPI NOR (log)      │
 │  Tamper switch (lid)                TVS + filtering         │
 │  Power path: Li-SOCl₂ D-cell ─► HLC pulse buffer ─► 3.6 V   │
 │  Coulomb counter / energy accounting                        │
 │  ┌───────────────── module footprint ────────────────────┐  │
 │  │ SKU-L: STM32WLE5 module (MCU + LoRa)                  │  │
 │  │ SKU-C: nRF9151 module (MCU + LTE-M/NB-IoT, R17 NTN)   │  │
 │  └───────────────────────────────────────────────────────┘  │
 │  Internal antenna (inside housing, shaded side, up)         │
 └─────────────────────────────────────────────────────────────┘
          Fully insulating housing; circuit floats (TN-01)
```

### 4.2 Why this split
- **Radio SoC as host.** Both candidate modules carry their own MCU, so no separate host processor is needed. This saves cost and sleep current.
- **Common baseboard, swappable module.** "One PCB footprint" (from the research) is met by a common baseboard carrying sensors, power, log memory and IMU, with a module footprint that accepts either radio module. Mechanical, thermal, EMC and IP work is done once.
- **One firmware codebase.** Zephyr RTOS supports both the STM32WL and nRF91 families [R, vendor support], so firmware logic (Section 6) is shared and only the radio driver differs.

### 4.3 Electrical isolation scheme (implements TN-01, TN-02, TN-03)
1. **Magnets:** pot magnets moulded into the polymer housing. Their steel cups touch the rail; nothing inside the housing is connected to them. Minimum internal clearance from any magnet part to the PCB or battery: 8 mm through solid polymer [E].
2. **Contact slug:** brass slug on a spring. Between slug and TMP117 #1 sits a silicone thermal pad (≥0.5 mm, breakdown ≥4 kV) [E]. The sensor flex has no copper within 3 mm of the slug edge.
3. **Circuit:** no conductive path to the outside. Antenna internal. Battery and PCB fully inside the housing. No external connector or cable (TN-03); configuration over the air only.
4. **Result:** the circuit floats and couples to the rail only capacitively. It follows rail potential without being bonded to it, so a person touching the housing touches only insulation, and the unit adds no new path between rail and earth.
5. **Trade-off:** the thermal pad adds thermal resistance and a few minutes of lag. That is acceptable at a 10-minute sample interval [R]; to be measured on the bench [E].

### 4.4 Mechanical concept
- **Envelope (initial):** about 110 × 65 × 38 mm (length along rail × height on web × depth from web) [A]. Depth chosen so the unit sits mostly under the rail-head overhang on the field side. Height to be checked against the 48, 57 and 60 kg/m profiles used by Transnet [A, profiles to confirm].
- **Web fit:** the rail web is curved and tapered, so the magnet pots sit on short compliant elastomer mounts that let each pot seat flat on the web [E].
- **Magnets:** baseline 3 × NdFeB SH-grade pot magnets, Ø25–32 mm, total rated normal pull ≥60 kgf on flat steel [E]. Ferrite pots are a cost-down option to evaluate against T1-11 on rusty web.
- **Contact slug:** centred between the magnets (the magnets also conduct heat from the rail, which helps), spring force about 10–20 N, slug face slightly convex for the curved web [E].
- **Near-rail air pocket:** louvred pocket on the lower shaded face, thermally isolated from the slug side by an internal foam wall [R].
- **Housing:** glass-filled PA or PBT, UV-stabilised, rust-brown body with a light-grey top cap shading the slug area [R]. Battery bay behind a security-Torx lid with a tamper switch; heavy parts (cell, HLC) potted or clamped [R].
- **Removal:** a slot for a lever tool so maintenance staff can remove it before tamping without damaging the housing [A].

## 5. Electronics bill of materials (indicative, 1k units) [E]

| Item | Example part | Est. US$ |
|---|---|---|
| Radio + MCU module (SKU-L / SKU-C) | STM32WLE5-based module / nRF9151 module | 7–10 / 18–25 |
| Rail and near-rail air sensors | 2 × TMP117 | 4–7 |
| Accelerometer | LIS2DW12 | 1 |
| Hall attachment sensor, tamper switch | DRV5032-class, microswitch | 1 |
| Log memory | 1 MB SPI NOR | 0.5 |
| Primary cell | Li-SOCl<sub>2</sub> D-cell ~17 Ah | 8–12 |
| Pulse buffer | Hybrid layer capacitor | 3–6 |
| Power path, TVS, passives, PCB | | 4–6 |
| Antenna (internal) | 868 MHz or LTE band PCB/FPC antenna | 1–2 |
| SIM / eSIM (SKU-C) | MFF2 eSIM | 1–2 |
| Housing, magnets, slug, pad, seals, fasteners | | 8–14 |
| **Total** | | **~38–60 (SKU-L) / ~50–75 (SKU-C)** |

This matches the research's Tier 1 BOM range (US$35–60 LoRa, US$45–75 LTE-M).

## 6. Firmware behaviour

### 6.1 State machine

```
 SHIP ──(Hall: attached ≥60 s)──► COMMISSION ──(joined + site config received)──► RUN
  ▲                                   │ fail ×N: retry with back-off                │
  │                                   ▼                                              ▼
  │                               (stays in COMMISSION, logs locally)        rail ≥ HOT threshold
  │                                                                           or rise ≥ X °C/h
  │                                                                                  ▼
  │                                                                                 HOT
  │                                                                     (immediate reporting)
  │
  │   Any state ─(IMU shock/move or Hall: detached)─► ALARM_TAMPER ─(not reattached in 30 min)─► LOCKED
  │   Any state ─(lid switch opened, no service token)─► LOCKED
  └── LOCKED: sends a periodic "stolen" beacon if a link exists; ignores everything except an authorised unlock
```

- **SHIP:** deep sleep; only the Hall sensor is powered. A unit in storage uses almost no energy.
- **COMMISSION:** joins the network, sends an *install* message (location hint, firmware version, battery state, attachment confirmation) and waits for site configuration (alarm levels L1/L2/L3, HOT threshold, rate-of-rise limit). Logging starts immediately, so no data is lost while waiting.
- **RUN:** samples every 10 min, reports according to SKU (T1-04), sends a daily heartbeat.
- **HOT:** entered when the rail reaches the HOT threshold (default 35 °C [A]) or rises faster than the rate-of-rise limit (default 8 °C/h [A]). In HOT, every sample is sent immediately. HOT is left 1 h after the condition clears.
- **Alarm levels:** crossing L1/L2/L3 upwards or downwards (with 1 °C hysteresis [A]) sends an alarm message immediately in any operating state.
- **Planned removal:** a service command (over the air, or a magnet-wand sequence on the Hall sensor [A]) puts the unit into SHIP so that tamping removal does not raise a theft alarm.

### 6.2 Alarm levels and Transnet practice
We do not hold a Transnet track-maintenance specification for heat-related speed restrictions. Until we do:
- The device and platform carry **three configurable levels per site** (L1 watch, L2 restrict, L3 severe restrict), using the Network Rail CRT(W)/CRT(30/60)/CRT(20) structure as a template [R].
- **No default values ship in the device.** Each site's values are entered by the operator at commissioning, based on that site's stress-free temperature and track condition (T1-20). This avoids embedding UK numbers in a South African product.

### 6.3 Queueing and back-fill
- Three priority queues: alarm > status/install > data backlog.
- Every record carries a device sequence number so the platform can de-duplicate and detect gaps.
- On reconnection, the device sends alarms first, then the newest data, then the backlog oldest-first at a rate limited to stay within duty-cycle (LoRa) or energy budget (LTE-M).

## 7. Payload format v0 (bearer-independent)

All multi-byte fields are big-endian. Temperatures are int16 in 0.01 °C (range ±327.67 °C).

**Data record (5 bytes):** rail temp (int16), near-rail air temp (int16), flags (u8: attached, lid closed, HOT, alarm level 0–3 in 2 bits, low battery).

| Message | Type byte | Body | Size |
|---|---|---|---|
| Data batch | 0x01 | seq (u16), base time (u32, Unix s), interval (u8, min), n (u8), n × record | 9 + 5n B (n = 6 → 39 B) |
| Alarm | 0x02 | seq, time, alarm code (u8), record | 13 B |
| Status / heartbeat | 0x03 | seq, time, battery % (u8), energy used (u16, mAh), FW version (u16), reset count (u8), radio stats (u8) | 15 B |
| Install | 0x04 | seq, time, HW rev, FW version, attach confirmation, battery | ~14 B |

- **LoRa SKU:** every 10 min sends a batch with n = 2 (current and previous sample), 19 B. The repeat covers single lost uplinks without needing confirmed uplinks. At SF7–SF9 this is well within EU868 limits [R].
- **LTE-M SKU:** hourly batch with n = 6, over CoAP/DTLS or MQTT-SN; alarms sent at once.
- **Downlink (both):** configuration (alarm levels, HOT threshold, rate-of-rise limit, reporting interval), service mode, unlock token, time sync.

## 8. Energy budget [E]

Usable capacity taken as 12 Ah of the 17 Ah cell (derating for temperature, pulse load and passivation). Self-discharge about 1 %/yr.

| Item | LoRa SKU (mAh/day) | LTE-M SKU (mAh/day) |
|---|---|---|
| Sleep floor (MCU + RTC + IMU wake-up + Hall), about 8 µA | 0.2 | 0.2 (plus PSM floor ~0.1) |
| Sensing (2 × TMP117 one-shot × 144) | <0.01 | <0.01 |
| Radio, normal running | 144 uplinks × 0.012–0.035 mAh (SF7–SF9) = 1.7–5.0 | 24 sessions × 0.1–0.3 mAh = 2.4–7.2 |
| HOT-mode extra (averaged over a year; assumes 120 hot days × 6 h) | none (already every 10 min) | about 1.5–4.5 |
| **Total** | **~1.9–5.2** | **~4.2–12** |
| **Estimated life on 12 Ah** | **SF7: >10 yr (limited by cell ageing); SF9: about 6 yr** | **good coverage: about 2.6–7 yr; deep coverage (×3 radio): about 1–2.5 yr** |

**What this means for the design:**
- The ≥7-year LoRa target (T1-07) needs nodes to run at SF7–SF8 on average, so the gateway must be close, roughly within 1–2 km of rail-level nodes [E, range to be confirmed in the field].
- The LTE-M SKU meets ≥3 years only in good coverage. At deep-coverage sites the LoRa SKU should be used instead. The coverage survey at each site decides which SKU goes in.

## 9. Verification plan for Tier 1 (G3/G4)

| # | Test | Requirements | Stage |
|---|---|---|---|
| V-01 | Thermal model of slug, pad and housing under 1000 W/m² sun on a hot rail | T1-01 | Desk (now) |
| V-02 | Rail offcut on a hot plate, solar simulator above, reference PRT in the web: reading error and lag | T1-01, T1-02 | Bench |
| V-03 | Thermal chamber calibration -30 to +90 °C against a reference PRT | T1-01, T1-08 | Lab |
| V-04 | Pull and shear test on new, rusty, painted and curved web offcuts (48/57/60 kg/m) | T1-11, T1-12 | Lab |
| V-05 | EN 50125-3 vibration and shock on a rail section | T1-10 | Lab |
| V-06 | IP67/IP68 immersion and dust | T1-09 | Lab |
| V-07 | Hipot 4 kV DC 60 s between rail-contact metal and circuit | T1-13, T1-14 | Lab |
| V-08 | EMC to EN 50121-4 / EN 50121-2, including 50 Hz magnetic field immunity | T1-15 | Lab |
| V-09 | Current profiling per state, both SKUs; update Section 8 | T1-07 | Bench |
| V-10 | Firmware HIL: link loss 30 days, back-fill, de-duplication, alarm priority, HOT entry/exit | T1-03 to T1-06 | Bench |
| V-11 | Tamper, detachment and lock tests | T1-17, T1-18 | Bench |
| V-12 | **Field trial: accuracy against reference, retention under traffic, rail-level radio range, coverage, axle-counter / track-circuit interaction, theft rate** | T1-01, T1-07, T1-10, T1-11, TN-xx | **Field (not yet done; needed before design freeze)** |

## 10. Open items for Tier 1

1. **Transnet confirmation of the TN-02 interpretation** (passive metal touching the rail is not "bonding"); request the full BBC1040, CEE 0059.84 and CEE.0228.96 documents.
2. **Transnet heat-restriction practice:** the values operators will enter for L1/L2/L3 per site.
3. **Rail profile drawings** (48, 57, 60 kg/m) to fix the housing envelope.
4. **Defaults for HOT threshold (35 °C) and rate-of-rise (8 °C/h):** placeholders until reviewed.
5. **Gateway product for the LoRa SKU:** select an off-the-shelf solar/LTE LoRaWAN gateway and a siting rule (TN-04, TN-05).
6. **Network choice:** private LoRaWAN network server versus a commercial network; Vodacom or MTN for LTE-M.
