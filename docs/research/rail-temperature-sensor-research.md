
# Rail Temperature Sensor: Design Research for Cheap, Mid-Range and Advanced Variants (Transnet/PRASA context)

All three tiers should use the same core design: a sealed, electrically floating unit fixed by magnet to the shaded side of the rail web, with no wire running from the rail to anything off it. The tiers should differ mainly in how they connect, how they are powered and how much of the "true ambient" problem they solve. A LoRaWAN or LTE-M primary-battery puck covers the cheap tier. A dual-mode cellular unit with store-and-forward and solar assist covers the mid tier. The advanced tier pairs rail nodes with an off-rail mast that carries a radiation-shielded weather station and satellite backhaul. On a mostly no-coverage network like Transnet's, connectivity and theft (not sensing accuracy) are what decide whether the product works.

## TL;DR
- **Benchmarks:** commercial units (Yeltech RTMU, RailSense TempSense, Dual Inventive CRM 3000, DYWIDAG SmartTemp) are all magnet- or clip-mounted, battery-powered, cellular, and claim roughly 0.5–0.75 °C accuracy and up to about 3 years of life. Most report every 15–60 minutes, not every 10. Retail is in the low thousands of pounds per unit (the Yeltech RTMU lists at £1,628 ex VAT), which leaves room for a cheaper LoRaWAN tier and a premium tier with off-grid satellite backhaul.
- **Physics:** rail temperature runs typically about 20 °C above air, and up to about 35 °F (about 19 °C) above the daily maximum in FRA data. A -10 to 60 °C ambient spec therefore implies a rail-sensing range of at least -20 to +85 °C, so a -30 to +90 °C design target is a safe margin. The rail contact sensor belongs at mid-web on the shaded side, between sleepers. A true WMO-style ambient reading cannot be taken on a rail-mounted puck, only "near-rail air"; the advanced tier needs an off-rail shielded sensor.
- **Constraints that drive the design:** under 3 kV DC and 25 kV AC the rail can sit at tens of volts in normal running (EN 50122-1 allows 60 V AC and 120 V DC long-term touch voltage) and hundreds of volts during faults. Transnet practice is that low-voltage equipment is not earthed or bonded to the return rail, so the device must be fully insulated and self-contained. Theft is endemic: at Transnet's 2025/26 results, Group CEO Michelle Phillips reported "approximately 5 870 theft and vandalism incidents" and losses of "almost R2 billion", despite a 7% fall in incidents. Batteries and solar panels are known targets, which argues for low-value, concealed, panel-free designs at the cheap and mid tiers.

## Key Findings

### 1. Why rail temperature matters (and how operators use the data)
- **The buckling mechanism.** Continuous welded rail (CWR) is fixed at a stress-free (neutral) temperature (SFT/RNT). Above it the rail goes into compression; once that compression exceeds the track's lateral resistance, the track buckles. Below it the rail is in tension and can break. In the UK, CWR is stressed to 27 °C, the mean summer rail temperature. US SFTs typically range from 35 to 43 °C. South African practice, as reported in Engineering News, is to do closure welds at the "neutral rail temperature", which is midway between the rail temperature extremes for that location. US guidance notes that a rail temperature rise of 40–50 °F above RNT can produce a potential buckle. RNT also falls over time and after maintenance, which is why monitoring matters even on well-stressed track.
- **How operators turn temperature into speed restrictions.** Network Rail uses a three-level Critical Rail Temperature (CRT) cascade, defined in its standard NR/L3/TRK/3012:
    - CRT(W): a watchman is posted.
    - CRT(30/60): an emergency speed restriction of 60 mph for passenger trains and 30 mph for other trains.
    - CRT(20): 20 mph for all traffic.

    Watchmen take readings at half-hourly intervals. A well-maintained section has a CRT(W) of typically 59 °C. Poor track can sit near the SFT itself, and CRT is reduced after track renewals or nearby works. Remote sensors replace the watchman and allow targeted rather than blanket restrictions.
- **Value case.** In its June 2025 article "Smart sensors, fewer delays", Network Rail reports that buckled rails caused "more than 350,000 minutes – or 240 days of delays" in the previous year, which was itself a 14% reduction on the year before. Traditionally it measured rail temperature with magnetic thermometers placed by hand. Since 2023 it has deployed remote sensors that read every 15 minutes, send data via mobile networks, and issue email alerts at thresholds. The aim is to avoid blanket speed restrictions.
- **Prediction alongside measurement.** The traditional US approach adds a constant offset, most often 30 °F, to peak ambient. Peer-reviewed work shows this is inaccurate on very hot days. Physics models (energy balance: solar absorption, convection, radiation) and machine-learning models (CNU, Korea) reach an RMSE of about 0.5 °C. ENSCO's commercial PRTS forecasts rail temperature in 30-minute increments on 5.6 × 5.6 mile grids, updated every six hours. Speed-limit criteria shift with a 4–5 °C change in rail temperature, so sensor accuracy of about ±0.5 °C or better is the right target.

### 2. What range must the sensor cover?
- Network Rail and Yeltech both state that rail can run more than 20 °C above air. In an FRA study, rail reached 34.7 °F above the maximum ambient. A Korean study puts typical rail temperatures at -10 to 60 °C in a temperate climate.
- With a 60 °C ambient ceiling and full sun, rail at about 75–85 °C is plausible. At night, rails radiate to the sky and can drop slightly below air temperature. Benchmarks cover:
    - TempSense: -20 to +80 °C measurement range.
    - TrenTrace: -55 to +125 °C.
    - Yeltech RTMU electronics: -20 to +70 °C operating.
- **Design recommendation:** rail channel -30 to +90 °C. Electronics rated -20 to +85 °C, because the puck itself sits on hot steel. Ambient channel -20 to +70 °C.
- **Spatial gradients are real.** Internal and surface rail temperatures can differ by up to 7 °C in summer. Sleeper zones and the rail foot run cooler because heat transfers into the sleeper and ballast. Shade also matters: a Japanese GIS study found that 16% of sections had maximum rail temperatures 2 °C or more below the uniform assumption. Placement therefore matters as much as sensor grade.

### 3. Benchmarks: existing commercial products

| Product | Sensing / mounting | Power | Connectivity | Reporting | Accuracy / range | Price signal |
|---|---|---|---|---|---|---|
| Yeltech RTMU Mk3 (UK; Network Rail) | Built-in magnet; activates on attachment | 13 Ah 3.6 V lithium, up to 3 yrs | GSM, SMS config, YelCloud, Network Rail II compatible | Batches every 12 h below 25 °C; every 30/60 min above 25 °C | Operating -20 to +70 °C | £1,628 ex VAT (Sunbelt Sales) |
| RailSense TempSense (Network Rail PA05/07797) | Rail-mounted, 0.6 kg, IP67 | Battery + solar charging, "rechargeable", up to 3 yrs | 2G/NB-IoT/LTE-M | Scheduled + threshold alerts | ±0.75 °C; -20 to +80 °C | n/a |
| Dual Inventive CRM 3000 (UK/EU; Translink) | Strong magnet on rail web | Internal battery, "left in track for years" | NB-IoT to cloud | Custom thresholds/alarms | n/a | n/a |
| Dual Inventive CRTM 3000 (earlier) | Magnetic nodes, two sensors, detects rail attachment | Node battery up to 5 yrs | Nodes to gateway ~70 m; gateway 2G | Push/SMS alerts | n/a | n/a |
| DYWIDAG SmartTemp (Network Rail WCML) | Installed around rail web; activates on rail contact and sends GPS | Battery | Cellular; Azure; RADAR 2 integration | Every 15 min | n/a | n/a |
| TrenTrace | PVC clip under rail foot, C360 brass contact | Battery up to 3 yrs | ZigBee ~30 m to base station | n/a | Digital sensor ±0.5 °C; -55 to +125 °C | n/a |
| ESIM TR-GSM (Italy, RFI) | Box bolted through web at neutral axis, 3 sensors | n/a | GSM | Threshold signalling | n/a | n/a |
| Pandrol Vortok MultiSensor / Rail Stress Monitor | Clamp-on; core temperature at centre of web + strain/SFT + 2-axis accelerometer | Logger | SMS/email/FTP | Continuous | SFT tracking | Premium |
| Senceive PT100 node | PT100 RTD, optional tilt | Long-life battery | Proprietary mesh | n/a | n/a | n/a |

Notes on these benchmarks:

- I could not find a current Salient Systems rail-temperature product. Salient's IntelliTrack wayside data-collection line was acquired by Portec Rail and is aimed at track and rolling-stock health. Treat Salient as a wayside-data reference, not a direct competitor.
- Yeltech's adaptive reporting is the key point. Even a 13 Ah cell gets only about 3 years on GSM because the unit transmits sparsely in cool weather. A strict 10-minute cellular cadence all year would cut that sharply.
- No benchmark publishes a radiation-shielded ambient channel on the rail unit. RailSense says it combines temperature, weather and solar data in analytics, which suggests ambient is typically sourced separately.

### 4. Sensing: placement, thermal isolation, sensor choice
- **Rail placement.** Use mid-web, on the shaded (field or gauge) side, in the crib midway between sleepers:
    - FRA's CWR guidance says to measure rail temperature "at the center of the web on the shady side of the rail".
    - Vortok measures "true rail temperature in the centre of the rail web".
    - Dual Inventive and DYWIDAG also mount on the web.
    - The head is unsuitable because of wheel and flange contact and grinding.
    - The foot is easy and concealed (TrenTrace) but reads cooler near sleepers and ballast, and it is exposed to tamping. TrenTrace says the sensor "needs to be removed" during tamping.

    The Korean ARMP study shows that the best representative point shifts with season and rail orientation, with a mean absolute error of about 0.2–0.3 °C at the optimum point. For a CRT product, consistency with operators' manual method (shaded web) matters more than theoretical optimality.
- **Keeping the housing's solar gain out of the rail reading.** Put the sensing element in a small copper or brass slug pressed against bare web steel by a spring and a thermal pad. TrenTrace uses C360 brass, which is softer than rail steel. Insulate the slug from the housing with PTFE or a closed-cell foam collar. Give the housing a white or light-grey, high-albedo UV-stable shell that shades the contact patch. The magnets are excellent thermal paths to the rail, so the slug can sit between them. Expect a small lag (minutes), which is fine at a 10-minute cadence.
- **Ambient.** A sensor centimetres from a 75 °C rail and above sun-heated ballast measures a near-rail microclimate, not WMO ambient (normally taken at about 1.25–2 m in a ventilated screen). Unaspirated plate shields can still read 1–3 °C warm in strong sun and low wind; aspirated shields cut this to a few tenths of a degree. Options by tier:
    - **Cheap:** a small louvered pocket on the shaded side of the puck, labelled "near-rail air", plus a backend overlay of weather data from a numerical weather prediction (NWP) model or a nearby station.
    - **Mid:** a short, thermally isolated standoff arm carrying a mini multi-plate shield (white ASA) with a low-mass sensor such as an SHT4x, held away from the rail and ballast.
    - **Advanced:** a separate off-rail mast at 1.5–2 m with a proper (ideally fan-aspirated) shield and a pyranometer. This feeds both ambient and solar irradiance into the prediction model.
- **Sensor technologies (typical datasheet values; not independently verified here):**
    - *NTC thermistor 10 kΩ:* ±0.2–1 °C interchangeability; cheapest (pennies); robust; needs a calibration table. Fine for the cheap tier if calibrated per batch.
    - *Digital IC, TMP117:* about ±0.1 °C at -20 to 50 °C and about ±0.2 °C at -40 to 100 °C; I²C; ultra-low power; about US$2–4. The best accuracy per dollar for both channels.
    - *DS18B20:* ±0.5 °C at -10 to +85 °C; 1-Wire; cheap, but slower and higher current. Note that TrenTrace's ±0.5 °C matches this class.
    - *SHT4x:* temperature about ±0.1–0.2 °C plus humidity. Ideal for the ambient channel inside a shield.
    - *PT1000 Class A RTD:* ±(0.15 + 0.002·|t|) °C; very stable and industry-familiar (Senceive and OleumTech use PT100). Needs a precision ADC; choose it for the premium or "metrology" tier.
    - *Thermocouples:* ±1–2.2 °C class tolerance, cold-junction errors and µV signals in a high-EMI environment. Not recommended.
    - *Infrared (non-contact):* emissivity of rusty versus polished or painted rail varies widely, and network operators paint rails white. Errors of several °C are likely. Suitable only as a supplementary head-surface reading on an advanced mast, not for CRT decisions.

### 5. Electrical safety and EMC under 25 kV AC and 3 kV DC
- **Rails carry traction return current and float.** EN 50122-1 limits the effective touch voltage, i.e. the rail-to-earth potential a person can touch. Figures from the 2011 edition's Annex D, also matched by a National Grid/WPD utility document:
    - AC: 60 V for durations over 300 s, 65 V at 300 s, 785 V at 0.1 s.
    - DC: 120 V for durations over 300 s, 150 V at 300 s, 625 V at 0.1 s.

    The 2022 edition renumbers these tables (Tables 8 and 10), and Amendment A1:2025 modifies clauses 9.2.2.2 and 9.3.2.2, so check the current values against a licensed copy. **Design implication:** the device will routinely sit at tens of volts relative to earth, and at several hundred volts during faults, while remaining at the same potential as the rail it is clamped to.
- **Transnet practice: do not bond to the rail, do not earth.** Transnet signalling specification BBC1040 states that "equipment operating at less than 150 V shall not be deliberately earthed nor shall they be bonded to the return rail". It references CEE 0059.84 ("Earthing and Bonding 3kV DC Electrification") and requires signal rooms within 3 m of a DC track to bond to the track only via a surge arrester. I read these only as tender-document excerpts; obtain the full specifications from Transnet. Rules for the product:
    1. No conductive path from the rail-mounted unit to earth, to a mast, or to the other rail. An external cable to a pole would bridge rail potential to earth, creating a touch-voltage hazard and a DC stray-current path. A cable across both rails could shunt a track circuit.
    2. Use a fully insulating housing with no exposed metal except the magnet face and contact slug, which touch the rail anyway.
    3. Link rail nodes to any off-rail mast or gateway by radio only.
    4. Keep the unit clear of insulated rail joints, impedance bonds and traction bonds. Never span an insulated block joint.
- **Magnetic fields and EMC.** Return currents of hundreds to thousands of amps produce strong 50 Hz (AC) and ripple (DC) fields at the web. Requirements:
    - Short internal sensor leads, differential or digital sensors, and a ground-plane PCB.
    - Shielded ADC references and transient-voltage-suppressor (TVS) protection.
    - Test to the railway EMC family EN 50121-4 (signalling and telecom apparatus; IEC 62236-4 equivalent) and EN 50121-2 emissions limits.
    - EN 50155 applies to rolling-stock electronics and is a useful reference for component derating, but it is not the governing standard for fixed trackside equipment.
- **Vibration and shock.** For equipment fixed to the rail, EN 50125-3 (fixed signalling and telecom environment) is the governing standard, not EN 61373, which covers rolling stock (Category 3 is axle-mounted). Search snippets of EN 50125-3 Table 5 suggest on-rail random vibration of about 280/140/50 m/s² RMS across three axes, against about 10 m/s² on ballast. It also specifies a half-sine 11 ms shock test. Those values could not be checked against a clean copy, so confirm them, but they show the rail is among the harshest mounting locations in railway engineering. Pot magnets, potting of heavy parts (cells, antenna), and the absence of PCB-mounted connectors are essential.
- **Ingress protection.** Specify IP67 at minimum and IP68 plus IP69K (jet wash) where possible, because of ballast cleaning, flooding and pressure washing. Benchmarks are IP67 (TempSense, TrenTrace).

### 6. Magnetic mounting
- **Precedent.** Magnetic attachment is the norm for this product class: Yeltech's "powerful built-in magnet", Dual Inventive's "strong magnetic mount… to the web", and Network Rail's traditional magnetic thermometers. Vortok also uses rare-earth magnets for temporary AWS (Automatic Warning System) equipment.
- **Engineering guidance (my analysis; validate by test):**
    - Use 2–4 rubber-boot or steel-pot NdFeB magnets on the web face. Pot construction concentrates flux and protects brittle NdFeB from shock.
    - Rated pull falls sharply with air gap (paint, rust scale, web curvature), and shear (sliding) resistance is only about 15–25% of normal pull.
    - Size for at least 10× the device weight in shear under EN 50125-3 shock. For a 0.5–0.8 kg unit, that means a total rated normal pull of roughly 60–120 kgf.
- **Magnet grade and temperature.** Standard N-grade NdFeB starts losing magnetism irreversibly at about 80 °C. The web can approach that, and the magnet faces it directly. Specify an SH-grade or higher (≥150 °C). Ferrite is cheaper, stable to over 200 °C and of no scrap value, but needs about 3–4× the volume for the same pull. It is reasonable for the cheap tier if the unit is light.
- **Secondary retention.** Add a polymer hook over the rail foot or a lanyard to a sleeper fastening on mid and advanced tiers. Magnets will "walk" under repeated shock, and tamping or ballast regulators can strip them; operators already require removal for tamping. An accelerometer should flag any shift.
- **Interference with train detection.** Frauscher-type axle-counter wheel sensors clamp to the inner (gauge) face of the rail below flange height. They work by inductive flux coupling and are known to be disturbed by eddy-current and magnetic track brakes. A static permanent magnet and a mass of steel near the sensor head could plausibly bias its flux path. Rules:
    - Mount on the field side.
    - Keep at least about 2 m from axle-counter heads and other trackside sensors (the commonly cited minimum separation between detection systems), and away from track-circuit tuning units and impedance bonds.
    - Get written acceptance from the signalling engineer.
    - In the UK, also keep clear of AWS and TPWS track magnets.

### 7. Power: batteries and harvesting
- **Lithium thionyl chloride (Li-SOCl<sub>2</sub>, e.g. bobbin D-cell about 17–19 Ah).** Wide temperature range (about -55 to +85 °C) and about 1%/yr self-discharge. It cannot deliver the 1–2 A pulses of a cellular or Iridium radio on its own, so pair it with a hybrid layer capacitor or supercapacitor. Yeltech's 13 Ah 3.6 V cell is this class. This is the default for the cheap tier, which has no harvesting and therefore nothing worth stealing.
- **LiFePO<sub>4</sub>.** Robust and safe, but must not be charged below 0 °C. That is acceptable for a -10 °C minimum with firmware charge inhibit, but its cycle life degrades at 60–80 °C housing temperatures.
- **LTO (lithium titanate).** Charges down to about -30 °C, tolerates heat and lasts many cycles. The best rechargeable choice for solar-assisted mid and advanced units, at a higher cost per Wh.
- **Supercapacitor-only with solar.** Feasible for LoRa-class power budgets; one hobby design runs a 0.25 °C sensor on a 35 × 14 mm cell and a supercapacitor. However, supercapacitor life roughly halves for every 10 °C rise, so a puck on 70 °C rail steel is a poor environment. Use one only as a pulse buffer.
- **Harvesting:**
    - *Solar:* by far the most productive source, but panels are theft targets and invite tampering.
    - *Thermoelectric (TEG) between rail and air:* the gradient exists by day, so it yields milliwatts at best. Possible but marginal.
    - *Vibration:* energy is available only during train passage, which is infrequent on many freight lines. Not a primary source.
- **Rough energy budgets at a 10-minute cadence (144 messages/day; my calculations from typical module figures):**
    - *LoRaWAN SF7–SF9:* about 0.01–0.05 mAh per uplink, so about 2–7 mAh/day. A 17 Ah cell lasts more than 10 years in principle; in practice life is self-discharge and derating limited, about 7–10 years.
    - *LTE-M/NB-IoT with PSM, good coverage:* about 0.1–0.3 mAh per report, so about 15–45 mAh/day, about 1–2.5 years on 17 Ah. Deep-coverage repetitions can multiply this 3–10×. Batching six readings into one hourly uplink (alarms immediate) restores about 3–5 years, which matches the Yeltech/TempSense "up to 3 years" pattern.
    - *Iridium SBD:* the 9603 averages 158 mA at 5 V (≤0.8 W) during message transfer, with 1.3 A peaks; RockBLOCK quotes 45–50 mA averaged over 60 s. At one session per hour, roughly 0.1–0.2 Wh/day; at a true 10-minute cadence, about 0.6–1 Wh/day. That requires solar plus a rechargeable store, which is why satellite belongs on a mast, not on a rail puck.

### 8. Connectivity options (status as of October 2026)
- **South African cellular.**
    - On 2 July 2025, the Department of Communications and Digital Technologies told Parliament it would no longer dictate the timing of the 2G/3G switch-off, reversing former minister Mondli Gungubele's 31 December 2027 deadline and leaving the decision to the mobile operators.
    - Activation of 2G/3G-only devices has been prohibited since 31 December 2024.
    - MTN has since told MyBroadband that 3G is "expected to be retired ahead of 2G over time" and that "there is no fixed national switch-off date".
    - **Design rule:** do not ship 2G/3G modules. Use LTE-M/NB-IoT (Cat-M1/NB2) modules, ideally ones that also support 3GPP Release 17 NTN, such as the Nordic nRF9151 certified by Myriota.
    - ICASA's State of the ICT Sector Report (March 2026) puts 2025 population coverage at 99.5% for 4G/LTE, but that is a population figure, not track kilometres. Rural freight corridors will have large gaps. Confirm LTE-M/NB-IoT availability per site with Vodacom and MTN.
- **LoRa/LoRaWAN.**
    - South Africa uses the EU863-870 plan. The LoRa Alliance regional parameters list South Africa at 868.7–869.2 MHz, and ICASA's Annexure B permits non-specific short-range devices (SRDs) at 25 mW ERP with duty cycle under 1% (868–868.6 MHz) and higher-power sub-bands.
    - At a 10-byte payload, about 0.05 s airtime at SF7 gives 144 uplinks/day of about 7 s (well within 1%). At SF12 (about 1.5 s per uplink) it gives about 216 s/day: still legal at about 0.25%, but far beyond community fair-use limits. ONISA, for example, allows 30 s airtime/day, which it equates to about 20 messages/day at SF12 or 500 at SF7.
    - **Implication:** a 10-minute cadence needs a private gateway within a few km so that nodes run SF7–SF9. That makes LoRaWAN a gateway-centric architecture: one solar/LTE or satellite gateway per yard, cutting or hot-spot cluster.
- **Mesh and short-range.** Dual Inventive's earlier node-to-gateway range was about 70 m; TrenTrace ZigBee about 30 m. Rail-level radio is badly shadowed by the rail head, ballast and passing trains, so expect short ranges. Raise antennas on a whip or mount the gateway on a pole.
- **Satellite IoT options:**
    - **Swarm:** shut down in March 2025. Do not design for it.
    - **Iridium SBD:** truly global and proven. Pricing (Ground Control examples): US$15.75/month for 5,000 bytes, US$36.75 for 25,000 bytes, US$147 for 250,000 bytes; minimum billable message 10 bytes; overage US$1.68–3.15 per 1,000 bytes. Ten-minute readings for a whole cluster batched hourly (about 50 KB/month) cost about US$80–150/month per gateway. That is viable per site, not per sensor.
    - **Myriota HyperPulse** (Release 17 NB-IoT NTN over leased Viasat L-band GEO): generally available since December 2025, with hybrid cellular added July 2026. Listed coverage is the US, Mexico, Argentina, Brazil, Australia, New Zealand and Saudi Arabia, with Myriota claiming under US$1/month connectivity (a marketing claim). **South Africa is not listed**, and older Myriota pricing quoted by Blues was US$500/month for 1 MB, so verify availability and price.
    - **Sateliot:** 11 LEO NB-IoT NTN satellites, full constellation targeted for 2027/28. It operates as a store-and-forward roaming extension to mobile network operators (MNOs), with latency of hours, not minutes.
    - **Starlink Direct-to-Cell ("Starlink Mobile"):** commercial messaging in the US and New Zealand, the UK via O2 since February 2026, and LTE Cat-1 IoT data tested. It reaches IoT only via partner operators. I found no South African partner; regulatory availability in SA is unconfirmed.
    - **Astrocast and Lacuna:** current status not verified in this research. Treat them as unconfirmed.
    - **Industry view:** Ground Control's 2026 survey finds standards-based NTN NB-IoT "best for price sensitive, scale driven use cases, not for critical real time monitoring".
- **Store-and-forward.** Every tier should log locally (at least 30 days at 10-minute resolution, about 4,300 records) and back-fill when the link returns. Alarm events (threshold crossing, tamper, detachment) should be sent first.

### 9. Theft and vandalism
- **Scale of the problem.** Transnet Group CEO Michelle Phillips reported "approximately 5 870 theft and vandalism incidents" in 2025/26, with losses of "almost R2 billion". CFO Nosipho Maphumulo put security spend at R3.7 bn. In a joint July 2021 statement, the CEOs of Telkom, Eskom, PRASA and Transnet put direct losses at "R7 billion a year", with a knock-on economic cost estimated at R187 bn a year. Gauteng shut 1,079 non-compliant scrapyards. MTN SA told MyBroadband that "close to 500 incidents of theft and vandalism" in 2024 "cost R450 million to replace stolen batteries and damaged base stations", up from R33 m in 2023. Batteries and anything resembling copper, solar panels or electronics are specifically targeted.
- **Design responses:**
    1. *Look worthless.* A small, dull grey, unbranded puck with no visible panel, antenna or cable, and an embossed "no copper / no resale value / tracked" marking in local languages.
    2. *Material choice.* Glass-filled polymer shell; avoid copper, brass and aluminium on the exterior. Use ferrite magnets where feasible: NdFeB has little scrap value, but strong magnets attract curiosity.
    3. *Concealment.* Field-side web placement under the rail head overhang, coloured to match rusty steel, which conflicts slightly with a white shell. One option is a white top cap over the contact area with a rust-coloured body.
    4. *Tamper detection.* A 3-axis accelerometer and magnetic attachment sensing (Dual Inventive and DYWIDAG both detect attachment) send an immediate "detached/moved" alarm with a last GNSS fix. Cellular cell-ID or LoRa gateway RSSI gives coarse location after theft.
    5. *Secure fasteners.* Use security Torx on the battery compartment, and a firmware lock that bricks the unit off-network, so there is no resale value.
    6. *Masts.* For advanced-tier masts, use anti-climb poles at least 3 m tall, small panels flush-mounted in steel frames, batteries in buried or locked steel vaults, and siting within existing secured perimeters (substations, relay rooms) wherever possible.

## Recommendations: the three product concepts
BOM and price figures below are my engineering estimates at 1k-unit volume, not quotes.

### Tier 1: "Cheap": LoRaWAN or LTE-M rail puck (primary battery, no panel)
- **Sensing:**
    - Rail: TMP117 (or batch-calibrated 10 kΩ NTC) in a spring-loaded brass slug at mid-web, shaded side, between sleepers, inside PTFE isolation.
    - Ambient: a second TMP117 in a louvered pocket on the shaded side, reported as "near-rail air".
    - The backend overlays NWP or SA Weather Service ambient for context.
- **Mounting:** 2–3 SH-grade NdFeB or large ferrite pot magnets; field side; at least 2 m from axle counters and track-circuit equipment.
- **Power:** one Li-SOCl<sub>2</sub> D-cell (about 17 Ah) plus a hybrid layer capacitor. No harvesting.
- **Connectivity:** two SKUs on one PCB footprint:
    - (a) LoRaWAN EU868 at SF7–SF9, 10-minute uplinks, needing a gateway (sold separately or shared). Expected life about 7–10 years.
    - (b) LTE-M/NB-IoT with PSM. Samples every 10 minutes, uploads hourly, alarms immediate. Expected life about 3–4 years.
- **Data and backend:** LoRaWAN network server or MQTT to a cloud dashboard. Configurable CRT(W)/(30/60)/(20)-style thresholds per site, with email/SMS alerts. 30-day on-device log.
- **Anti-theft:** low-value appearance, no external parts, accelerometer "moved/removed" alarm, serial marking, firmware lock.
- **Key risks:**
    - Ambient reading is not WMO-grade.
    - Needs coverage or a gateway; useless at a lone no-coverage site.
    - Magnet creep under EN 50125-3 shock.
    - Reduced battery life in deep-coverage cellular areas.
- **Cost:** BOM about US$35–60 (LoRa) or US$45–75 (LTE-M). Target selling price about US$150–300, roughly one-fifth of the price of a Yeltech RTMU (£1,628).

### Tier 2: "Mid-range": dual-mode connected node with store-and-forward and solar assist
- **Sensing:**
    - Rail: PT1000 Class A (or TMP117) at mid-web, plus a second contact sensor on the foot for gradient context.
    - Ambient: SHT4x in a mini multi-plate white ASA shield on a 15–25 cm thermally isolated standoff above the puck.
    - Plus an onboard accelerometer.
- **Mounting:** 3–4 SH-grade NdFeB pot magnets plus a polymer foot-hook secondary retention. Attachment detection activates the unit and logs a GNSS fix, as the SmartTemp design does.
- **Power:** a small integrated 0.5–1 W solar cell on a hardened glass top, an LTO pack (charges to -30 °C), and a Li-SOCl<sub>2</sub> backup cell, so the unit survives panel damage or theft. Adaptive cadence: 10-minute reporting when the rail is above about 35 °C or rising quickly, hourly otherwise, which is the same logic Yeltech uses.
- **Connectivity:** LTE-M/NB-IoT (Release 17 NTN-capable module for a future satellite fallback) plus a LoRa radio. Nodes out of cellular coverage relay to the nearest node or gateway that has it. 60-day store-and-forward.
- **Data and backend:** cloud platform with per-site CRT registers, a rail-minus-ambient analytics view, an API for operator systems (the equivalent of Network Rail RADAR 2 / Transnet maintenance systems), and over-the-air (OTA) firmware updates.
- **Anti-theft:** everything in Tier 1, plus GNSS on tamper, a geofence alarm, a tamper switch on the housing, and a rust-coloured body with a white solar cap.
- **Key risks:** a slightly visible solar cell invites tampering; cellular-only sites still need a gateway; LTE-M coverage along remote corridors; and EMC qualification to EN 50121-4.
- **Cost:** BOM about US$120–200. Target selling price about US$500–900 per node, plus about US$3–8/month for cellular data (small payloads: well under 1 MB/month).

### Tier 3: "Advanced": rail nodes plus off-rail weather and backhaul mast (site system)
- **Sensing:**
    - On the rail: 2–6 rail nodes per site on both rails and both sides, optionally including a strain-gauge or SFT option (Vortok MultiSensor-like) to track neutral-temperature drift.
    - On an off-rail mast set back beyond the OHTE structure gauge: WMO-style ambient at 1.5–2 m in a fan-aspirated shield, a pyranometer and an anemometer. These feed a physics/ML rail-temperature forecast (CNU/ENSCO-type), so CRT breaches can be predicted 6–24 h ahead.
- **Electrical safety architecture:** radio-only link (LoRa or 2.4 GHz) between the rail nodes, which float at rail potential, and the mast, which is earthed per local practice. There is no galvanic connection, which satisfies Transnet's "not earthed or bonded to the return rail" practice for low-voltage gear. If a mast is placed on an OHTE structure, treat it as bonded to rail on DC sections (Transnet CEE.0228.96 describes "Earthing and bonding masts to rail") and get Transnet approval.
- **Power:** mast with 20–40 W solar, LiFePO<sub>4</sub> or LTO storage with heater and charge inhibit, in a locked vault. Rail nodes as in Tier 2.
- **Connectivity:** multi-bearer mast gateway. LTE-M/LTE Cat-1 where coverage exists; otherwise Iridium SBD, with 10-minute data batched hourly and alarms sent immediately, at about US$80–150/month per site. NTN NB-IoT (Myriota, Sateliot, or a Starlink partner) added as it becomes available in South Africa.
- **Data and backend:** full platform with forecasting, CRT workflow (watch → 60/30 → 20 equivalents), audit trail, multi-site map, and integration with train control and maintenance systems.
- **Anti-theft:** anti-climb 3 m+ pole, security fasteners, vaulted batteries, CCTV-grade tamper and door sensors, siting inside secured perimeters, and GNSS on nodes.
- **Key risks:** the mast is a visible theft magnet; Transnet wayleave and approval for mast placement; satellite operating cost; and complexity of installation and maintenance.
- **Cost:** BOM about US$400–900 per mast (excluding pole and civils) plus about US$150–250 per rail node. Target selling price about US$3,000–8,000 per site plus a subscription.

## Caveats
- **Unverified technical values.** The EN 50125-3 on-rail vibration figures came from a garbled extraction. The EN 50122-1 values are from the 2011 edition (the 2022 edition plus A1:2025 may differ). The Transnet specification quotes are tender-document snippets. All should be confirmed against licensed copies and with Transnet Technology Management before design freeze. I found no SANS adoption of EN 50121/EN 50122 and no PRASA specification.
- **Estimates and datasheet values.** Battery-life, BOM and price figures are engineering estimates. Sensor accuracies are typical datasheet values. Satellite pricing and coverage are moving quickly, and Myriota's "<$1/month" is a vendor claim with no confirmed South African coverage.
- **Gaps in the benchmark survey.** I did not identify a current Salient Systems rail-temperature product, nor verify the 2026 status of Astrocast or Lacuna.
- **Field validation needed.** Magnet holding force, axle-counter interaction and the thermal-isolation performance of the contact slug and ambient shield must be validated by field trials on a South African line. Cape-gauge rail profiles and 3 kV DC return currents differ from UK benchmarks.
