# Tier 1 firmware core (desk-only example)

This folder holds the portable C99 core of the Tier 1 rail temperature sensor firmware: the payload codec, the state machine and scheduler, the priority queues and the persistent sample log. It implements sections 6 and 7 of `docs/design/tier1-core-design.md`, plus decisions D-11 (LTE-M cadences, 18-byte config) and D-13 (rail lag compensation). It does no dynamic allocation and uses no external libraries. All hardware access goes through `rtsm_hal_t`.

```
include/  payload.h  rtsm_hal.h  rtsm.h  queue.h  rlog.h
src/      payload.c  rtsm.c (state machine + scheduler)  queue.c  rlog.c (log ring)
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

There are 36 tests (about 14,000 checks). They include two simulated 32-day runs, and the whole suite takes well under a second.

`test/payload_vectors.json` holds 7 vectors: two batches (n=6 with a rail 0x8000 fault, and n=2 with negative values), two alarms, a status, an install and an 18 B config downlink. They are produced by the real encoders. A separate Python decoder has been checked against them.

## Porting

Fill in `rtsm_hal_t`. It provides the RTC in Unix seconds, rail and air temperature in cC (0x8000 on fault), Hall, lid and IMU (a latched event, cleared on read), battery %, an optional energy counter, optional link quality, `radio_send` (true only once the bearer has accepted the message) and log slot read/write. Then:

```c
rtsm_init(&sm, &hal, &init);       /* SKU, hw_rev, fw_version, reset_count, seq, unlock token */
loop: rtsm_step(&sm);              /* after every wake-up */
      rtsm_on_downlink(&sm, b, n); /* when a downlink arrives */
      sleep until rtsm_next_wakeup(&sm) or a Hall/IMU/lid IRQ
```

The port must persist the seq counter, reset count and unlock token in NV and pass them in at init. The core does not persist its own state machine state (see deviations).

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

The first failure stops the tick. Between ticks, queued alarms and status messages are retried with back-off of 60, 120, 240, 480 and then 600 s; a new alarm triggers an attempt straight away. If a data send fails, the next tick is treated as a batch boundary. After reconnection the order is therefore alarms, then the newest data, then the backlog oldest-first.

**Log and sent-tracking scheme.** This uses per-record sent flags, not a watermark, because records are sent out of order (newest first, then the backlog).
- Each slot holds time, rail, air, flags and a status byte: 0xFF erased, 0xA5 unsent, 0x05 sent. Marking a record sent only clears bits, so NOR flash can do it in place.
- A record is marked sent only after `radio_send` returns true.
- Back-fill never picks a record that is already marked sent, so the only repeat ever sent is the deliberate LoRa n=2 previous record.
- The ring holds 4608 records (32 days).
- A scan pointer skips the sent prefix.
- `rlog_init` rebuilds the indices after a reset by scanning the slots.
- If a 33rd day passes without a link, the oldest unsent records are overwritten and counted in `lost_unsent`.

**Seq.** There is one u16 counter that wraps. Alarm, status and install messages take their seq when they are queued and keep it across retries. A data batch takes its seq when it is attempted and hands it back if the attempt fails, so the platform sees no gaps (tested across the 0xFFFF to 0 wrap). The one exception: if a queue overflows (32 alarms or 8 status messages are kept, oldest dropped), the dropped message's seq leaves a real gap.

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
12. **Status fields:** `tx_fail_count` counts failed attempts since the previous status message (saturating). `energy_mAh` and `link_quality` come from optional HAL functions and are 0 when absent.
13. **Rate of rise:** needs the record from exactly 1 h earlier and a valid rail value. Otherwise only the threshold applies, for example in the first hour or after a gap.

## Deviations / limitations

- **State after reset:** the state machine state, config and seq counter are not persisted by the core. The port must supply seq and reset_count at init and is expected to save the config. After a reset the core starts in SHIP and the log is recovered.
- **Log recovery after reset:** recovery assumes slots are written in order and timestamps rise. A backwards time sync on a full ring can confuse which record is treated as the newest.
- **LTE-M during an outage:** the unit makes one data attempt per 10-min tick. On a real port a cheap "registered?" check could avoid this.
- **No 0x85+ downlinks and no confirmed-uplink logic:** `radio_send` success counts as delivery.
