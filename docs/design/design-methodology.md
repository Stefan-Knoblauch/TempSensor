# Rail Temperature Sensor: Design Methodology and System Layering

Status: **Draft 0.1, desk-based design only.** Source research: [`docs/research/rail-temperature-sensor-research.pdf`](../research/rail-temperature-sensor-research.pdf).

> **Evidence basis.** This draft is built only from desk research and engineering judgement. No field testing, prototype measurement or operator consultation has been done yet. Every requirement and number carries an evidence tag (below) so that we can see which ones still need lab or field data. Nothing tagged **[F]** should be treated as settled until a South African field trial has been run.

## 1. Evidence tags

| Tag | Meaning | What closes it |
|---|---|---|
| **[R]** | Taken from the research (standards, benchmarks, published data) | Check against the licensed standard or primary source |
| **[E]** | Engineering estimate or calculation | Bench or lab measurement |
| **[A]** | Working assumption; a placeholder until someone decides | Decision by the product owner or operator |
| **[F]** | Can only be confirmed in the field (on track, under traffic, in SA conditions) | Field trial (**not yet done**) |

## 2. Design methodology

### 2.1 Guiding principles
1. **One core, three tiers.** All tiers share a single rail-node core: housing, contact slug, magnet system, MCU/logger, firmware framework and data model. Tiers add to that core (connectivity, power, ambient sensing, mast). They never fork it. This keeps qualification cost down, because EMC, vibration and IP testing of the core carries over to every tier.
2. **Safety invariants come before features.** A small set of rules (Section 4) applies to every tier and cannot be traded away for cost or performance.
3. **Connectivity and theft drive the architecture; sensing accuracy is the easier problem.** Design effort and risk reduction are weighted to match.
4. **Consistency with operator practice.** Measure where operators measure (shaded mid-web) and present data in terms they already use (CRT(W) / CRT(30/60) / CRT(20)-style thresholds), so the product replaces the watchman without changing the decision process.
5. **Degrade gracefully.** Every layer has a defined fallback: log locally when the link drops, run on the backup cell when the panel is stolen, send the alarm before the backlog.
6. **Traceability.** Every requirement has an ID, an evidence tag, an owning layer and a verification method.

### 2.2 Process (stage-gated, V-model)

```
 G0 Needs & constraints ─────────────────────────────── G6 Acceptance (operator)
     G1 System requirements ───────────────────────── G5 Field trial  [NOT YET DONE]
         G2 Architecture & layering ──────────────── G4 System / lab qualification
             G3 Layer detailed design ── unit / bench verification
```

| Gate | Output | Status |
|---|---|---|
| G0 Needs & constraints | Research report; operating context (Transnet/PRASA, 3 kV DC / 25 kV AC, theft, coverage) | Done (desk) |
| G1 System requirements | Requirement set with IDs and tags (Section 5) | Draft in this document |
| G2 Architecture & layering | Layer model, interfaces, tier mapping (Sections 3, 6, 7) | Draft in this document |
| G3 Layer detailed design | Schematics, mechanical CAD, firmware design, payload specification | Not started |
| G4 Lab qualification | Thermal-chamber accuracy, magnet pull/shear, IP67/68, EN 50125-3 vibration/shock, EN 50121-4 EMC, battery life | Not started |
| G5 Field trial | Pilot on a South African line: accuracy against a reference thermometer, retention, train-detection interaction, coverage, theft | **Not started; required before design freeze** |
| G6 Operator acceptance | Transnet/PRASA technical approval, signalling sign-off | Not started |

Each gate is passed only when every requirement owned at that level is either verified or explicitly deferred with a reason. Requirements tagged [F] can pass G4 as "lab-verified, field-pending", but cannot pass G5.

### 2.3 Build order
1. **Tier 1 core first.** It is the common platform, carries the least risk and is the cheapest to iterate. Most qualification work (mechanical, thermal, EMC, IP) is done once, on this core.
2. **Tier 2** adds solar/LTO power, the ambient standoff, dual radio and relay. These are add-on modules on the same core.
3. **Tier 3** adds the off-rail mast and satellite backhaul. This is largely a separate product (the gateway/mast) that talks to Tier 2 nodes by radio.

## 3. System layering

The system is described as seven stacked layers plus three cross-cutting concerns that apply to all of them.

```
 ┌──────────────────────────────────────────────────────────────┐   Cross-cutting
 │ L7  Application & operations  (CRT workflow, alerts, API)    │   ┌──────────────┐
 ├──────────────────────────────────────────────────────────────┤   │ X1 Electrical│
 │ L6  Data platform  (ingest, storage, forecasting, OTA)       │   │    safety &  │
 ├──────────────────────────────────────────────────────────────┤   │    EMC       │
 │ L5  Connectivity  (LoRaWAN / LTE-M / NTN / Iridium, relay)   │   ├──────────────┤
 ├──────────────────────────────────────────────────────────────┤   │ X2 Security &│
 │ L4  Device logic  (firmware: sampling, cadence, log, alarms) │   │    anti-theft│
 ├──────────────────────────────────────────────────────────────┤   ├──────────────┤
 │ L3  Electronics  (MCU, sensor front end, RTC, flash, IMU)    │   │ X3 Environ-  │
 ├──────────────────────────────────────────────────────────────┤   │    ment &    │
 │ L2  Energy  (primary cell, LTO, solar, pulse buffer)         │   │    reliab.   │
 ├──────────────────────────────────────────────────────────────┤   └──────────────┘
 │ L1  Physical  (housing, magnets, contact slug, ambient shield)│
 ├──────────────────────────────────────────────────────────────┤
 │ L0  Site & installation  (placement rules, rail, clearances) │
 └──────────────────────────────────────────────────────────────┘
```

Each layer below gives its responsibility, interfaces, the invariant parts shared by all tiers, the tier variants and how it will be verified.

### L0: Site and installation
- **Responsibility:** where and how a unit is placed so that the reading is representative and the installation is safe.
- **Rules (all tiers):** shaded side, mid-web, in the crib midway between sleepers [R]; field side by default [R]; at least 2 m from axle-counter heads, track-circuit tuning units, impedance bonds and insulated block joints [R/F]; never span an insulated joint [R]; removed before tamping [R].
- **Outputs to upper layers:** site record (GNSS, km mark, rail side, rail profile, CRT values, sleeper type, shading notes, photo).
- **Verification:** installation procedure review [A]; signalling engineer written acceptance per site [R]; **field check of representativeness against a reference thermometer [F]**.

### L1: Physical (mechanical and thermal)
- **Responsibility:** hold the unit on the rail, couple the sensor thermally to the steel, isolate it from housing solar gain, protect contents.
- **Common core:** glass-filled polymer shell (fully insulating, no exposed metal other than magnet faces and slug) [R]; spring-loaded brass/copper slug with thermal pad and PTFE or foam isolation collar [R]; pot magnets on the web face [R]; potted heavy parts and no PCB-mounted connectors [R]; IP67 minimum, IP68/IP69K target [R]; target mass 0.5–0.8 kg [E].
- **Magnet sizing:** total rated normal pull 60–120 kgf, i.e. at least 10× device weight in shear [E]; SH grade or higher (≥150 °C) for NdFeB, or ferrite for Tier 1 if the unit is light [R].
- **Tier variants:**
  - T1: 2–3 magnets; louvered "near-rail air" pocket; dull grey/rust finish; no external parts.
  - T2: 3–4 magnets plus polymer foot-hook retention; 15–25 cm isolated standoff with a mini multi-plate shield; white solar cap on a rust-coloured body.
  - T3 rail node: as T2. Mast: 1.5–2 m fan-aspirated shield, pyranometer, anemometer, anti-climb pole ≥3 m, locked battery vault.
- **Verification:** thermal model of slug versus housing [E]; pull and shear test on new, rusty, painted and curved web samples [E→lab]; IP test [lab]; EN 50125-3 vibration/shock [lab, values to confirm against a licensed copy]; **magnet creep under real traffic and tamping-train passage [F]**.

### L2: Energy
- **Responsibility:** supply the device for its rated life over -20 to +85 °C, including radio current pulses.
- **Common core:** power-path design that accepts a primary cell and an optional rechargeable store; pulse buffer (hybrid layer capacitor); fuel gauge or coulomb counter reported in every heartbeat.
- **Tier variants:**
  - T1: one Li-SOCl<sub>2</sub> D-cell (~17 Ah) plus HLC; no harvesting [R].
  - T2: 0.5–1 W solar cell, LTO pack (charges to -30 °C), Li-SOCl<sub>2</sub> backup so the node survives panel theft [R].
  - T3 mast: 20–40 W solar, LiFePO<sub>4</sub> or LTO with heater and charge inhibit, vaulted [R].
- **Energy budget targets [E]:** T1-LoRa ≥7 years; T1-LTE-M ≥3 years (hourly batch); T2 indefinite in normal sun, ≥6 months on the backup cell alone [A].
- **Verification:** budget spreadsheet from module datasheets [E]; current-profile measurement on the prototype [lab]; accelerated thermal ageing [lab]; **real coverage-dependent radio energy (deep-coverage repetitions) [F]**.

### L3: Electronics
- **Responsibility:** measure, timestamp, store and hand data to the radio.
- **Common core:** low-power MCU with RTC; sensors on short internal leads, digital or differential [R]; ground-plane PCB, TVS and filtering on all lines [R]; 3-axis accelerometer (tamper, detachment, vibration statistics); attachment sensor (Hall or reed against the rail) [R]; at least 60 days of non-volatile log at 10-minute resolution [R]; one PCB footprint that accepts different radio modules [R].
- **Sensor choice per tier:**
  - T1: TMP117 for rail and near-rail air (or a batch-calibrated 10 kΩ NTC as a cost-down option) [R].
  - T2: PT1000 Class A or TMP117 for mid-web; second contact sensor on the foot; SHT4x for ambient; GNSS [R].
  - T3: as T2, plus optional strain/SFT channel; mast carries reference-grade ambient, pyranometer and anemometer [R].
- **Accuracy target:** ±0.5 °C or better on the rail channel over the full range, including the effect of the mounting [R]; ±0.2 °C sensor-only [E].
- **Verification:** thermal-chamber calibration against a reference PRT [lab]; EMC pre-scan [lab]; **accuracy in the installed state on sunny, low-wind days against a reference contact thermometer [F]**.

### L4: Device logic (firmware)
- **Responsibility:** decide when to sample, when to send and what to send first.
- **Common core behaviour:**
  - Sample every 10 minutes all year [A, from the 10-minute requirement].
  - Adaptive reporting: hourly batches when the rail is below a site threshold (default 35 °C [A]); every sample sent immediately above it or when the rate of rise exceeds a set value [R, Yeltech-style].
  - Alarm-first queue: threshold crossings, tamper, detachment and low battery go out before any backlog [R].
  - Store-and-forward with back-fill when the link returns (T1 ≥30 days, T2/T3 ≥60 days) [R].
  - Activates on attachment; records a GNSS or cell location fix on attachment and on tamper [R].
  - Firmware lock: refuses to operate off its registered network or after an unauthorised opening [R].
  - Signed OTA updates (T2/T3; T1-LTE-M optional) [A].
  - Thresholds and cadence are configurable per site from the platform.
- **Verification:** unit tests and hardware-in-the-loop simulation of link loss, battery fade and temperature ramps [lab]; **behaviour under real intermittent coverage [F]**.

### L5: Connectivity
- **Responsibility:** move messages from node to platform within the latency the operator needs (alarms within minutes where any link exists).
- **Common core:** the payload format (Section 7) is the same over every bearer; no 2G/3G modules [R].
- **Tier variants:**
  - T1: SKU (a) LoRaWAN EU868, SF7–SF9, needs a gateway within a few km [R]; SKU (b) LTE-M/NB-IoT with PSM [R].
  - T2: LTE-M/NB-IoT with a Release 17 NTN-capable module, plus LoRa for node-to-node or node-to-gateway relay [R].
  - T3: multi-bearer mast gateway (LTE-M / Cat-1, Iridium SBD, NTN NB-IoT when available in SA) [R]. Radio-only link from rail nodes to the mast [R].
- **Verification:** link budget for rail-level antennas (shadowing by rail head, ballast and passing trains) [E]; duty-cycle and fair-use calculation [R/E]; **per-site LTE-M/NB-IoT coverage confirmation with Vodacom and MTN and real rail-level LoRa range [F]**; satellite availability and price in SA [R, to recheck before T3 design].

### L6: Data platform
- **Responsibility:** receive, de-duplicate, store and enrich data; manage devices.
- **Functions:** ingest from the LoRaWAN network server and MQTT; de-duplicate back-filled records by device sequence number; device registry and site records (from L0); overlay of NWP or SA Weather Service ambient data (all tiers) [R]; rail-minus-ambient analytics (T2+); physics/ML rail-temperature forecast 6–24 h ahead (T3) [R]; OTA and configuration management; audit log.
- **Verification:** load and replay tests with synthetic data [lab]; forecast model validation **needs site data [F]**.

### L7: Application and operations
- **Responsibility:** turn data into operator decisions.
- **Functions:** per-site CRT register with three configurable levels mapped to local practice (CRT(W)-, CRT(30/60)- and CRT(20)-style) [R]; email/SMS/push alerts with acknowledgement; map of sites and device health; tamper and theft workflow; API for Transnet/PRASA maintenance and train-control systems [A: interface to be defined with the operator]; audit trail of every alert and acknowledgement.
- **Verification:** workflow walk-through with operator staff [A]; **live trial of alerting through a hot season [F]**.

### X1: Electrical safety and EMC (cross-cutting)
- The rail-mounted unit is fully insulated and floats at rail potential; there is no conductive path to earth, to a mast or to the other rail [R].
- It must withstand rail-to-earth potentials of tens of volts continuously and hundreds of volts during faults without becoming a hazard; housing insulation is specified with margin over the EN 50122-1 touch-voltage values (to confirm against the 2022 edition plus A1:2025) [R].
- EMC to EN 50121-4 (immunity) and EN 50121-2 (emissions); EN 50155 used only as a derating reference [R].
- Masts are earthed per Transnet practice; a mast on an OHTE structure is treated as bonded to rail on DC sections and needs Transnet approval [R].
- **Verification:** insulation and dielectric test [lab]; EMC test [lab]; **interaction with axle counters and track circuits on a live installation, with signalling sign-off [F]**.

### X2: Security and anti-theft (cross-cutting)
- Physical: look worthless (small, dull, unbranded, no visible panel/antenna/cable, "no copper / no resale value / tracked" marking); no copper, brass or aluminium on the outside; security Torx; concealment under the rail-head overhang [R].
- Detection: accelerometer plus attachment sensing gives an immediate "moved/detached" alarm with last location; geofence (T2/T3) [R].
- Deterrence: firmware lock so a stolen unit is a brick [R].
- Cyber: device identity keys, encrypted and authenticated payloads, signed firmware, role-based platform access [A].
- **Verification:** design review [A]; **theft and vandalism rates in a pilot deployment [F]**.

### X3: Environment and reliability (cross-cutting)
- Rail channel range -30 to +90 °C; electronics rated -20 to +85 °C; ambient channel -20 to +70 °C [R].
- On-rail vibration and shock per EN 50125-3 (values to confirm) [R].
- UV-stable materials; resistance to ballast dust, oil, flooding and jet washing [R].
- Target service life: 10 years for housing and magnets [A]; replaceable battery for T1/T2 [A].
- **Verification:** HALT/thermal cycling and UV exposure [lab]; **long-term durability [F]**.

## 4. Safety invariants (apply to every tier, cannot be waived)

| ID | Invariant | Tag |
|---|---|---|
| INV-1 | No galvanic path from a rail-mounted unit to earth, a mast, another rail or any off-rail equipment. Radio links only. | R |
| INV-2 | Fully insulating housing; only the magnet faces and contact slug touch the rail. | R |
| INV-3 | Never installed across an insulated block joint, and kept clear of train-detection equipment by the agreed distance. | R / F |
| INV-4 | The unit must not shunt, bias or otherwise interfere with track circuits or axle counters. | R / F |
| INV-5 | A unit that detaches must not foul the structure gauge (falls clear or is retained). | A / F |
| INV-6 | Device data supports, but never replaces, the operator's own decision process: loss of data must raise an alert, not silence one. | A |

## 5. Key requirements (G1 draft)

| ID | Requirement | Layer | Tag | Verify by |
|---|---|---|---|---|
| REQ-S-01 | Rail-channel measurement range -30 to +90 °C | L3 | R | Lab |
| REQ-S-02 | Rail-channel accuracy ±0.5 °C installed (target ±0.3 °C) | L1/L3 | R / F | Lab then field |
| REQ-S-03 | Sample interval 10 min; reported per adaptive cadence | L4 | A | Test |
| REQ-S-04 | Ambient channel -20 to +70 °C; T1 labelled "near-rail air"; T3 WMO-style | L1/L3 | R | Lab then field |
| REQ-M-01 | Retention ≥10× device weight in shear on rusty web | L1 | E | Lab then field |
| REQ-M-02 | Survives EN 50125-3 on-rail vibration and shock | L1/X3 | R | Lab |
| REQ-M-03 | IP67 minimum, IP68/IP69K target | L1 | R | Lab |
| REQ-P-01 | T1-LoRa life ≥7 yr; T1-LTE-M ≥3 yr; T2 ≥6 months on backup cell | L2 | E / A | Analysis, lab, field |
| REQ-C-01 | Alarm delivered within 5 min of detection where a link exists | L4/L5 | A | Test, field |
| REQ-C-02 | No data loss over a link outage of ≥30 days (T1) / ≥60 days (T2/T3) | L4 | R | Test |
| REQ-C-03 | No 2G/3G radio modules | L5 | R | Design review |
| REQ-E-01 | Compliance with EN 50121-4 / EN 50121-2 | X1 | R | Lab |
| REQ-T-01 | Detachment or movement alarm with last location | L4/X2 | R | Test, field |
| REQ-A-01 | Three-level CRT thresholds per site with acknowledged alerts | L7 | R | Test |

## 6. Tier mapping summary

| Layer | Tier 1 "Cheap" | Tier 2 "Mid-range" | Tier 3 "Advanced" |
|---|---|---|---|
| L1 Physical | Puck, 2–3 magnets, louvered pocket | + foot-hook, ambient standoff shield, solar cap | Tier 2 nodes + weather mast |
| L2 Energy | Li-SOCl<sub>2</sub> + HLC | Solar + LTO + Li-SOCl<sub>2</sub> backup | Mast: 20–40 W solar, vaulted storage |
| L3 Electronics | TMP117 ×2, IMU, attach sensor | + PT1000/foot sensor, SHT4x, GNSS | + strain/SFT option; mast met sensors |
| L4 Device logic | Adaptive cadence, 30-day log, alarm-first | + relay, 60-day log, OTA, geofence | + gateway aggregation and batching |
| L5 Connectivity | LoRaWAN **or** LTE-M | LTE-M (NTN-ready) **and** LoRa | Mast: LTE / Iridium / NTN; nodes by radio |
| L6 Platform | Ingest, NWP overlay | + rail-minus-ambient analytics | + forecasting |
| L7 Application | CRT thresholds, alerts | + operator API | + forecast-driven CRT workflow, multi-site |
| Indicative BOM [E] | US$35–75 | US$120–200 | US$400–900 mast + US$150–250/node |

## 7. Key interfaces

- **IF-1 Rail ↔ L1:** thermal (slug and pad on bare web), magnetic (pot faces), mechanical (foot hook). Defined by rail profile; Cape-gauge profiles to be listed [A].
- **IF-2 L1 ↔ L3:** sensor on the slug connected by a short internal lead; digital (I²C) or 4-wire RTD.
- **IF-3 L2 ↔ L3:** single regulated rail plus pulse buffer; battery telemetry over I²C.
- **IF-4 L4 ↔ L5:** bearer-agnostic message queue with priority (alarm > status > backlog).
- **IF-5 L5 ↔ L6:** compact binary payload (draft below); LoRaWAN via network server, cellular via MQTT/TLS or CoAP/DTLS, satellite via gateway batching.
- **IF-6 L6 ↔ L7 / operator systems:** REST/JSON API and webhooks [A].

**Draft payload (per record, about 10–12 bytes):** device sequence number; timestamp (offset from batch header); rail temperature (int16, 0.01 °C); ambient temperature (int16, 0.01 °C); status flags (attached, tamper, low battery, threshold level); battery percentage; optional foot temperature and humidity (T2+). Batches carry one header with device ID, base time and firmware version.

## 8. Verification plan (current position)

| Activity | Can be done now (desk / bench) | Lab (after prototype) | Field (**not yet done**) |
|---|---|---|---|
| Thermal coupling and isolation | Analytical / FEA model | Hot-plate + solar simulator rig | Side-by-side with a reference thermometer on track |
| Accuracy | Error budget | Thermal chamber vs reference PRT | Seasonal comparison at pilot sites |
| Retention | Magnet pull calculations | Pull/shear on rail offcuts; EN 50125-3 shake table | Creep under traffic; tamping |
| Power | Energy budget | Current profiling; thermal ageing | Coverage-dependent consumption |
| Connectivity | Link and airtime budgets | Range test on rail offcuts | Per-site coverage; rail-level LoRa range |
| EMC / safety | Design rules review | EN 50121-4 / -2; dielectric test | Axle-counter and track-circuit interaction |
| Theft | Design review | n/a | Pilot loss rate |
| Operations | Workflow mock-up | Platform replay tests | Hot-season alert trial with operator |

## 9. Top risks (desk view)

| Risk | Layer | Likelihood | Impact | Mitigation now | Retire by |
|---|---|---|---|---|---|
| No coverage at the sites that matter | L5 | High | High | Gateway-centric LoRa option; NTN-ready modules; Tier 3 mast | Per-site coverage survey [F] |
| Theft or vandalism of units | X2 | High | High | Look-worthless design; no panel at T1; firmware lock | Pilot [F] |
| Magnet creep or detachment | L1 | Medium | High | 10× shear margin; foot hook at T2+; detachment alarm | Lab + field |
| Interference with train detection | X1 | Low–Medium | Very high | Field-side, ≥2 m clearance, signalling sign-off | Field [F] |
| Housing solar gain biases rail reading | L1/L3 | Medium | Medium | Isolated slug, high-albedo shade cap | Lab + field |
| Standards values unconfirmed (EN 50125-3, EN 50122-1, Transnet specs) | X1/X3 | Medium | Medium | Flag as [R]-to-confirm; buy licensed copies | Before G3 |
| Battery life short in deep-coverage cellular areas | L2 | Medium | Medium | Hourly batching; LoRa SKU | Field [F] |

## 10. Open decisions

1. Which tier to prototype first; recommendation is the Tier 1 core with both radio SKUs on one PCB.
2. Default adaptive-cadence threshold (35 °C proposed) and whether operators need 10-minute reports all year.
3. Whether Transnet/PRASA have a preferred LoRaWAN network or cellular operator.
4. CRT value sets and how they map to Transnet speed-restriction practice (the UK cascade is used here only as a template).
5. Obtain licensed copies of EN 50125-3, EN 50122-1:2022+A1:2025, EN 50121-2/-4 and Transnet BBC1040, CEE 0059.84 and CEE.0228.96.
6. Field-trial partner, line section and reference instrumentation (to plan G5).
