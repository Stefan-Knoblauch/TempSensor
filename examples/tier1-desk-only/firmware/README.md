# Tier 1 firmware core (desk-only example)

This folder holds the portable C99 core of the Tier 1 rail temperature sensor firmware: the payload codec, the state machine and scheduler, the priority queues, the persistent sample log and the persistent device state (nvstate, R-9). It implements sections 6 and 7 of `docs/design/tier1-core-design.md`, plus decisions D-11 (LTE-M cadences, 18-byte config) and D-13 (rail lag compensation). It does no dynamic allocation and uses no external libraries. All hardware access goes through `rtsm_hal_t`.

```
include/  payload.h  rtsm_hal.h  rtsm.h  queue.h  rlog.h  nvstate.h
src/      payload.c  rtsm.c (state machine + scheduler)  queue.c  rlog.c (log ring)
          nvstate.c (A/B persistent state record)
test/     test_main.c test_payload.c test_rtsm.c sim_hal.[ch] test.h
          gen_vectors.c -> payload_vectors.json
```

## How to run

```
make test      # builds with gcc -std=c99 -Wall -Wextra -Werror -pedantic,
               # writes test/payload_vectors.json, runs all tests (exit 1 on failure)
make vectors   # only regenerate test/payload_vectors.json
make clean
```

There are 47 tests (about 14,600 checks). They include two simulated 32-day runs, two 30-day flash-wear runs, resets in RUN and LOCKED, corrupted NV slots and 48-hour outages, and the whole suite takes about a second. A few tests print `info:` lines with the measured NV write and send-attempt counts.

`test/payload_vectors.json` holds 7 vectors: two batches (n=6 with a rail 0x8000 fault, and n=2 with negative values), two alarms, a status, an install and an 18 B config downlink. They are produced by the real encoders. A separate Python decoder has been checked against them.

## Porting

Fill in `rtsm_hal_t`. It provides the RTC in Unix seconds, rail and air temperature in cC (0x8000 on fault), Hall, lid and IMU (a latched event, cleared on read), battery %, an optional energy counter (mAh since power-up), optional link quality, `radio_send` (true only once the bearer has accepted the message) and log slot read/write. Three functions are optional and new for R-9:

- `link_available()` returns 1 (registered / joined), 0 (known down) or -1 (unknown). NULL means unknown. On LTE-M this should be a cheap modem query such as `+CEREG?` that does not power up the radio.
- `nv_read(slot, buf, len)` / `nv_write(slot, buf, len)` hold the 47-byte nvstate record in slot 0 (A) or 1 (B). `nv_write` erases the slot first if the medium needs it. A 4 KiB sector of internal flash per slot, or two pages of the SPI NOR, is enough. If both are NULL, nothing persists and every boot starts from factory defaults.

Then:

```c
rtsm_init(&sm, &hal, &init);       /* SKU, hw_rev, fw_version, factory unlock token;
                                      loads nvstate, bumps reset_count, writes once */
loop: rtsm_step(&sm);              /* after every wake-up */
      rtsm_on_downlink(&sm, b, n); /* when a downlink arrives */
      sleep until rtsm_next_wakeup(&sm) or a Hall/IMU/lid IRQ
```

The port no longer passes the seq counter or reset count, and it no longer needs to save the config. The core keeps all of these in nvstate. `init.unlock_token` is the factory default. It is used only when NV holds no valid record (first boot, or both slots corrupt), and after that the NV copy is authoritative. `rtsm_set_unlock_token()` rotates the token and persists it. `hal.now()` must be valid when `rtsm_init` is called.

## Design notes

**States.** The unit has a visible `state` and an underlying `op_state` (COMMISSION, RUN or HOT). ALARM_TAMPER is laid over `op_state`. While it is active, sampling, level alarms, HOT detection and data reporting all continue, and resolving the tamper returns the unit to `op_state`. That is also how a unit that was HOT returns to HOT.

- **SHIP:** only the Hall sensor is read. The unit moves to COMMISSION after the sensor has read attached continuously for at least 60 s.
- **COMMISSION:** the first sample is taken at entry and sets the 10-minute grid anchor. An Install message is queued at entry and again every 6 h until config arrives. Records are logged but not sent until a valid 0x81 arrives. They are then back-filled.
- **Tamper:** an IMU movement event or a Hall "detached" reading starts an episode. The unit sends 0x10 and/or 0x11, each at most once per episode. The unit counts as reattached when the Hall sensor reads attached and there has been no new movement for `RTSM_TAMPER_SETTLE_S` (60 s), and this must happen within 30 min. Otherwise the unit sends 0x30 and goes to LOCKED.
- **Lid:** the open edge (closed to open) in any operating state sends 0x12 and goes to LOCKED.
- **LOCKED:** no sampling and no data. Already-queued alarms are flushed, and a status beacon goes out every 6 h. Only 0x83 with the right token is acted on; it moves the unit to COMMISSION, where it needs a new config.

**HOT.** The unit enters HOT when compensated rail >= `hot_threshold`, or when rail minus the record from exactly 1 h earlier >= `rate_of_rise` (0 disables the rate check). It leaves HOT at the first sample that is at least 1 h after the first sample where the condition cleared. If the condition returns during that hour, the hour restarts.

**Alarm levels.** The level rises as soon as rail >= Lk. It falls only when rail < Lk − hysteresis. Each change raises one alarm (0x01 or 0x02) carrying the new level in the record flags, including a jump of several levels at once.

**Lag compensation (D-13).** `rail = raw + RAIL_TAU_COMP_S*(raw − raw_prev)/dt`. This is done in integer cC, rounded half away from zero and clamped to ±32767, so it never produces 0x8000. It is applied only when both values are valid and 300 <= dt <= 900 s, where dt is measured between the actual read times. The compensated value is what is logged, sent and used for HOT, rate of rise and levels. The air channel is not compensated.

**Reporting.**
- **LoRa:** every tick sends `[previous, current]` (n=2). The previous record is repeated on purpose.
- **LTE-M (D-11):** the cadence is chosen at each sample:
  - HOT (`hot_batch_min`, 10/20/30) if `op_state` is HOT
  - otherwise COOL (`cool_batch_min`, 60/120/240) if rail < `cool_threshold`
  - otherwise NORMAL (`batch_period_min`, 10/20/30/60)

  A batch boundary is a grid time where (t − anchor) is a multiple of the current period. The batch at a boundary contains every unsent, contiguous record since the previous boundary, up to 48. Recommended config values: cool_threshold 2500, cool_batch_min 240, hot_batch_min 20.
- **Alarms** are sent immediately in every state and cadence.
- **Heartbeat:** a daily status message, timed from the commissioning anchor.

**Transmit priority.** On every 10-min tick, sending happens in this order:
1. The alarm queue.
2. The status/install queue.
3. The newest-data batch.
4. Back-fill: unsent records oldest-first, in contiguous runs (up to 8 per message for LoRa, 48 for LTE-M), at most `RTSM_BACKLOG_MAX_PER_TICK` (6) messages per tick.

The first failure stops the tick. Between ticks, queued alarms are retried with back-off of 60, 120, 240, 480 and then 600 s, and a new alarm triggers an attempt straight away. Status and data follow the long back-off and the `link_available()` gate described under "Outage behaviour". The exception is the Install/status queue during COMMISSION, which keeps the short back-off because an installer is waiting on site. If a data send fails, the next tick is treated as a batch boundary. After reconnection the order is therefore alarms, then the newest data, then the backlog oldest-first.

**Log and sent-tracking scheme.** This uses per-record sent flags, not a watermark, because records are sent out of order (newest first, then the backlog).
- Each slot holds time, rail, air, flags and a status byte: 0xFF erased, 0xA5 unsent, 0x05 sent. Marking a record sent only clears bits, so NOR flash can do it in place.
- A record is marked sent only after `radio_send` returns true.
- Back-fill never picks a record that is already marked sent, so the only repeat ever sent is the deliberate LoRa n=2 previous record.
- The ring holds 4608 records (32 days).
- A scan pointer skips the sent prefix.
- `rlog_init` rebuilds the indices after a reset by scanning the slots.
- If a 33rd day passes without a link, the oldest unsent records are overwritten and counted in `lost_unsent`.

**Seq.** There is one u16 counter that wraps. Alarm, status and install messages take their seq when they are queued and keep it across retries. A data batch takes its seq when it is attempted and hands it back if the attempt fails, so the platform sees no gaps (tested across the 0xFFFF to 0 wrap). The one exception: if a queue overflows (32 alarms or 8 status messages are kept, oldest dropped), the dropped message's seq leaves a real gap.

**Persistence across reset (nvstate, R-9).** One 47-byte record holds:
- the state class: SHIP, SHIP after service mode, commissioned RUN, or LOCKED
- the 18-byte config, stored as the 0x81 downlink and re-validated by the decoder at boot
- the unlock token
- reset_count (u16)
- seq_reserved (u16)
- the cumulative energy in mAh (u32)

It is stored in two A/B slots with a magic, a version, a u32 generation counter and a CRC-32 (IEEE). At boot both slots are read, and the valid one with the higher generation wins. A write always goes to the other slot with generation + 1, so a power cut during a write leaves the previous record intact. If both slots are blank or corrupt, the unit falls back to factory defaults: SHIP, no config, the factory token and counters at 0.

- **When it writes:** only on change of state class, config or token. In addition it writes once per boot (reset_count + 1 and a new seq block, in a single write), once per seq block, and once each time the energy total has grown by `RTSM_NV_ENERGY_STEP_MAH` (50 mAh) since the last write, checked at each sample. `nvs_t.writes` counts the writes. The state-class write for LOCKED happens inside `enter_locked`, before the 0x12/0x30 alarm is sent, so pulling the battery after a lock does not unlock the unit.
- **Seq:** block reservation. NV holds `seq_reserved`, and before the core uses a seq at or beyond it, it writes `seq_reserved = seq + RTSM_NV_SEQ_BLOCK` (256). After a reset the counter resumes at `seq_reserved`. A seq is therefore never reused, and a reset leaves a gap of up to 256. The hand-back of a failed batch's seq still applies within a run.
- **Boot by class:**
  - **RUN:** RUN resumes with the stored config. A new 10-minute grid starts at boot. The log is recovered by `rlog_init`, and its unsent records (including the ones that were waiting for an LTE-M boundary) are back-filled exactly once. A status message goes out at once, carrying the new reset_count.
  - **LOCKED:** the unit stays LOCKED, sends a beacon at once and then one every 6 h. It accepts only 0x83 with the persisted token.
  - **SHIP after service mode:** the unit must still be seen detached before it can re-commission.
  - **SHIP, or COMMISSION without config:** the unit boots in SHIP and re-commissions after being attached for 60 s.
- **Energy:** the HAL counter counts from power-up. The core adds the persisted total and reports the sum, saturated to u16, in the status message. A reset loses at most the consumption since the last write, which is under 50 mAh.
- **Flash wear (measured over 30 days, link up, energy rising about 5 mAh/day):**
  - LoRa: 17 writes for 4350 uplinks. That is 0.57 writes/day, or 0.39 per 100 uplinks, because seq blocks dominate and each seq write also refreshes the energy total.
  - LTE-M (NORMAL hourly): 5 writes for 750 uplinks, which is 0.17 writes/day. Seq blocks alone account for about 0.1/day and energy steps for about 0.07/day.
  - Each reset and each state or config change adds one write. Over 10 years this is about 2,000 writes per slot pair, far below the endurance of any NOR or internal flash.

**Outage behaviour (R-9, LTE-M energy).** Messages fall into two classes:
- **Status/data:** batches, plus status and install messages outside COMMISSION. They are gated:
  - When `link_available()` returns 0, no status or data attempt is made on that tick.
  - When it returns 1, they are attempted as before.
  - When it returns -1 or is NULL, a long back-off runs while sends keep failing. After a failed attempt the next one waits 10, 20, 40, 80 and then 120 min, and every 120 min after that (`RTSM_DATA_BACKOFF_FIRST_S` 600 s, doubling, capped at `RTSM_DATA_BACKOFF_MAX_S` 7200 s). The gaps fall on 10-minute ticks.
- **Alarms:** not gated. They are attempted on every tick, plus the short 60…600 s retries between ticks.

Any successful uplink, an alarm included, resets both back-offs. The next permitted data tick is treated as a batch boundary, so after reconnection the newest data still goes before the back-fill. The same logic applies to the LoRa SKU. `send_attempts_total` and `send_failures_total` in `rtsm_t` count every radio attempt.

Measured in a 48-hour outage with the link state unknown: 27 status/data attempts, against 288 before. That is the same for LTE-M and LoRa. After reconnection every record is delivered exactly once on LTE-M, and with no loss on LoRa. With `link_available()` = 0 there are 0 status/data attempts, while a pending alarm is still attempted on every tick.

**Time sync (0x84).** This is stored as an offset to the HAL clock. Schedules keep running in HAL time, and record timestamps jump. A batch never spans the jump, because batches are built only from records exactly 600 s apart.

## Spec ambiguities resolved

1. **Config length:** 18 B, following the field list (D-11 correction). The 14 B v0 form is rejected.
2. **"Reattached":** Hall attached and no new IMU event for 60 s, completed inside the 30-min window. For an episode started by movement alone, the 60 s run from the movement.
3. **Lid open:** sends 0x12 only, with no extra 0x30. The lock is implied.
4. **Lid already open when entering COMMISSION:** this does not lock the unit. Only a closed-to-open edge counts, so the installer can close the lid after attaching.
5. **Service mode:** 0x82 mode 1 sends the unit to SHIP (radio off, no tamper or lid monitoring). Because the unit is usually still on the rail, SHIP after a service command must first see the unit detached before the 60 s attach rule re-arms. Without this, it would re-commission itself at once. Mode 0 while operating is acknowledged and does nothing. In SHIP and LOCKED, 0x82 is ignored.
6. **COMMISSION data:** COMMISSION samples are logged but not reported before config, since there are no levels yet. They are back-filled once the unit is in RUN. Low-battery, sensor-fault and tamper alarms are active in COMMISSION; level and HOT alarms are not.
7. **Config while running:** a config received in RUN or HOT is applied in place. Re-commissioning (after unlock or SHIP) always needs a new config (T1-20).
8. **LOCKED, "nothing else":** no sampling, data or new alarms. Alarms queued before the lock, including 0x30 and 0x12, are still delivered. The first beacon is sent 6 h after the lock.
9. **Exactly once vs. the LoRa n=2 repeat:** the repeat is the spec's own redundancy. Every other record goes out in exactly one successful message. The tests check exactly once for LTE-M, and for LoRa they check that every record is delivered with no back-fill duplicates.
10. **Rate limit for LTE-M:** back-fill may also go out on 10-min ticks between batch boundaries ("max N per 10-min tick"). Records belonging to the current open batch window are not back-fill.
11. **Missed samples:** if the MCU wakes late, the sample is stamped at the latest grid point and missed grid points are skipped, not back-dated.
12. **Status fields:** `tx_fail_count` counts failed attempts since the previous status message (saturating). `energy_mAh` is the cumulative total: the persisted total plus the HAL counter since power-up, saturated at 65535. `reset_count` is the low 8 bits of the persisted u16, and it is 1 on the first boot from blank flash. `link_quality` comes from an optional HAL function and is 0 when absent.
13. **Rate of rise:** needs the record from exactly 1 h earlier and a valid rail value. Otherwise only the threshold applies, for example in the first hour or after a gap.

## R-9 resolution

The residual risk R-9 in `../02-requirements.md` had three parts:

1. **Config, seq and token not persisted by the core:** resolved by `nvstate`. Config, state class (including LOCKED), unlock token, reset_count, energy total and a seq reservation are kept in A/B records with CRC-32. Tests cover:
   - a reset mid-RUN (RUN and config resume, no seq reuse, exactly-once back-fill)
   - a reset in LOCKED (stays LOCKED)
   - slot A corrupt (slot B is used)
   - both slots corrupt (SHIP defaults)
   - the flash-wear bound
2. **One LTE-M send attempt per 10-min tick during an outage:** resolved by the `link_available()` gate and the 10/20/40/80/120-min long back-off. A 48-hour outage now costs 27 attempts instead of 288. Updating the energy model with these numbers is still open, outside the firmware.
3. **Outages over 32 days overwrite the oldest unsent records (counted):** unchanged. This is a capacity limit of the log, and `lost_unsent` reports it.

## Deviations / limitations

- **State after reset (R-9, resolved):** see "Persistence across reset". What is not persisted:
  - The alarm level, HOT state and cadence. They are re-derived from the next samples, so a level alarm can be raised again after a reset.
  - A tamper episode in progress. The unit boots into RUN. If it is still detached, a new episode starts (0x11 again) and it locks 30 min later.
  - The time sync offset.
  - The 10-minute grid anchor. A new grid starts at boot.
  - Queued alarms and status messages, which are lost with RAM. Logged samples are not lost.
- **NV corner cases:**
  - If both slots are corrupt, the unit falls back to factory defaults. It needs re-commissioning, and the seq restarts at 0, so the platform must accept a seq reset together with a reset_count change.
  - If a completed record is later corrupted (bit rot) and the unit falls back to the older slot, seq values up to one block may be reused. A power cut during a write cannot cause this, because the reservation is written before the seq is used.
  - A boot loop costs one NV write per boot.
- **Log recovery after reset:** recovery assumes slots are written in order and timestamps rise. A backwards time sync on a full ring can confuse which record is treated as the newest.
- **Outage cost (R-9, resolved in firmware):** the unit makes at most about 1 status/data attempt per 120 min with the link state unknown, and none with `link_available()` = 0. Alarms are still attempted every tick. The energy model (outside this folder) still has to be extended with the cost of the 120-min retries and the `link_available()` query.
- **Install retries during COMMISSION** keep the short 60…600 s back-off and are not subject to the 120-min schedule. This is deliberate, because the installer is on site. Only `link_available()` = 0 suppresses them.
- **No 0x85+ downlinks and no confirmed-uplink logic:** `radio_send` success counts as delivery.
