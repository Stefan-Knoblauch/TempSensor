/*
 * rtsm.c - Tier 1 state machine, sampler and transmit scheduler.
 *
 * Structure:
 *   rtsm_step()        entry point after every wake-up
 *     ship_step()      SHIP: Hall debounce only
 *     check_tamper()   COMMISSION/RUN/HOT/ALARM_TAMPER: lid, IMU, Hall
 *     operating_step() 10-min sampling, heartbeat, install, transmit
 *     locked_step()    LOCKED: 6-hourly beacon, flush queued alarms
 *   rtsm_on_downlink() 0x81..0x84
 *
 * Persistence (R-9): config, state class, unlock token, reset count, a seq
 * reservation and the energy total live in nvstate (A/B record). They are
 * written on change only, plus once per boot, once per RTSM_NV_SEQ_BLOCK
 * uplinks and once per RTSM_NV_ENERGY_STEP_MAH.
 *
 * Timing is done in HAL time (RTC seconds). Device Unix time used in
 * payloads is HAL time + time_offset, so a 0x84 time sync never disturbs
 * the schedules. All "due" comparisons are plain >= on uint32 Unix seconds
 * (valid until 2106).
 */
#include "rtsm.h"

#include <string.h>

#define HOUR_S 3600u

/* ======================================================================== */
/* Small helpers                                                            */
/* ======================================================================== */

static uint32_t hal_now(const rtsm_t *r)
{
    return r->hal->now(r->hal->ctx);
}

static uint32_t to_unix(const rtsm_t *r, uint32_t hal_t)
{
    return hal_t + r->time_offset; /* mod 2^32 */
}

uint32_t rtsm_device_time(const rtsm_t *r)
{
    return to_unix(r, hal_now(r));
}

static uint32_t min_u32(uint32_t a, uint32_t b)
{
    return a < b ? a : b;
}

static bool is_operating(rtsm_state_t s)
{
    return s == RTSM_COMMISSION || s == RTSM_RUN || s == RTSM_HOT ||
           s == RTSM_ALARM_TAMPER;
}

/* Change the underlying operating state; the visible state follows unless
 * a tamper episode is overlaid on top of it. */
static void set_op_state(rtsm_t *r, rtsm_state_t s)
{
    r->op_state = s;
    if (r->state != RTSM_ALARM_TAMPER) {
        r->state = s;
    }
}

static bool queues_pending(const rtsm_t *r)
{
    return mq_count(&r->q_alarm) > 0u || mq_count(&r->q_status) > 0u;
}

/* ======================================================================== */
/* Persistence (nvstate)                                                    */
/* ======================================================================== */

uint32_t rtsm_energy_total(const rtsm_t *r)
{
    uint32_t e = r->hal->energy_mAh ? r->hal->energy_mAh(r->hal->ctx) : 0u;
    return r->energy_base + e;
}

/* State class that a reset must come back to. */
static uint8_t nv_class(const rtsm_t *r)
{
    rtsm_state_t s = r->state == RTSM_ALARM_TAMPER ? r->op_state : r->state;
    switch (s) {
    case RTSM_SHIP:
        return r->ship_armed ? (uint8_t)NVS_CLASS_SHIP : (uint8_t)NVS_CLASS_SHIP_SERVICE;
    case RTSM_RUN:
    case RTSM_HOT:
        return r->have_config ? (uint8_t)NVS_CLASS_RUN : (uint8_t)NVS_CLASS_SHIP;
    case RTSM_LOCKED:
        return (uint8_t)NVS_CLASS_LOCKED;
    default:
        return (uint8_t)NVS_CLASS_SHIP; /* COMMISSION: no config yet */
    }
}

/* What NV should hold now (energy and seq reservation as last written). */
static void nv_want(const rtsm_t *r, nvs_data_t *d)
{
    *d = r->nv.d;
    d->state_class = nv_class(r);
    d->have_config = r->have_config;
    memset(d->config, 0, sizeof d->config);
    if (r->have_config) {
        (void)pl_encode_config(d->config, sizeof d->config, &r->cfg);
    }
}

static void nv_write_now(rtsm_t *r, nvs_data_t *d)
{
    d->energy_mAh = rtsm_energy_total(r);
    (void)nvs_commit(&r->nv, d);
}

/* Write only if something changed (or the energy total moved by a step,
 * when check_energy is set - that reads the HAL counter). */
static void nv_persist(rtsm_t *r, bool check_energy)
{
    nvs_data_t d;
    bool due = false;
    nv_want(r, &d);
    if (check_energy && r->hal->energy_mAh != NULL) {
        uint32_t e = rtsm_energy_total(r);
        due = e > r->nv.d.energy_mAh && e - r->nv.d.energy_mAh >= RTSM_NV_ENERGY_STEP_MAH;
    }
    if (due || nvs_differs(&r->nv, &d)) {
        nv_write_now(r, &d);
    }
}

/* Every seq the core puts on air goes through here. Before a value at or
 * beyond the NV reservation is used, a new block is reserved, so a reset
 * (which resumes at the reservation) can never reuse a seq. */
static uint16_t next_seq(rtsm_t *r)
{
    uint16_t left = (uint16_t)(r->nv.d.seq_reserved - r->seq);
    if (left == 0u || left > RTSM_NV_SEQ_BLOCK) {
        nvs_data_t d;
        nv_want(r, &d);
        d.seq_reserved = (uint16_t)(r->seq + RTSM_NV_SEQ_BLOCK);
        nv_write_now(r, &d);
    }
    return pl_seq_next(&r->seq);
}

/* ======================================================================== */
/* Link state and send gating                                               */
/* ======================================================================== */

static int link_state(const rtsm_t *r)
{
    int l = r->hal->link_available ? r->hal->link_available(r->hal->ctx) : -1;
    return l > 0 ? 1 : (l == 0 ? 0 : -1);
}

/* Status/install messages use the long back-off, except during
 * COMMISSION where an installer is waiting on site (short back-off). */
static bool status_long_backoff(const rtsm_t *r)
{
    return r->state == RTSM_LOCKED || r->op_state != RTSM_COMMISSION;
}

/* May a status/data message be attempted now? Link known down: no.
 * Link known up: yes. Unknown: only once the long back-off has expired. */
static bool data_gate(const rtsm_t *r, uint32_t now, bool long_backoff)
{
    int l = link_state(r);
    if (l == 0) {
        return false;
    }
    if (l > 0 || !long_backoff) {
        return true;
    }
    return now >= r->data_next_ok;
}

const char *rtsm_state_name(rtsm_state_t s)
{
    switch (s) {
    case RTSM_SHIP: return "SHIP";
    case RTSM_COMMISSION: return "COMMISSION";
    case RTSM_RUN: return "RUN";
    case RTSM_HOT: return "HOT";
    case RTSM_ALARM_TAMPER: return "ALARM_TAMPER";
    case RTSM_LOCKED: return "LOCKED";
    default: return "?";
    }
}

/* ======================================================================== */
/* D-13 rail lag compensation                                               */
/* ======================================================================== */

int16_t rtsm_rail_compensate(int16_t raw, int16_t prev, uint32_t dt_s)
{
    int32_t num, corr, out, dt;
    if (raw == PL_TEMP_NA || prev == PL_TEMP_NA || dt_s < RAIL_COMP_DT_MIN_S ||
        dt_s > RAIL_COMP_DT_MAX_S) {
        return raw;
    }
    dt = (int32_t)dt_s;
    num = (int32_t)RAIL_TAU_COMP_S * ((int32_t)raw - (int32_t)prev);
    /* Round half away from zero (symmetric for rising and falling). */
    corr = num >= 0 ? (num + dt / 2) / dt : -((-num + dt / 2) / dt);
    out = (int32_t)raw + corr;
    /* Keep inside the valid range; never produce the 0x8000 marker. */
    if (out > 32767) out = 32767;
    if (out < -32767) out = -32767;
    return (int16_t)out;
}

/* ======================================================================== */
/* Transmission primitives                                                  */
/* ======================================================================== */

/* One radio attempt. Failure starts/extends the short retry back-off, and
 * for status/data (`data_class`) also the long data back-off. Any success
 * clears both. */
static bool radio_tx(rtsm_t *r, const uint8_t *buf, size_t len, uint32_t now, bool data_class)
{
    r->send_attempts_total++;
    if (r->hal->radio_send(r->hal->ctx, buf, len)) {
        r->backoff_s = 0;
        r->data_backoff_s = 0;
        r->data_next_ok = 0;
        return true;
    }
    r->send_failures_total++;
    if (data_class) {
        r->data_backoff_s = r->data_backoff_s == 0u
                                ? RTSM_DATA_BACKOFF_FIRST_S
                                : min_u32(2u * r->data_backoff_s, RTSM_DATA_BACKOFF_MAX_S);
        r->data_next_ok = now + r->data_backoff_s;
    }
    if (r->tx_fail_count < 255u) {
        r->tx_fail_count++;
    }
    r->backoff_s = r->backoff_s == 0u
                       ? (uint16_t)RTSM_BACKOFF_MIN_S
                       : (uint16_t)min_u32(2u * r->backoff_s, RTSM_BACKOFF_MAX_S);
    r->next_tx_ok = now + r->backoff_s;
    return false;
}

/* Send queued messages in FIFO order; stop at the first failure (the link
 * is assumed down until the next retry). */
static bool flush_queue(rtsm_t *r, mq_t *q, uint32_t now, bool data_class)
{
    const mq_msg_t *m;
    while ((m = mq_peek(q)) != NULL) {
        if (!radio_tx(r, m->buf, m->len, now, data_class)) {
            return false;
        }
        mq_pop(q);
    }
    return true;
}

/* Alarm queue first (always attempted), then the status/install queue
 * (subject to the link gate). Returns false if anything is left behind. */
static bool flush_priority(rtsm_t *r, uint32_t now)
{
    bool lng = status_long_backoff(r);
    if (!flush_queue(r, &r->q_alarm, now, false)) {
        return false;
    }
    if (mq_count(&r->q_status) == 0u) {
        return true;
    }
    if (!data_gate(r, now, lng)) {
        return false;
    }
    return flush_queue(r, &r->q_status, now, lng);
}

/* ======================================================================== */
/* Message builders (alarm / status / install are queued with their seq)    */
/* ======================================================================== */

static void raise_alarm(rtsm_t *r, uint8_t code, uint32_t now, const pl_record_t *rec)
{
    uint8_t buf[PL_ALARM_SIZE];
    size_t n = pl_encode_alarm(buf, sizeof buf, next_seq(r), to_unix(r, now),
                               code, rec);
    (void)mq_push(&r->q_alarm, buf, (uint8_t)n);
    r->tx_kick = true; /* T1-05: alarms go out immediately */
}

static void queue_status(rtsm_t *r, uint32_t now)
{
    uint8_t buf[PL_STATUS_SIZE];
    pl_status_t s;
    size_t n;
    s.seq = next_seq(r);
    s.time = to_unix(r, now);
    s.battery_pct = r->hal->battery_pct(r->hal->ctx);
    s.energy_mAh = (uint16_t)min_u32(rtsm_energy_total(r), 0xFFFFu);
    s.fw_version = r->id.fw_version;
    s.reset_count = (uint8_t)r->nv.d.reset_count; /* low 8 bits on air */
    s.tx_fail_count = r->tx_fail_count;
    s.link_quality = r->hal->link_quality ? r->hal->link_quality(r->hal->ctx) : 0u;
    r->tx_fail_count = 0; /* counts failures between two status messages */
    n = pl_encode_status(buf, sizeof buf, &s);
    (void)mq_push(&r->q_status, buf, (uint8_t)n);
    r->tx_kick = true;
}

static void queue_install(rtsm_t *r, uint32_t now)
{
    uint8_t buf[PL_INSTALL_SIZE];
    pl_install_t s;
    size_t n;
    s.seq = next_seq(r);
    s.time = to_unix(r, now);
    s.hw_rev = r->id.hw_rev;
    s.fw_version = r->id.fw_version;
    s.attached = r->hal->hall_attached(r->hal->ctx) ? 1u : 0u;
    s.battery_pct = r->hal->battery_pct(r->hal->ctx);
    n = pl_encode_install(buf, sizeof buf, &s);
    (void)mq_push(&r->q_status, buf, (uint8_t)n);
    r->tx_kick = true;
}

/* Latest sample with live attachment/lid flags, used for tamper alarms. */
static pl_record_t current_record(const rtsm_t *r, bool attached, bool lid_closed)
{
    pl_record_t rec = r->last_rec;
    rec.flags = pl_make_flags(attached, lid_closed, r->op_state == RTSM_HOT, r->level,
                              r->low_batt, (r->last_rec.flags & PL_FLAG_FAULT) != 0u);
    return rec;
}

/* ======================================================================== */
/* State entries                                                            */
/* ======================================================================== */

static void enter_ship(rtsm_t *r, bool after_service)
{
    r->state = RTSM_SHIP;
    r->op_state = RTSM_SHIP;
    /* After a service command the unit is usually still on the rail; it
     * must be seen detached once before it can re-commission itself. */
    r->ship_armed = !after_service;
    r->attach_pending = false;
    nv_persist(r, false);
}

static void enter_commission(rtsm_t *r, uint32_t now)
{
    r->state = RTSM_COMMISSION;
    r->op_state = RTSM_COMMISSION;
    r->have_config = false; /* T1-20: a site cannot go live without config */
    r->level = 0;
    r->cadence = RTSM_CAD_NORMAL;
    r->hot_clear_pending = false;
    r->anchor = now; /* 10-min grid starts at commissioning */
    r->next_sample = now;
    r->next_status = now + RTSM_HEARTBEAT_S;
    r->next_install = now + RTSM_INSTALL_RESEND_S;
    r->pending_from = r->log.next;
    r->attach_pending = false;
    r->sent_move = false;
    r->sent_detach = false;
    r->lid_was_closed = r->hal->lid_closed(r->hal->ctx);
    (void)r->hal->imu_movement(r->hal->ctx); /* discard handling/transport events */
    nv_persist(r, false);                     /* e.g. LOCKED -> unlocked */
    queue_install(r, now);
}

/* Boot into a commissioned unit (NV class RUN with a valid config): a new
 * 10-min grid starts now, the pre-reset log is back-filled, and a status
 * message tells the platform about the reset (reset_count). Level, HOT and
 * cadence are re-derived from the next samples. */
static void resume_run(rtsm_t *r, uint32_t now)
{
    r->state = RTSM_RUN;
    r->op_state = RTSM_RUN;
    r->anchor = now;
    r->next_sample = now;
    r->next_status = now + RTSM_HEARTBEAT_S;
    r->next_install = now + RTSM_INSTALL_RESEND_S;
    r->pending_from = r->log.next;
    r->lid_was_closed = r->hal->lid_closed(r->hal->ctx);
    queue_status(r, now);
}

static void enter_locked(rtsm_t *r, uint32_t now)
{
    r->state = RTSM_LOCKED;
    r->next_beacon = now + RTSM_LOCKED_BEACON_S;
    r->tx_kick = true;
    nv_persist(r, false); /* before any TX: a battery pull cannot unlock */
}

/* ======================================================================== */
/* Alarm level, HOT and cadence evaluation                                  */
/* ======================================================================== */

/* Level 0..3; rises immediately, falls only below L(k) - hysteresis. */
static uint8_t eval_level(const rtsm_t *r, int16_t rail)
{
    const int16_t th[3] = { r->cfg.l1, r->cfg.l2, r->cfg.l3 };
    uint8_t raw = 0;
    uint8_t lvl;
    uint8_t i;
    for (i = 0; i < 3u; i++) {
        if (rail >= th[i]) {
            raw = (uint8_t)(i + 1u);
        }
    }
    if (raw >= r->level) {
        return raw;
    }
    lvl = r->level;
    while (lvl > raw && (int32_t)rail < (int32_t)th[lvl - 1u] - (int32_t)r->cfg.hysteresis) {
        lvl--;
    }
    return lvl;
}

/* HOT entry/exit. `t_unix` is the timestamp the new record will get; the
 * record one hour earlier is RTSM_SAMPLE_PERIOD_S-grid aligned in the log. */
static void eval_hot(rtsm_t *r, int16_t rail, uint32_t t_unix, uint32_t now)
{
    const uint32_t back = HOUR_S / RTSM_SAMPLE_PERIOD_S;
    bool cond = rail >= r->cfg.hot_threshold;
    rtsm_log_entry_t e;

    if (!cond && r->cfg.rate_of_rise > 0u && r->log.next >= back &&
        rlog_get(&r->log, r->log.next - back, &e) && e.time == t_unix - HOUR_S &&
        e.rail_cC != PL_TEMP_NA) {
        cond = ((int32_t)rail - (int32_t)e.rail_cC) >= (int32_t)r->cfg.rate_of_rise;
    }

    if (cond) {
        r->hot_clear_pending = false;
        if (r->op_state == RTSM_RUN) {
            set_op_state(r, RTSM_HOT);
        }
    } else if (r->op_state == RTSM_HOT) {
        if (!r->hot_clear_pending) {
            r->hot_clear_pending = true;
            r->hot_clear_since = now;
        } else if (now - r->hot_clear_since >= RTSM_HOT_HOLD_S) {
            r->hot_clear_pending = false;
            set_op_state(r, RTSM_RUN);
        }
    }
}

/* D-11 LTE-M cadence: HOT > COOL (rail < cool_threshold) > NORMAL. */
static void eval_cadence(rtsm_t *r, int16_t rail)
{
    if (r->op_state == RTSM_HOT) {
        r->cadence = RTSM_CAD_HOT;
    } else if (rail < r->cfg.cool_threshold) {
        r->cadence = RTSM_CAD_COOL;
    } else {
        r->cadence = RTSM_CAD_NORMAL;
    }
}

static uint32_t cadence_period_s(const rtsm_t *r)
{
    switch (r->cadence) {
    case RTSM_CAD_COOL: return 60u * r->cfg.cool_batch_min;
    case RTSM_CAD_HOT: return 60u * r->cfg.hot_batch_min;
    default: return 60u * r->cfg.batch_period_min;
    }
}

/* ======================================================================== */
/* Sampling                                                                 */
/* ======================================================================== */

/* Take one sample at grid time `grid`, evaluate alarms, append to the log.
 * Returns false only if the log write failed. */
static bool do_sample(rtsm_t *r, uint32_t now, uint32_t grid, uint32_t *abs_out)
{
    const rtsm_hal_t *h = r->hal;
    int16_t raw = h->read_rail_cC(h->ctx);
    int16_t air = h->read_air_cC(h->ctx);
    bool attached = h->hall_attached(h->ctx);
    bool lid = h->lid_closed(h->ctx);
    uint8_t batt = h->battery_pct(h->ctx);
    uint32_t t_unix = to_unix(r, grid);
    bool fault = raw == PL_TEMP_NA || air == PL_TEMP_NA;
    uint8_t codes[4];
    uint8_t ncodes = 0;
    uint8_t i;
    int16_t rail;
    pl_record_t rec;
    rtsm_log_entry_t e;
    bool ok;

    /* D-13: compensated rail value is used for everything downstream. */
    rail = r->have_prev_raw ? rtsm_rail_compensate(raw, r->prev_raw_rail, now - r->prev_raw_time)
                            : raw;
    r->have_prev_raw = true;
    r->prev_raw_rail = raw;
    r->prev_raw_time = now;

    /* Sensor fault: one alarm per episode (episode ends when both read OK). */
    if (fault) {
        if (!r->fault_active) {
            r->fault_active = true;
            codes[ncodes++] = PL_ALM_SENSOR_FAULT;
        }
    } else {
        r->fault_active = false;
    }

    /* Low battery: alarm once below 10 %, re-armed above 15 %. */
    if (!r->low_batt && batt < RTSM_LOWBAT_ON_PCT) {
        r->low_batt = true;
        codes[ncodes++] = PL_ALM_LOW_BATTERY;
    } else if (r->low_batt && batt > RTSM_LOWBAT_OFF_PCT) {
        r->low_batt = false;
    }

    /* Level, HOT and cadence need site config and a valid rail value. */
    if (r->have_config && rail != PL_TEMP_NA) {
        uint8_t lvl = eval_level(r, rail);
        if (lvl != r->level) {
            codes[ncodes++] = lvl > r->level ? PL_ALM_LEVEL_UP : PL_ALM_LEVEL_DOWN;
            r->level = lvl;
        }
        eval_hot(r, rail, t_unix, now);
        eval_cadence(r, rail);
    }

    rec.rail_cC = rail;
    rec.air_cC = air;
    rec.flags = pl_make_flags(attached, lid, r->op_state == RTSM_HOT, r->level, r->low_batt,
                              fault);
    r->last_rec = rec;

    e.time = t_unix;
    e.rail_cC = rail;
    e.air_cC = air;
    e.flags = rec.flags;
    e.status = RLOG_ST_UNSENT;
    ok = rlog_append(&r->log, &e, abs_out);

    for (i = 0; i < ncodes; i++) {
        raise_alarm(r, codes[i], now, &rec);
    }
    return ok;
}

/* ======================================================================== */
/* Data batches from the log                                                */
/* ======================================================================== */

/* Send records [first, first+n) as one batch; mark them sent only on
 * success. A failed attempt gives its seq number back so that the platform
 * does not see a gap. */
static bool send_batch(rtsm_t *r, uint32_t now, uint32_t first, uint8_t n)
{
    pl_record_t recs[RTSM_LTEM_MAX_N];
    rtsm_log_entry_t e;
    uint32_t base = 0;
    uint16_t seq_before = r->seq;
    size_t len;
    uint8_t i;

    for (i = 0; i < n; i++) {
        if (!rlog_get(&r->log, first + i, &e)) {
            return false;
        }
        if (i == 0u) {
            base = e.time;
        }
        recs[i].rail_cC = e.rail_cC;
        recs[i].air_cC = e.air_cC;
        recs[i].flags = e.flags;
    }
    len = pl_encode_batch(r->txbuf, sizeof r->txbuf, next_seq(r), base,
                          RTSM_INTERVAL_MIN, recs, n);
    if (len == 0u || !radio_tx(r, r->txbuf, len, now, true)) {
        r->seq = seq_before;
        r->data_link_lost = true;
        return false;
    }
    r->data_link_lost = false;
    for (i = 0; i < n; i++) {
        (void)rlog_mark_sent(&r->log, first + i);
    }
    return true;
}

/* True if abs `b` directly follows `a` on the sample grid. */
static bool contiguous(const rtsm_log_entry_t *a, const rtsm_log_entry_t *b)
{
    return b->time == a->time + RTSM_SAMPLE_PERIOD_S;
}

/* The "newest data" batch for this tick. LoRa: [previous, current] (the
 * previous record is a deliberate repeat, T1-04). LTE-M: every unsent,
 * contiguous record since the last batch boundary. */
static bool send_live(rtsm_t *r, uint32_t now, uint32_t cur, uint32_t *first_out)
{
    rtsm_log_entry_t a, b;
    uint32_t first = cur;

    if (r->id.sku == RTSM_SKU_LORA) {
        if (cur > r->log.first && rlog_get(&r->log, cur - 1u, &a) &&
            rlog_get(&r->log, cur, &b) && contiguous(&a, &b)) {
            first = cur - 1u;
        }
    } else {
        while (first > r->pending_from && first > r->log.first &&
               cur - first + 1u < RTSM_LTEM_MAX_N && rlog_get(&r->log, first - 1u, &a) &&
               a.status == RLOG_ST_UNSENT && rlog_get(&r->log, first, &b) && contiguous(&a, &b)) {
            first--;
        }
    }
    *first_out = first;
    return send_batch(r, now, first, (uint8_t)(cur - first + 1u));
}

/* Back-fill: oldest unsent first, contiguous runs, at most
 * RTSM_BACKLOG_MAX_PER_TICK messages per tick, only records below `end`. */
static void send_backlog(rtsm_t *r, uint32_t now, uint32_t end)
{
    uint8_t max_n = r->id.sku == RTSM_SKU_LORA ? (uint8_t)RTSM_LORA_MAX_N
                                                : (uint8_t)RTSM_LTEM_MAX_N;
    while (r->backlog_budget > 0u) {
        rtsm_log_entry_t prev, e;
        uint32_t a = rlog_first_unsent(&r->log);
        uint8_t n = 1;
        if (a >= end || !rlog_get(&r->log, a, &prev)) {
            return;
        }
        while (n < max_n && a + n < end && rlog_get(&r->log, a + n, &e) &&
               e.status == RLOG_ST_UNSENT && contiguous(&prev, &e)) {
            prev = e;
            n++;
        }
        if (!send_batch(r, now, a, n)) {
            return;
        }
        r->backlog_budget--;
    }
}

/* Everything sent on a 10-min tick, in priority order:
 * alarms > status/install > newest data > backlog (oldest first). */
static void tick_tx(rtsm_t *r, uint32_t now, uint32_t cur, uint32_t grid)
{
    uint32_t backlog_end = r->pending_from;
    bool boundary = true;

    if (!flush_priority(r, now)) {
        return;
    }
    /* No data before site config (COMMISSION records are back-filled). */
    if (!r->have_config || (r->op_state != RTSM_RUN && r->op_state != RTSM_HOT)) {
        return;
    }
    /* Link known down, or long back-off running: no data this tick. The
     * next tick that may send is treated as a batch boundary (newest
     * first), like after a failed attempt. */
    if (!data_gate(r, now, true)) {
        r->data_link_lost = true;
        return;
    }
    if (r->id.sku == RTSM_SKU_LTEM) {
        /* Batch boundary of the current cadence; after a failed data
         * attempt every tick is treated as a boundary so that, on
         * reconnection, the newest data goes before the back-fill. */
        boundary = ((grid - r->anchor) % cadence_period_s(r)) == 0u || r->data_link_lost;
    }
    if (boundary) {
        uint32_t live_first = cur;
        bool ok = send_live(r, now, cur, &live_first);
        r->pending_from = cur + 1u; /* everything up to now is due */
        if (!ok) {
            return;
        }
        backlog_end = live_first;
    }
    send_backlog(r, now, backlog_end);
}

/* ======================================================================== */
/* Per-state step functions                                                 */
/* ======================================================================== */

/* Returns true when the unit has just entered COMMISSION. */
static bool ship_step(rtsm_t *r, uint32_t now)
{
    bool att = r->hal->hall_attached(r->hal->ctx);
    if (!r->ship_armed) {
        if (!att) {
            r->ship_armed = true;
        }
        return false;
    }
    if (!att) {
        r->attach_pending = false;
        return false;
    }
    if (!r->attach_pending) {
        r->attach_pending = true;
        r->attach_since = now;
    }
    if (now - r->attach_since >= RTSM_ATTACH_DEBOUNCE_S) {
        enter_commission(r, now);
        return true;
    }
    return false;
}

static void check_tamper(rtsm_t *r, uint32_t now)
{
    const rtsm_hal_t *h = r->hal;
    bool lid = h->lid_closed(h->ctx);
    bool att = h->hall_attached(h->ctx);
    bool mv = h->imu_movement(h->ctx);
    bool lid_opened = r->lid_was_closed && !lid;
    pl_record_t rec;

    r->lid_was_closed = lid;
    rec = current_record(r, att, lid);

    /* Lid opened while operating (service mode would have put us in SHIP). */
    if (lid_opened) {
        raise_alarm(r, PL_ALM_LID_OPENED, now, &rec);
        enter_locked(r, now);
        return;
    }

    if (r->state != RTSM_ALARM_TAMPER) {
        if (!mv && att) {
            return;
        }
        r->state = RTSM_ALARM_TAMPER; /* op_state is kept underneath */
        r->tamper_deadline = now + RTSM_TAMPER_WINDOW_S;
        r->sent_move = false;
        r->sent_detach = false;
        r->settle_ok = false;
    }

    /* Inside a tamper episode (possibly the step that started it). */
    if (mv) {
        if (!r->sent_move) {
            r->sent_move = true;
            raise_alarm(r, PL_ALM_MOVEMENT, now, &rec);
        }
        r->settle_ok = false; /* restart the quiet period */
    }
    if (!att) {
        if (!r->sent_detach) {
            r->sent_detach = true;
            raise_alarm(r, PL_ALM_DETACHED, now, &rec);
        }
        r->settle_ok = false;
    } else if (!r->settle_ok) {
        r->settle_ok = true;
        r->settle_since = now;
    }

    if (r->settle_ok && now - r->settle_since >= RTSM_TAMPER_SETTLE_S) {
        r->state = r->op_state; /* reattached in time: resume */
        return;
    }
    if (now >= r->tamper_deadline) {
        raise_alarm(r, PL_ALM_LOCKED, now, &rec);
        enter_locked(r, now);
    }
}

static void operating_step(rtsm_t *r, uint32_t now)
{
    bool tick = now >= r->next_sample;
    bool have_cur = false;
    uint32_t grid = 0;
    uint32_t cur = 0;

    if (tick) {
        /* Latest grid point <= now (missed points are not back-dated). */
        grid = r->anchor + ((now - r->anchor) / RTSM_SAMPLE_PERIOD_S) * RTSM_SAMPLE_PERIOD_S;
        r->next_sample = grid + RTSM_SAMPLE_PERIOD_S;
        r->backlog_budget = (uint8_t)RTSM_BACKLOG_MAX_PER_TICK;
        have_cur = do_sample(r, now, grid, &cur);
    }
    if (now >= r->next_status) {
        queue_status(r, now);
        r->next_status += RTSM_HEARTBEAT_S * ((now - r->next_status) / RTSM_HEARTBEAT_S + 1u);
    }
    if (r->op_state == RTSM_COMMISSION && now >= r->next_install) {
        if (mq_count(&r->q_status) == 0u) { /* previous one delivered */
            queue_install(r, now);
        }
        r->next_install = now + RTSM_INSTALL_RESEND_S;
    }

    if (tick) {
        nv_persist(r, true); /* energy step check once per sample */
    }
    if (tick && have_cur) {
        tick_tx(r, now, cur, grid);
    } else if (tick || r->tx_kick || (queues_pending(r) && now >= r->next_tx_ok)) {
        (void)flush_priority(r, now);
    }
    r->tx_kick = false;
}

static void locked_step(rtsm_t *r, uint32_t now)
{
    if (now >= r->next_beacon) {
        queue_status(r, now); /* "stolen" beacon */
        nv_persist(r, true);
        r->next_beacon += RTSM_LOCKED_BEACON_S *
                          ((now - r->next_beacon) / RTSM_LOCKED_BEACON_S + 1u);
        (void)flush_priority(r, now);
    } else if (r->tx_kick || (queues_pending(r) && now >= r->next_tx_ok)) {
        (void)flush_priority(r, now);
    }
    r->tx_kick = false;
}

/* ======================================================================== */
/* Public API                                                               */
/* ======================================================================== */

void rtsm_init(rtsm_t *r, const rtsm_hal_t *hal, const rtsm_init_t *init)
{
    nvs_data_t d;
    pl_downlink_t dl;
    uint32_t now;

    memset(r, 0, sizeof *r);
    r->hal = hal;
    r->id = *init;
    r->state = RTSM_SHIP;
    r->op_state = RTSM_SHIP;
    r->ship_armed = true;
    r->cadence = RTSM_CAD_NORMAL;
    r->last_rec.rail_cC = PL_TEMP_NA;
    r->last_rec.air_cC = PL_TEMP_NA;
    /* Placeholders only; RUN is impossible until a valid 0x81 arrives. */
    r->cfg.batch_period_min = 60u;
    r->cfg.cool_batch_min = 240u;
    r->cfg.hot_batch_min = 20u;
    r->cfg.hysteresis = 100u;
    rlog_init(&r->log, hal, (uint16_t)RLOG_CAPACITY);
    mq_init(&r->q_alarm, r->alarm_store, (uint8_t)RTSM_ALARM_QUEUE_LEN);
    mq_init(&r->q_status, r->status_store, (uint8_t)RTSM_STATUS_QUEUE_LEN);

    /* Persistent state (R-9). Resume seq at the reservation, so no seq
     * used before the reset can be reused (gaps are possible). */
    nvs_load(&r->nv, hal, init->unlock_token);
    r->energy_base = r->nv.d.energy_mAh;
    r->seq = r->nv.d.seq_reserved;
    if (r->nv.d.have_config &&
        pl_decode_downlink(r->nv.d.config, PL_CONFIG_SIZE, &dl) == PL_DL_OK &&
        dl.type == PL_T_CONFIG) {
        r->cfg = dl.u.config;
        r->have_config = true;
    }

    /* Boot record: reset_count + 1 and a fresh seq block, one write. */
    d = r->nv.d;
    d.reset_count++;
    d.seq_reserved = (uint16_t)(r->seq + RTSM_NV_SEQ_BLOCK);
    d.have_config = r->have_config;
    if (!r->have_config) {
        memset(d.config, 0, sizeof d.config);
        if (d.state_class == (uint8_t)NVS_CLASS_RUN) {
            d.state_class = (uint8_t)NVS_CLASS_SHIP; /* cannot run without config */
        }
    }
    nv_write_now(r, &d);

    now = hal_now(r);
    switch (d.state_class) {
    case NVS_CLASS_RUN:
        resume_run(r, now);
        break;
    case NVS_CLASS_LOCKED:
        r->state = RTSM_LOCKED;
        r->op_state = RTSM_LOCKED;
        r->next_beacon = now + RTSM_LOCKED_BEACON_S;
        queue_status(r, now); /* powered up while locked: beacon at once */
        break;
    case NVS_CLASS_SHIP_SERVICE:
        r->ship_armed = false;
        break;
    default:
        break; /* SHIP, armed */
    }
}

void rtsm_set_unlock_token(rtsm_t *r, const uint8_t token[PL_TOKEN_LEN])
{
    nvs_data_t d;
    nv_want(r, &d);
    memcpy(d.token, token, PL_TOKEN_LEN);
    if (nvs_differs(&r->nv, &d)) {
        nv_write_now(r, &d);
    }
}

void rtsm_step(rtsm_t *r)
{
    uint32_t now = hal_now(r);

    if (r->state == RTSM_SHIP && !ship_step(r, now)) {
        nv_persist(r, false); /* service SHIP re-armed by a detach */
        return; /* SHIP: only the Hall sensor is looked at */
    }
    if (r->state != RTSM_LOCKED) {
        check_tamper(r, now);
    }
    if (r->state == RTSM_LOCKED) {
        locked_step(r, now);
    } else {
        operating_step(r, now);
    }
    nv_persist(r, false); /* catches any remaining class change */
}

static bool token_equal(const uint8_t *a, const uint8_t *b)
{
    uint8_t diff = 0;
    uint8_t i;
    for (i = 0; i < PL_TOKEN_LEN; i++) {
        diff |= (uint8_t)(a[i] ^ b[i]); /* constant time */
    }
    return diff == 0u;
}

rtsm_dl_result_t rtsm_on_downlink(rtsm_t *r, const uint8_t *buf, size_t len)
{
    pl_downlink_t dl;
    uint32_t now = hal_now(r);

    if (pl_decode_downlink(buf, len, &dl) != PL_DL_OK) {
        return RTSM_DL_REJECTED;
    }
    /* LOCKED ignores everything except an authorised unlock. */
    if (r->state == RTSM_LOCKED && dl.type != PL_T_UNLOCK) {
        return RTSM_DL_IGNORED;
    }

    switch (dl.type) {
    case PL_T_CONFIG:
        if (!is_operating(r->state)) {
            return RTSM_DL_IGNORED;
        }
        r->cfg = dl.u.config;
        r->have_config = true;
        if (r->op_state == RTSM_COMMISSION) {
            set_op_state(r, RTSM_RUN);
            r->cadence = RTSM_CAD_NORMAL;
            r->pending_from = r->log.next; /* COMMISSION records -> backlog */
        }
        nv_persist(r, false); /* written only if config or class changed */
        return RTSM_DL_APPLIED;

    case PL_T_SERVICE:
        if (!is_operating(r->state)) {
            return RTSM_DL_IGNORED;
        }
        if (dl.u.service_mode == PL_SERVICE_SHIP) {
            enter_ship(r, true); /* planned removal: no tamper alarms */
        }
        return RTSM_DL_APPLIED; /* mode 0 while operating: nothing to do */

    case PL_T_UNLOCK:
        if (r->state != RTSM_LOCKED) {
            return RTSM_DL_IGNORED;
        }
        if (!token_equal(dl.u.token, r->nv.d.token)) {
            return RTSM_DL_REJECTED;
        }
        enter_commission(r, now);
        return RTSM_DL_APPLIED;

    case PL_T_TIMESYNC:
        if (!is_operating(r->state)) {
            return RTSM_DL_IGNORED;
        }
        r->time_offset = dl.u.time - now;
        return RTSM_DL_APPLIED;

    default:
        return RTSM_DL_REJECTED;
    }
}

uint32_t rtsm_next_wakeup(const rtsm_t *r)
{
    uint32_t w = 0xFFFFFFFFu;
    switch (r->state) {
    case RTSM_SHIP:
        if (r->attach_pending) {
            w = r->attach_since + RTSM_ATTACH_DEBOUNCE_S;
        }
        return w; /* otherwise Hall interrupt only */
    case RTSM_LOCKED:
        w = r->next_beacon;
        break;
    default:
        w = min_u32(r->next_sample, r->next_status);
        if (r->op_state == RTSM_COMMISSION) {
            w = min_u32(w, r->next_install);
        }
        if (r->state == RTSM_ALARM_TAMPER) {
            w = min_u32(w, r->tamper_deadline);
            if (r->settle_ok) {
                w = min_u32(w, r->settle_since + RTSM_TAMPER_SETTLE_S);
            }
        }
        break;
    }
    if (mq_count(&r->q_alarm) > 0u) {
        w = min_u32(w, r->next_tx_ok);
    } else if (mq_count(&r->q_status) > 0u) {
        uint32_t t = r->next_tx_ok;
        if (status_long_backoff(r) && r->data_next_ok > t) {
            t = r->data_next_ok; /* nothing to do before the long back-off */
        }
        w = min_u32(w, t);
    }
    return w;
}
