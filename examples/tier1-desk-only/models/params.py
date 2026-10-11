"""Single source of truth for every number used by the Tier 1 desk-only models.

Each parameter carries an evidence tag:
  R  research / standard / datasheet value
  E  engineering estimate
  A  assumption (designer decision in this desk-only example)
Changing a value here changes every model and the verification report.
"""
from dataclasses import dataclass


@dataclass(frozen=True)
class P:
    value: float
    unit: str
    tag: str
    note: str


# --- Device -----------------------------------------------------------------
MASS_KG = P(0.60, "kg", "A", "Tier 1 mass limit (T1-12)")
SAMPLE_INTERVAL_MIN = P(10, "min", "A", "T1-03")

# --- Battery (Li-SOCl2 bobbin D-cell + hybrid layer capacitor) ---------------
CELL_NOMINAL_MAH = P(17000, "mAh", "R", "bobbin D-cell class, 17-19 Ah")
CELL_USABLE_MAH = P(12000, "mAh", "E", "derated for temperature, pulse load, passivation")
SELF_DISCHARGE_PER_YR = P(0.01, "1/yr", "R", "~1 %/yr of nominal")
CELL_CALENDAR_LIMIT_YR = P(12, "yr", "E", "practical calendar life cap at hot-rail temperatures")

# --- Sleep floor ----------------------------------------------------------------
SLEEP_UA = P(8.0, "uA", "E", "MCU stop2 + RTC + IMU wake-up + Hall + leakage")
LTEM_PSM_FLOOR_UA = P(4.0, "uA", "R", "nRF91-class modem PSM floor (datasheet typ.)")

# --- LoRa radio (STM32WLE5-class, EU868, +14 dBm) -------------------------------
LORA_TX_MA = P(45.0, "mA", "R", "STM32WL HP PA at +14 dBm, typ.")
LORA_RX_MA = P(5.5, "mA", "R", "STM32WL RX, typ.")
LORA_WAKE_MAS = P(1.0, "mAs", "E", "wake, TCXO start, sample, AES/MIC, MAC processing, RX-delay housekeeping (conservative)")
LORAWAN_OVERHEAD_B = P(13, "B", "R", "MHDR1+FHDR7+FPort1+MIC4")
APP_PAYLOAD_B = P(19, "B", "A", "batch n=2: 9 + 5*2")
LORA_PREAMBLE_SYM = P(8, "sym", "R", "EU868 default")
LORA_CR = P(1, "-", "R", "coding rate 4/5")
LORA_BW_HZ = P(125e3, "Hz", "R", "EU868 DR0-DR5")
LORA_RX2_SF = P(12, "-", "R", "EU868 default RX2 = DR0 (SF12); conservative (TTN uses SF9)")
LORA_RX_WINDOW_SYM = P(8, "sym", "E", "symbols listened per RX window when no downlink")
DUTY_CYCLE_LIMIT = P(0.01, "-", "R", "ICASA / EU868 g1 sub-band 1 %")
FAIR_USE_S_PER_DAY = P(30.0, "s", "R", "community network fair use (e.g. ONISA); private network not bound")

# LoRa sensitivity at BW125 (SX126x/STM32WL datasheet, typ.)
LORA_SENS_DBM = {7: -124.0, 8: -127.0, 9: -130.0, 10: -133.0, 11: -135.5, 12: -137.0}

# --- LTE-M (nRF9151-class) -----------------------------------------------------
LTEM_SESSION_MAH_GOOD = (0.1, 0.3)  # R: research range per report in good coverage
LTEM_DEEP_FACTOR = P(3.0, "x", "R", "deep-coverage repetitions multiply 3-10x; 3 used as design case")
LTEM_BATCH_PER_DAY = P(24, "/day", "A", "hourly batch")

# --- Hot season (drives HOT-mode immediate reporting on LTE-M) -----------------
HOT_DAYS_PER_YR = P(120, "day", "E", "days per year with rail >= HOT threshold")
HOT_HOURS_PER_DAY = P(6, "h", "E", "hours per hot day in HOT mode (incl. 1 h hold)")

# --- Link budget ---------------------------------------------------------------
TX_ERP_DBM = P(14.0, "dBm", "R", "25 mW ERP SRD limit")
NODE_ANT_GAIN_DBI = P(-5.0, "dBi", "E", "internal antenna inside housing, next to rail steel")
GW_ANT_GAIN_DBI = P(3.0, "dBi", "E", "omni on pole")
GW_CABLE_LOSS_DB = P(1.0, "dB", "E", "")
NODE_HEIGHT_M = P(0.15, "m", "E", "antenna height above ballast surface, under rail head")
GW_HEIGHT_M = P(6.0, "m", "A", "gateway on own 6 m pole (TN-05)")
EXCESS_LOSS_DB = P(10.0, "dB", "E", "rail-head shadowing, ballast, vegetation")
FADE_MARGIN_DB = P(10.0, "dB", "E", "slow fading + passing-train shadowing")
FREQ_HZ = P(868.1e6, "Hz", "R", "")

# --- Magnets / retention ---------------------------------------------------------
N_MAGNETS = P(3, "-", "A", "")
MAG_RATED_PULL_KGF = P(34.0, "kgf", "R", "per Ø32 mm NdFeB SH pot magnet, zero gap, thick steel (typ. catalogue)")
MAG_HALF_PULL_GAP_MM = P(0.5, "mm", "E", "gap at which pot-magnet pull halves")
GAP_WORST_MM = P(0.4, "mm", "E", "D-24: contact spots wire-brushed at install -> residual scale 0.1 + paint 0.1 + seat mismatch 0.1 + friction coat 0.1 (was 0.6 unbrushed)")
MU_BARE_WORST = P(0.15, "-", "R", "shear/normal ratio, bare pot face (15-25 %)")
MU_COATED = P(0.5, "-", "E", "pot face with 0.1 mm high-friction PU coat on rusty web")
# EN 50125-3 on-rail random vibration; values from a garbled extraction, axis mapping assumed
VIB_RMS_MS2 = {"vertical": P(280, "m/s2", "R", "EN 50125-3 Table 5, unconfirmed"),
               "lateral": P(140, "m/s2", "R", "EN 50125-3 Table 5, unconfirmed"),
               "longitudinal": P(50, "m/s2", "R", "EN 50125-3 Table 5, unconfirmed")}
PEAK_FACTOR = P(3.0, "sigma", "E", "Gaussian random vibration design peak")
RETENTION_MARGIN = P(1.5, "x", "A", "required margin over 3-sigma inertial load")

# --- Thermal ---------------------------------------------------------------------
TMP117_ACC_C = P(0.2, "degC", "R", "TMP117 max error -55..150 degC (0.1 for -20..50)")
ADC_QUANT_C = P(0.0078, "degC", "R", "TMP117 LSB")
INSTALLED_ERROR_BUDGET_C = P(0.5, "degC", "R", "T1-01")
DCDC_EFFICIENCY = P(0.85, "-", "E", "buck/boost + HLC charge losses applied to active charge")
COOL_THRESHOLD_C = P(25.0, "degC", "A", "below this rail temp the LTE-M SKU batches every COOL_BATCH_MIN (Yeltech-style)")
COOL_BATCH_MIN = P(240, "min", "A", "cool-mode batch period; alarms still immediate")
COOL_FRACTION = P(0.4, "-", "E", "fraction of hours/year with rail < 25 degC (nights + winter, SA inland)")
N_COATED_POTS = P(2, "-", "A", "outer pots PU-coated; centre (sensing) pot bare for thermal contact (D-12/D-14)")
HOOK_LEVER_MM = P(6.0, "mm", "A", "hook lip lever from root to load line (D-25: shortened from 10 mm to lift the first mode above the band)")
HOOK_WIDTH_MM = P(30.0, "mm", "A", "hook width along rail")
HOOK_THICK_MM = P(10.0, "mm", "A", "hook root thickness (designed)")
PA_GF_FATIGUE_MPA = P(20.0, "MPa", "E", "PA6-GF30 bending fatigue at 1e7 cycles, 80 degC, conditioned (conservative)")
LTEM_FAILED_ATTEMPT_MAH = P(0.9, "mAh", "E", "failed LTE-M attempt (network search, no service) ~3x a good session")
OUTAGE_FRACTION = P(0.10, "-", "A", "design case: share of time a good-coverage site has no service")
OUTAGE_ATTEMPTS_PER_DAY_BACKOFF = P(13.5, "/day", "R", "firmware test: 27 attempts in a 48 h outage (10,20,40,80,120.. min back-off)")
OUTAGE_ATTEMPTS_PER_DAY_NAIVE = P(144, "/day", "R", "firmware before R-9 fix: one attempt per 10-min tick")
VIB_BAND_UPPER_HZ = P(2000, "Hz", "A", "assumed upper frequency of significant EN 50125-3 on-rail content")
MODAL_SEPARATION = P(1.25, "x", "A", "required first-mode margin above the band")
