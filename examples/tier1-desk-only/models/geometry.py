"""Single geometric description of the Tier 1 unit (mm).

Frame: x along the rail, y up, z away from the rail web (web face at z = 0, the unit
extends to +z on the field side). Positions are part centres. Used by:
  - packaging.py   (fit inside the housing, no clashes)
  - viewer/build_viewer.py (interactive 3-D view and parts list)
Shapes: box (size x,y,z), cyl (diameter, length, axis), shell (the housing body),
frame (gasket loop: outer x, outer y, section), louvre (vent slats).
"""

BODY_L, BODY_H, BODY_D = 114.0, 62.0, 53.0     # D-30: was 110 x 62 x 38; the D-cell and pot row did not fit
WALL = 2.5
CAP_T = 4.0                                     # top cap thickness (part of the 62 mm height)
LID_T = 3.0

# Internal cavity (inside walls, under the cap, in front of the lid)
CAVITY = {"x": (-BODY_L / 2 + WALL, BODY_L / 2 - WALL),
          "y": (-BODY_H / 2 + WALL, BODY_H / 2 - CAP_T),
          "z": (WALL, BODY_D - LID_T)}

POT_D, POT_H, POT_PITCH = 32.0, 9.0, 35.0      # D-30: pitch 35 (3 mm between pots)


def P(id, name, shape, pos, size=None, *, axis=None, d=None, length=None, color, mat,
      explode=(0, 0, 0), order=0, sku="both", inside=True, group, function, spec="",
      decision="", source=None, qty=1):
    return dict(id=id, name=name, shape=shape, pos=list(pos), size=list(size) if size else None,
                axis=axis, d=d, length=length, color=color, mat=mat, explode=list(explode),
                order=order, sku=sku, inside=inside, group=group, function=function, spec=spec,
                decision=decision, source=source, qty=qty)


RUST, GREY, STEEL, NICKEL, FR4 = "#7a3b22", "#c9ccc8", "#5d6166", "#b8bcc0", "#1f5a3a"

PARTS = [
    # --- enclosure ---------------------------------------------------------------
    P("body", "Housing body", "shell", (0, -CAP_T / 2, BODY_D / 2),
      (BODY_L, BODY_H - CAP_T, BODY_D), color=RUST, mat="polymer", inside=False, order=3,
      group="Enclosure", function="Insulating structure that carries everything and keeps the circuit electrically floating (TN-01/02). Moulded head-stop rib on top, pockets for the Hall sensor and the near-rail air sensor.",
      spec="PA6-GF30, UV-stabilised, rust-brown, 2.5 mm walls", decision="D-15, D-30", source="housing"),
    P("cap", "Top cap", "box", (0, BODY_H / 2 - CAP_T / 2, BODY_D / 2), (BODY_L, CAP_T, BODY_D),
      color=GREY, mat="polymer", inside=False, explode=(0, 46, 0), order=4, group="Enclosure",
      function="Light-grey cap that shades the sensing pot from the sun and carries the antenna away from the rail steel.",
      spec="ASA, light grey (solar absorptance ~0.35)", decision="D-12", source="housing"),
    P("lid", "Battery-bay lid", "box", (0, (BODY_H / 2 - CAP_T - BODY_H / 2 + WALL) / 2, BODY_D - LID_T / 2),
      (BODY_L - 2 * WALL, BODY_H - CAP_T - WALL, LID_T),
      color=RUST, mat="polymer", inside=False, explode=(0, 0, 140), order=10, group="Enclosure",
      function="Closes the battery bay. Carries the lid magnet for tamper detection and the pressure-equalising vent.",
      spec="PA6-GF30, 3 mm", decision="D-19, D-20", source="housing"),
    P("oring", "Lid gasket", "frame", (0, -CAP_T / 2, BODY_D - LID_T - 0.6), (BODY_L - 10, BODY_H - CAP_T - 10, 1.6),
      color="#202224", mat="rubber", inside=False, explode=(0, 0, 124), order=9, group="Enclosure",
      function="Seals the lid for IP67/IP68.", spec="EPDM or FKM, 1.6 mm section", decision="T1-09", source="oring"),
    P("screws", "Security screws (×4)", "screws", (0, -CAP_T / 2, BODY_D + 1), (BODY_L - 14, BODY_H - CAP_T - 14, 3),
      color=NICKEL, mat="metal", inside=False, explode=(0, 0, 168), order=11, qty=4, group="Enclosure",
      function="Pin-in Torx screws hold the lid. Opening the lid without a service token locks the unit.",
      spec="A2 stainless, M3, pin-in Torx", decision="T1-18", source="screws"),
    P("vent", "Pressure vent", "cyl", (-38, -16, BODY_D + 0.8), axis="z", d=9, length=2.4,
      color="#e8e8e2", mat="polymer", inside=False, explode=(0, 0, 152), order=10, group="Enclosure",
      function="ePTFE membrane lets the air volume breathe through daily 30–70 °C cycles without pumping water past the seals.",
      spec="Adhesive ePTFE vent, IP68", decision="D-20", source="vent"),
    P("hook", "Hook slider", "hook", (0, -BODY_H / 2 - 12.5, 5), (30, 25, 10),
      color="#3b3d40", mat="polymer", inside=False, explode=(0, -52, 0), order=2, group="Mounting",
      function="Rests on the rail foot and, with the head-stop rib, traps the unit vertically. Set in 1 mm detents to the 48/57/60 kg profile mark, locked by a security screw.",
      spec="PA6-GF30, 10 mm root, 6 mm lip lever, 14–50 mm travel", decision="D-14, D-15, D-25", source="housing"),
    # --- rail interface ----------------------------------------------------------
    P("potA", "Pot magnet A (coated)", "pot", (-POT_PITCH, 0, POT_H / 2), axis="z", d=POT_D, length=POT_H, inside=False,
      color=NICKEL, mat="metal", explode=(0, 0, -42), order=0, group="Rail interface",
      function="Clamps the unit to the web. A 0.1 mm PU friction coat resists sliding along the rail.",
      spec="Ø32 NdFeB SH pot, ~34 kgf rated; 151 N at 0.4 mm on hot rail steel", decision="D-14, D-24", source="pot"),
    P("potC", "Pot magnet C (sensing)", "pot", (0, 0, POT_H / 2), axis="z", d=POT_D, length=POT_H, inside=False,
      color=NICKEL, mat="metal", explode=(0, 0, -42), order=0, group="Rail interface",
      function="Bare steel face clamps hard onto the rail, so its cup sits at rail temperature. The rail sensor is bonded to its back.",
      spec="Ø32 NdFeB SH pot, bare face", decision="D-12", source="pot"),
    P("potB", "Pot magnet B (coated)", "pot", (POT_PITCH, 0, POT_H / 2), axis="z", d=POT_D, length=POT_H, inside=False,
      color=NICKEL, mat="metal", explode=(0, 0, -42), order=0, group="Rail interface",
      function="Clamps the unit to the web; PU friction coat.", spec="Ø32 NdFeB SH pot", decision="D-14", source="pot"),
    P("pad", "Isolation pad", "box", (0, 0, POT_H + 0.25), (5, 5, 0.5), color="#9ec3d6", mat="rubber",
      explode=(0, 64, -22), order=1, group="Rail interface",
      function="Conducts heat from the pot to the sensor while holding off ≥4 kV, so the circuit is never bonded to the rail.",
      spec="Silicone, 0.5 mm, ~3 W/m·K, ≥4 kV", decision="T1-14, TN-02", source="pad"),
    P("tmp1", "Rail sensor (TMP117 #1)", "box", (0, 0, POT_H + 0.5 + 0.45), (3, 3, 0.9), color="#141414", mat="chip",
      explode=(0, 78, -18), order=1, group="Sensing",
      function="Measures the cup temperature (= rail web temperature) to ±0.1–0.2 °C. Firmware adds 150 s lag compensation.",
      spec="TI TMP117, I²C 0x48, on flex", decision="D-12, D-13", source="tmp117"),
    P("guard", "Thermal guard bridge", "box", (0, 0, 12), (2 * POT_PITCH + POT_D - 4, 30, 2), color=STEEL, mat="metal",
      explode=(0, 42, -16), order=1, group="Sensing",
      function="Steel bridge tied to the outer pots so the sensor's surroundings sit at rail temperature; cuts the reading error from ~2 °C to ~0.25 °C.",
      spec="2 mm mild steel, laser cut, ~40 g", decision="D-12", source="guard"),
    P("tmag", "Attachment sensor (TMAG5273)", "box", (-(POT_PITCH + POT_D / 2 + 2.0), 0, 3.5), (2, 3, 1), color="#141414", mat="chip",
      explode=(-36, 0, -30), order=1, group="Sensing",
      function="3-D Hall sensor 2 mm beside the outer rim of pot A. Detects attachment and removal from the change in the magnet's field vector.",
      spec="TI TMAG5273, I²C; ‖B − B_ref‖ > 10 mT", decision="D-18 rev", source="tmag5273"),
    P("tmp2", "Near-rail air sensor (TMP117 #2)", "box", (40, -BODY_H / 2 + WALL + 1.5, 30), (3, 0.9, 3), color="#141414", mat="chip",
      explode=(0, -44, 0), order=2, group="Sensing",
      function="Reads air in a louvred pocket on the lower face. Reported as 'near-rail air', not ambient (it reads 4–8 °C warm in sun).",
      spec="TI TMP117, I²C 0x49", decision="T1-02", source="tmp117"),
    P("louvre", "Air-pocket louvres", "louvre", (40, -BODY_H / 2 + 0.6, 30), (22, 1.2, 16), color=RUST, mat="polymer",
      inside=False, explode=(0, -30, 0), order=2, group="Sensing",
      function="Shaded slots that let air reach the near-rail air sensor.", spec="Moulded in the body", decision="T1-02", source="housing"),
    P("foam", "Foam wall", "box", (0, -CAP_T / 2, 14), (BODY_L - 2 * WALL, BODY_H - CAP_T - 2 * WALL, 2), color="#8c8678", mat="foam",
      explode=(0, 0, 22), order=4, group="Enclosure",
      function="Thermal break between the rail-temperature zone (pots, guard) and the battery/electronics bay.",
      spec="Closed-cell PE foam, 2 mm", decision="D-12", source=None),
    # --- power ---------------------------------------------------------------------
    P("cradle", "Cell cradle", "cradle", (-20, -20.6, 32.2), (64, 6, 34), color="#4a4d50", mat="polymer",
      explode=(0, -34, 42), order=5, group="Power",
      function="Rigid cradle with a thin hard liner. Keeps the cell's first mode at 5.7 kHz, above the rail vibration band; the cell stays replaceable.",
      spec="PA6-GF30 + 0.5 mm Shore-90 PU liner", decision="D-27", source="housing"),
    P("cell", "Li-SOCl₂ D-cell", "cyl", (-20, -0.5, 32.2), axis="x", d=34.2, length=61.5, color="#2e6f9e", mat="cell",
      explode=(0, 0, 62), order=6, group="Power",
      function="Primary 3.6 V cell: ≥7 years on LoRa, 3.3–3.9 years on LTE-M. Wide temperature range, ~1 %/yr self-discharge.",
      spec="3.6 V, ~17–19 Ah bobbin D-cell", decision="D-17", source="cell"),
    P("hlc", "Pulse capacitor (HLC)", "cyl", (31, 0, 28), axis="y", d=14.5, length=50, color="#8a8f3a", mat="cell",
      explode=(18, 0, 80), order=7, group="Power",
      function="Hybrid layer capacitor supplies the radio's current pulses that the bobbin cell cannot.",
      spec="3.6–3.9 V HLC, AA size", decision="D-17", source="hlc"),
    # --- electronics -------------------------------------------------------------
    P("pcb", "Baseboard PCB", "box", (32, -2, 44.2), (40, 50, 1.6), color=FR4, mat="pcb",
      explode=(0, 0, 102), order=8, group="Electronics",
      function="Common board for both versions: sensors, log memory, accelerometer, power path. Corner screws plus a centre standoff (first mode 5.0 kHz).",
      spec="4-layer FR-4, 1.6 mm, conformal coated", decision="D-26", source="pcb"),
    P("modL", "LoRa module (RAK3172)", "box", (32, 10, 42.3), (15, 15.5, 2.2), color="#30343a", mat="metalcan", sku="lora",
      explode=(0, 0, 90), order=8, group="Electronics",
      function="STM32WLE5 microcontroller + LoRa radio: runs the firmware and sends EU868 uplinks every 10 min.",
      spec="RAK3172 (STM32WLE5CC)", decision="D-01", source="rak3172"),
    P("modC", "LTE-M module (nRF9151)", "box", (32, 10, 42.4), (11, 16, 1.9), color="#30343a", mat="metalcan", sku="ltem",
      explode=(0, 0, 90), order=8, group="Electronics",
      function="nRF9151 SiP: microcontroller + LTE-M/NB-IoT modem (Release 17 NTN-ready). Batches every 4 h / 1 h / 20 min by rail temperature.",
      spec="Nordic nRF9151", decision="D-01, D-11", source="nrf9151"),
    P("imu", "Accelerometer (LIS2DW12)", "box", (22, -12, 43.0), (2, 2, 0.7), color="#141414", mat="chip",
      explode=(0, 0, 90), order=8, group="Electronics",
      function="Wakes the unit on movement or shock (tamper) and checks the unit is upright on the web.",
      spec="ST LIS2DW12, ~1 µA wake-up mode", decision="T1-17", source="lis2dw12"),
    P("flash", "Log memory (8 Mbit NOR)", "box", (42, -12, 43.0), (4, 4, 0.6), color="#141414", mat="chip",
      explode=(0, 0, 90), order=8, group="Electronics",
      function="Stores ≥32 days of 10-minute readings plus the A/B persistent-state slots.",
      spec="Macronix MX25R8035F", decision="T1-06, D-28", source="mx25r"),
    P("reed", "Lid reed switch", "box", (46, 18, 46.2), (2, 6, 2), color="#cfd6d8", mat="glass",
      explode=(0, 0, 114), order=8, group="Electronics",
      function="Closed by the lid magnet. Lid opened without a service token → unit locks (alarm 0x12).",
      spec="SMD reed switch", decision="D-19", source="reed"),
    P("lidmag", "Lid magnet", "cyl", (46, 18, BODY_D - 1.6), axis="z", d=4, length=2, color=NICKEL, mat="metal", inside=False,
      explode=(0, 0, 140), order=10, group="Electronics",
      function="Holds the reed switch closed while the lid is fitted.", spec="Ø4 × 2 mm NdFeB", decision="D-19", source="lidmag"),
    P("antL", "868 MHz antenna", "box", (0, BODY_H / 2 - CAP_T - 0.6, 30), (42, 0.2, 10), color="#c58b2a", mat="flex", sku="lora",
      explode=(0, 72, 0), order=5, group="Electronics",
      function="Flexible antenna inside the top cap, as far from the rail steel as the envelope allows.",
      spec="868 MHz FPC antenna", decision="D-16", source="ant868"),
    P("antC", "LTE-M antenna", "box", (0, BODY_H / 2 - CAP_T - 0.6, 30), (42, 0.2, 10), color="#c58b2a", mat="flex", sku="ltem",
      explode=(0, 72, 0), order=5, group="Electronics",
      function="Wideband LTE flexible antenna inside the top cap.", spec="LTE-M FPC antenna", decision="D-01", source="antlte"),
]


def as_json():
    return {"body": {"L": BODY_L, "H": BODY_H, "D": BODY_D, "wall": WALL, "cap": CAP_T, "lid": LID_T},
            "pot": {"d": POT_D, "h": POT_H, "pitch": POT_PITCH}, "parts": PARTS}
