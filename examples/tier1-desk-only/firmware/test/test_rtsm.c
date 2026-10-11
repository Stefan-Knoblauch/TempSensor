/*
 * test_rtsm.c - Behavioural tests of the state machine against the
 * simulated HAL. Time is advanced one second per rtsm_step() call.
 */
#include <stdlib.h>
#include <string.h>

#include "rtsm.h"
#include "sim_hal.h"
#include "test.h"

#define T0 0x6955B900u /* 2026-01-01T00:00:00Z */
#define TICK 600u
#define DAY 86400u

static sim_t S;
static rtsm_t R;
static const uint8_t TOKEN[8] = { 0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03, 0x04 };

/* ---- Harness ------------------------------------------------------------ */

static rtsm_init_t g_in;

static void setup(rtsm_sku_t sku)
{
    memset(&g_in, 0, sizeof g_in);
    g_in.sku = sku;
    g_in.hw_rev = 2;
    g_in.fw_version = 0x0102;
    memcpy(g_in.unlock_token, TOKEN, sizeof TOKEN);
    sim_init(&S, T0);
    rtsm_init(&R, &S.hal, &g_in);
}

/* MCU reset (battery pull / watchdog): RAM is lost, the sim's flash (log
 * and nvstate slots), clock and captured messages are kept. */
static void reboot(uint32_t downtime_s)
{
    S.now += downtime_s;
    rtsm_init(&R, &S.hal, &g_in);
}

static void advance(uint32_t secs)
{
    while (secs--) {
        S.now++;
        rtsm_step(&R);
    }
}

static void tick(void) { advance(TICK); }

static pl_config_t std_cfg(void)
{
    pl_config_t c;
    c.l1 = 4000;
    c.l2 = 4500;
    c.l3 = 5000;
    c.hot_threshold = 3500;
    c.rate_of_rise = 800;
    c.hysteresis = 100;
    c.batch_period_min = 60;
    c.cool_threshold = 2500;
    c.cool_batch_min = 240;
    c.hot_batch_min = 20;
    return c;
}

static rtsm_dl_result_t send_cfg(const pl_config_t *c)
{
    uint8_t b[PL_CONFIG_SIZE];
    (void)pl_encode_config(b, sizeof b, c);
    return rtsm_on_downlink(&R, b, sizeof b);
}

static rtsm_dl_result_t send_unlock(const uint8_t *tok)
{
    uint8_t b[9];
    b[0] = PL_T_UNLOCK;
    memcpy(b + 1, tok, 8);
    return rtsm_on_downlink(&R, b, sizeof b);
}

static rtsm_dl_result_t send_service(uint8_t mode)
{
    uint8_t b[2];
    b[0] = PL_T_SERVICE;
    b[1] = mode;
    return rtsm_on_downlink(&R, b, sizeof b);
}

/* Attach at the current time; 60 s later the unit is in COMMISSION and
 * has taken its first sample (S.now == R.anchor). */
static void install_unit(void)
{
    S.hall = true;
    rtsm_step(&R);
    advance(RTSM_ATTACH_DEBOUNCE_S);
}

static void commission_and_run(void)
{
    pl_config_t c = std_cfg();
    install_unit();
    CHECK_EQ(R.state, RTSM_COMMISSION);
    CHECK_EQ(send_cfg(&c), RTSM_DL_APPLIED);
    CHECK_EQ(R.state, RTSM_RUN);
}

/* Choose the raw sensor value so that the lag-compensated (reported) value
 * of the next sample is as close as possible to `v` (D-13). */
static int16_t raw_for(int16_t v)
{
    int32_t p = S.rail;
    int32_t est = p + ((int32_t)v - p) * 4 / 5;
    int32_t best = est;
    int32_t bestd = 1000000;
    int32_t x;
    for (x = est - 6; x <= est + 6; x++) {
        int32_t d = abs((int)rtsm_rail_compensate((int16_t)x, (int16_t)p, TICK) - v);
        if (d < bestd) {
            bestd = d;
            best = x;
        }
    }
    return (int16_t)best;
}

/* One tick whose reported rail value is ~v. */
static void feed(int16_t v)
{
    S.rail = raw_for(v);
    tick();
}

/* ---- Message inspection -------------------------------------------------- */

static uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static int16_t bei16(const uint8_t *p)
{
    uint16_t v = be16(p);
    return v < 0x8000u ? (int16_t)v : (int16_t)((int32_t)v - 65536);
}

static const sim_msg_t *msg(uint32_t i) { return &S.msgs[i]; }
static uint8_t mtype(uint32_t i) { return S.msgs[i].buf[0]; }
static uint8_t batch_n(uint32_t i) { return S.msgs[i].buf[8]; }
static uint32_t batch_base(uint32_t i) { return be32(S.msgs[i].buf + 3); }
static uint8_t alarm_code(uint32_t i) { return S.msgs[i].buf[7]; }
static uint8_t alarm_level(uint32_t i) { return (uint8_t)((S.msgs[i].buf[12] >> 3) & 3u); }

static uint32_t count_type_from(uint32_t from, uint8_t type)
{
    uint32_t i, n = 0;
    for (i = from; i < S.nmsg; i++) {
        if (mtype(i) == type) n++;
    }
    return n;
}

static int32_t last_index_of(uint8_t type)
{
    int32_t i;
    for (i = (int32_t)S.nmsg - 1; i >= 0; i--) {
        if (mtype((uint32_t)i) == type) return i;
    }
    return -1;
}

static int32_t find_alarm(uint32_t from, uint8_t code)
{
    uint32_t i;
    for (i = from; i < S.nmsg; i++) {
        if (mtype(i) == PL_T_ALARM && alarm_code(i) == code) return (int32_t)i;
    }
    return -1;
}

static bool last_log(rtsm_log_entry_t *e)
{
    return rlog_get(&R.log, R.log.next - 1u, e);
}

/* ======================================================================== */
/* Tests                                                                    */
/* ======================================================================== */

static void t_ship_to_commission(void)
{
    setup(RTSM_SKU_LORA);
    advance(3600); /* on the shelf: nothing happens */
    CHECK_EQ(R.state, RTSM_SHIP);
    CHECK_EQ(S.tx_attempts, 0);

    /* Attached 30 s, a 1 s glitch, then 59 s: still SHIP. */
    S.hall = true;
    advance(30);
    S.hall = false;
    advance(1);
    S.hall = true;
    rtsm_step(&R); /* re-attached at this instant */
    advance(59);
    CHECK_EQ(R.state, RTSM_SHIP);
    CHECK_EQ(rlog_count(&R.log), 0);
    advance(1); /* 60 s continuously attached */
    CHECK_EQ(R.state, RTSM_COMMISSION);
    CHECK_EQ(R.anchor, S.now);
    CHECK_EQ(rlog_count(&R.log), 1); /* first sample at entry */
    CHECK_EQ(S.nmsg, 1);
    CHECK_EQ(mtype(0), PL_T_INSTALL);
    CHECK_EQ(msg(0)->len, PL_INSTALL_SIZE);
    CHECK_EQ(msg(0)->buf[7], 2);        /* hw_rev */
    CHECK_EQ(be16(msg(0)->buf + 8), 0x0102);
    CHECK_EQ(msg(0)->buf[10], 1);       /* attached */
    CHECK_EQ(msg(0)->buf[11], 90);      /* battery */
}

static void t_no_run_without_config(void)
{
    pl_config_t bad = std_cfg();
    pl_config_t good = std_cfg();
    uint32_t i;
    setup(RTSM_SKU_LORA);
    CHECK_EQ(send_cfg(&good), RTSM_DL_IGNORED); /* SHIP ignores config */
    install_unit();
    for (i = 0; i < 12; i++) tick();
    CHECK_EQ(R.state, RTSM_COMMISSION);
    CHECK_EQ(count_type_from(0, PL_T_BATCH), 0); /* no data before config */
    bad.l2 = bad.l1;
    CHECK_EQ(send_cfg(&bad), RTSM_DL_REJECTED);
    bad = std_cfg();
    bad.hysteresis = 600;
    CHECK_EQ(send_cfg(&bad), RTSM_DL_REJECTED);
    tick();
    CHECK_EQ(R.state, RTSM_COMMISSION);
    CHECK_EQ(send_cfg(&good), RTSM_DL_APPLIED);
    CHECK_EQ(R.state, RTSM_RUN);
}

static void t_logging_during_commission(void)
{
    pl_config_t c = std_cfg();
    rtsm_log_entry_t e;
    uint32_t i, before;
    int32_t b;
    setup(RTSM_SKU_LTEM);
    install_unit();
    for (i = 0; i < 6; i++) tick();
    CHECK_EQ(rlog_count(&R.log), 7);
    CHECK_EQ(rlog_count_unsent(&R.log), 7);
    CHECK(rlog_get(&R.log, 0, &e));
    CHECK(e.time == R.anchor);
    CHECK(rlog_get(&R.log, 6, &e));
    CHECK(e.time == R.anchor + 6u * TICK);
    CHECK_EQ(count_type_from(0, PL_T_BATCH), 0);

    CHECK_EQ(send_cfg(&c), RTSM_DL_APPLIED);
    before = S.nmsg;
    tick(); /* COMMISSION records go out as back-fill */
    CHECK_EQ(count_type_from(before, PL_T_BATCH), 1);
    b = last_index_of(PL_T_BATCH);
    CHECK(b >= 0);
    CHECK_EQ(batch_n((uint32_t)b), 7);
    CHECK(batch_base((uint32_t)b) == R.anchor);
    CHECK_EQ(msg((uint32_t)b)->buf[7], 10); /* interval_min */
    CHECK_EQ(rlog_count_unsent(&R.log), 1);  /* the new sample waits for the hour */
}

static void t_install_backoff(void)
{
    uint32_t a;
    setup(RTSM_SKU_LORA);
    S.link_up = false;
    install_unit();
    a = R.anchor;
    advance(TICK + 10);
    CHECK(S.tx_attempts >= 5);
    CHECK_EQ(S.attempt_t[0] - a, 0);
    CHECK_EQ(S.attempt_t[1] - a, 60);
    CHECK_EQ(S.attempt_t[2] - a, 180);
    CHECK_EQ(S.attempt_t[3] - a, 420);
    CHECK_EQ(S.attempt_t[4] - a, 600);
    CHECK_EQ(S.nmsg, 0);
    S.link_up = true;
    advance(TICK);
    CHECK_EQ(S.nmsg, 1);
    CHECK_EQ(mtype(0), PL_T_INSTALL);
    CHECK_EQ(be16(msg(0)->buf + 1), 0); /* same seq as the first attempt */
}

static void t_lora_cadence(void)
{
    uint32_t i, before;
    int32_t b;
    setup(RTSM_SKU_LORA);
    commission_and_run();
    for (i = 0; i < 12; i++) {
        before = S.nmsg;
        tick();
        CHECK_EQ(S.nmsg - before, 1); /* exactly one batch per 10 min */
        b = last_index_of(PL_T_BATCH);
        CHECK_EQ(b, (int32_t)before);
        CHECK_EQ(batch_n((uint32_t)b), 2);
        CHECK_EQ(msg((uint32_t)b)->len, 19);
        CHECK(batch_base((uint32_t)b) == S.now - TICK); /* previous + current */
        CHECK(msg((uint32_t)b)->t == S.now);
    }
    CHECK_EQ(rlog_count_unsent(&R.log), 0);
}

static void t_ltem_hourly(void)
{
    uint32_t i, before, nb = 0;
    setup(RTSM_SKU_LTEM);
    commission_and_run();
    tick(); /* first tick back-fills the COMMISSION sample (n=1) */
    before = S.nmsg;
    for (i = 0; i < 6 * 5 - 1; i++) tick(); /* up to anchor + 5 h */
    for (i = before; i < S.nmsg; i++) {
        if (mtype(i) != PL_T_BATCH) continue;
        nb++;
        CHECK_EQ((msg(i)->t - R.anchor) % 3600u, 0);
        CHECK_EQ(batch_n(i), 6);
        CHECK(batch_base(i) == msg(i)->t - 5u * TICK);
    }
    CHECK_EQ(nb, 5);
}

static void t_ltem_cool_mode(void)
{
    uint32_t i, before, nb = 0, t;
    int32_t a;
    setup(RTSM_SKU_LTEM);
    S.rail = 2000; /* below cool_threshold 2500 */
    commission_and_run();
    tick();
    CHECK_EQ(R.cadence, RTSM_CAD_COOL);
    before = S.nmsg;
    for (i = 0; i < 6 * 12 - 1; i++) tick(); /* to anchor + 12 h */
    for (i = before; i < S.nmsg; i++) {
        if (mtype(i) != PL_T_BATCH) continue;
        nb++;
        CHECK_EQ((msg(i)->t - R.anchor) % (4u * 3600u), 0); /* every 4 h */
        CHECK_EQ(batch_n(i), 24);
    }
    CHECK_EQ(nb, 3);

    /* Alarms stay immediate in COOL: movement mid-interval. */
    advance(250);
    t = S.now + 1u;
    S.imu_event = true;
    advance(1);
    a = find_alarm(before, PL_ALM_MOVEMENT);
    CHECK(a >= 0);
    if (a >= 0) CHECK(msg((uint32_t)a)->t == t);
    CHECK_EQ(R.state, RTSM_ALARM_TAMPER);
    advance(RTSM_TAMPER_SETTLE_S);
    CHECK_EQ(R.state, RTSM_RUN);

    /* Low-battery alarm in COOL also goes out at the sample, not 4 h later. */
    S.batt = 5;
    advance(TICK);
    a = find_alarm(before, PL_ALM_LOW_BATTERY);
    CHECK(a >= 0);
    if (a >= 0) CHECK((msg((uint32_t)a)->t - R.anchor) % TICK == 0u);
}

/* NORMAL -> HOT -> NORMAL -> COOL, checking batch times and sizes. */
static void t_ltem_cadence_transitions(void)
{
    static const uint32_t exp_t[] = { 3600, 7200, 8400, 9600, 10800, 12000, 13200,
                                      14400, 18000, 28800, 43200 };
    static const uint8_t exp_n[] = { 6, 6, 2, 2, 2, 2, 2, 2, 6, 18, 24 };
    uint32_t i, k = 0, before;
    setup(RTSM_SKU_LTEM);
    commission_and_run(); /* rail 3000: NORMAL */
    tick();
    before = S.nmsg;
    while (S.now - R.anchor < 7200u) tick();
    CHECK_EQ(R.cadence, RTSM_CAD_NORMAL);
    S.rail = 3600; /* HOT from the 7800 sample */
    while (S.now - R.anchor < 10800u) tick();
    CHECK_EQ(R.state, RTSM_HOT);
    CHECK_EQ(R.cadence, RTSM_CAD_HOT);
    S.rail = 3000; /* condition clears at 11400, HOT left at 15000 */
    while (S.now - R.anchor < 14400u) tick();
    CHECK_EQ(R.state, RTSM_HOT);
    tick();
    CHECK_EQ(R.state, RTSM_RUN);
    CHECK_EQ(R.cadence, RTSM_CAD_NORMAL);
    while (S.now - R.anchor < 18000u) tick();
    S.rail = 2000; /* COOL from 18600 */
    tick();
    CHECK_EQ(R.cadence, RTSM_CAD_COOL);
    while (S.now - R.anchor < 43200u) tick();

    for (i = before; i < S.nmsg; i++) {
        if (mtype(i) != PL_T_BATCH) continue;
        if (k < sizeof exp_t / sizeof exp_t[0]) {
            CHECK_EQ(msg(i)->t - R.anchor, exp_t[k]);
            CHECK_EQ(batch_n(i), exp_n[k]);
        }
        k++;
    }
    CHECK_EQ(k, sizeof exp_t / sizeof exp_t[0]);
    CHECK_EQ(rlog_count_unsent(&R.log), 0);
}

static void t_hot_threshold(void)
{
    rtsm_log_entry_t e;
    uint32_t i;
    setup(RTSM_SKU_LORA);
    commission_and_run();
    tick();
    feed(3400);
    CHECK_EQ(R.state, RTSM_RUN);
    feed(3600);
    CHECK_EQ(R.state, RTSM_HOT);
    CHECK(last_log(&e));
    CHECK(e.flags & PL_FLAG_HOT);
    feed(3600);
    feed(3000); /* first clear sample */
    for (i = 0; i < 5; i++) {
        tick();
        CHECK_EQ(R.state, RTSM_HOT); /* +10 .. +50 min */
    }
    tick(); /* +60 min */
    CHECK_EQ(R.state, RTSM_RUN);
    CHECK(last_log(&e));
    CHECK(!(e.flags & PL_FLAG_HOT));

    /* Condition returning during the hold restarts the hour. */
    feed(3600);
    feed(3000);
    tick();
    tick();
    feed(3600);
    CHECK_EQ(R.state, RTSM_HOT);
    feed(3000);
    for (i = 0; i < 5; i++) tick();
    CHECK_EQ(R.state, RTSM_HOT);
    tick();
    CHECK_EQ(R.state, RTSM_RUN);
}

static void t_hot_rate_of_rise(void)
{
    uint32_t i;
    setup(RTSM_SKU_LTEM);
    S.rail = 2000;
    commission_and_run();
    for (i = 0; i < 7; i++) tick();
    /* +1.5 degC per 10 min: 7.5 degC after 50 min, 9 degC after 60 min. */
    feed(2150);
    feed(2300);
    feed(2450);
    feed(2600);
    feed(2750);
    CHECK_EQ(R.state, RTSM_RUN);
    feed(2900);
    CHECK_EQ(R.state, RTSM_HOT); /* well below hot_threshold 3500 */
    CHECK_EQ(R.cadence, RTSM_CAD_HOT);
}

static void t_alarm_levels(void)
{
    struct { int16_t v; int8_t code; uint8_t level; } seq[] = {
        { 3900, 0, 0 },
        { 4000, PL_ALM_LEVEL_UP, 1 },
        { 4600, PL_ALM_LEVEL_UP, 2 },
        { 4410, 0, 2 },   /* above L2 - hyst (4400): stays 2 */
        { 4390, PL_ALM_LEVEL_DOWN, 1 },
        { 4450, 0, 1 },   /* below L2 again: no re-trigger */
        { 3910, 0, 1 },   /* above L1 - hyst */
        { 3880, PL_ALM_LEVEL_DOWN, 0 },
        { 5100, PL_ALM_LEVEL_UP, 3 }, /* 0 -> 3 in one sample: one alarm */
        { 4950, 0, 3 },
        { 3000, PL_ALM_LEVEL_DOWN, 0 },
    };
    uint32_t i, before;
    int32_t a;
    setup(RTSM_SKU_LORA);
    commission_and_run();
    tick();
    for (i = 0; i < sizeof seq / sizeof seq[0]; i++) {
        before = S.nmsg;
        feed(seq[i].v);
        CHECK_EQ(R.level, seq[i].level);
        CHECK_EQ(count_type_from(before, PL_T_ALARM), seq[i].code ? 1 : 0);
        if (seq[i].code) {
            a = last_index_of(PL_T_ALARM);
            CHECK_EQ(alarm_code((uint32_t)a), seq[i].code);
            CHECK_EQ(alarm_level((uint32_t)a), seq[i].level);
            CHECK(msg((uint32_t)a)->t == S.now); /* immediate */
            CHECK((uint32_t)a == before);        /* ahead of the data batch */
        }
    }
}

/* Shared 30-day outage scenario. Returns index of first post-reconnect msg. */
static uint16_t deliv[8192];

static void outage_scenario(rtsm_sku_t sku)
{
    uint32_t i, reconnect, k, viol = 0;
    uint32_t oldest_unsent_t;
    int32_t first_batch = -1, last_alarm = -1, second_batch = -1;
    rtsm_log_entry_t e;
    uint32_t max_per_t = 0, run = 0, run_t = 0;

    setup(sku);
    commission_and_run();
    for (i = 0; i < 144; i++) tick(); /* one good day */
    S.link_up = false;
    for (i = 0; i < 30u * 144u; i++) {
        if (i == 1440u) feed(4100);      /* level up during outage */
        else if (i == 1441u) feed(3000); /* and down again */
        else tick();
    }
    CHECK(rlog_count_unsent(&R.log) >= 30u * 144u);
    CHECK(rlog_get(&R.log, rlog_first_unsent(&R.log), &e));
    oldest_unsent_t = e.time;
    reconnect = S.nmsg;
    S.link_up = true;
    for (i = 0; i < 2u * 144u; i++) tick();

    CHECK_EQ(rlog_count_unsent(&R.log), 0);
    CHECK_EQ(R.log.lost_unsent, 0);
    CHECK(rlog_count(&R.log) > 31u * 144u);

    /* Every logged record delivered exactly once (LoRa: the "previous"
     * slot of an n=2 batch is a deliberate repeat and is tolerated). */
    memset(deliv, 0, sizeof deliv);
    for (i = 0; i < S.nmsg; i++) {
        uint8_t n;
        if (mtype(i) != PL_T_BATCH) continue;
        n = batch_n(i);
        for (k = 0; k < n; k++) {
            uint32_t idx = (batch_base(i) + k * TICK - R.anchor) / TICK;
            if (idx >= 8192u) { viol++; continue; }
            if (deliv[idx] > 0u && !(sku == RTSM_SKU_LORA && n == 2u && k == 0u)) viol++;
            deliv[idx]++;
        }
    }
    CHECK_EQ(viol, 0);
    for (i = R.log.first; i < R.log.next; i++) {
        uint32_t idx;
        CHECK(rlog_get(&R.log, i, &e));
        idx = (e.time - R.anchor) / TICK;
        if (sku == RTSM_SKU_LTEM) {
            if (deliv[idx] != 1u) { viol++; }
        } else if (deliv[idx] < 1u || deliv[idx] > 2u) {
            viol++;
        }
    }
    CHECK_EQ(viol, 0);

    /* Ordering after reconnection: alarms, then newest data, then backlog
     * oldest first. */
    for (i = reconnect; i < S.nmsg; i++) {
        if (mtype(i) == PL_T_ALARM) last_alarm = (int32_t)i;
        if (mtype(i) == PL_T_BATCH) {
            if (first_batch < 0) first_batch = (int32_t)i;
            else if (second_batch < 0) second_batch = (int32_t)i;
        }
    }
    CHECK(find_alarm(reconnect, PL_ALM_LEVEL_UP) >= 0);
    CHECK(find_alarm(reconnect, PL_ALM_LEVEL_DOWN) >= 0);
    CHECK(last_alarm >= 0 && first_batch > last_alarm);
    CHECK(find_alarm(reconnect, PL_ALM_LEVEL_UP) < find_alarm(reconnect, PL_ALM_LEVEL_DOWN));
    if (first_batch >= 0) {
        uint32_t fb = (uint32_t)first_batch;
        CHECK(batch_base(fb) + (batch_n(fb) - 1u) * TICK == msg(fb)->t); /* newest */
    }
    if (second_batch >= 0) {
        CHECK(batch_base((uint32_t)second_batch) == oldest_unsent_t);
    }
    /* Rate limit: at most 1 live + N back-fill batches per tick. */
    for (i = reconnect; i < S.nmsg; i++) {
        if (mtype(i) != PL_T_BATCH) continue;
        if (msg(i)->t != run_t) { run_t = msg(i)->t; run = 0; }
        run++;
        if (run > max_per_t) max_per_t = run;
    }
    CHECK(max_per_t <= 1u + RTSM_BACKLOG_MAX_PER_TICK);
    CHECK(max_per_t >= 2u);
}

static void t_outage_ltem(void) { outage_scenario(RTSM_SKU_LTEM); }
static void t_outage_lora(void) { outage_scenario(RTSM_SKU_LORA); }

static void t_tamper_reattach(void)
{
    uint32_t before, logn;
    int32_t a;
    setup(RTSM_SKU_LTEM);
    commission_and_run();
    advance(100);
    before = S.nmsg;
    S.hall = false;
    advance(1);
    CHECK_EQ(R.state, RTSM_ALARM_TAMPER);
    a = find_alarm(before, PL_ALM_DETACHED);
    CHECK(a >= 0);
    if (a >= 0) {
        CHECK(msg((uint32_t)a)->t == S.now);
        CHECK(!(msg((uint32_t)a)->buf[12] & PL_FLAG_ATTACHED));
    }
    logn = rlog_count(&R.log);
    advance(TICK);
    CHECK_EQ(rlog_count(&R.log), logn + 1u); /* still logging */
    S.hall = true;                           /* back on after ~11 min */
    advance(1);                              /* first step that sees it */
    advance(RTSM_TAMPER_SETTLE_S - 1u);
    CHECK_EQ(R.state, RTSM_ALARM_TAMPER);
    advance(1);
    CHECK_EQ(R.state, RTSM_RUN);
    advance(3600);
    CHECK_EQ(R.state, RTSM_RUN);
    CHECK_EQ(count_type_from(before, PL_T_ALARM), 1); /* no LOCKED alarm */

    /* Movement only (still attached): resumes after the quiet period. */
    S.imu_event = true;
    advance(1);
    CHECK_EQ(R.state, RTSM_ALARM_TAMPER);
    CHECK(find_alarm(before, PL_ALM_MOVEMENT) >= 0);
    advance(30);
    S.imu_event = true; /* new movement restarts the quiet period */
    advance(1);
    advance(RTSM_TAMPER_SETTLE_S - 1u);
    CHECK_EQ(R.state, RTSM_ALARM_TAMPER);
    advance(1);
    CHECK_EQ(R.state, RTSM_RUN);

    /* Tamper while HOT returns to HOT. */
    feed(3700);
    CHECK_EQ(R.state, RTSM_HOT);
    S.hall = false;
    advance(5);
    CHECK_EQ(R.state, RTSM_ALARM_TAMPER);
    CHECK_EQ(R.op_state, RTSM_HOT);
    S.hall = true;
    advance(RTSM_TAMPER_SETTLE_S + 1u);
    CHECK_EQ(R.state, RTSM_HOT);
}

static void t_tamper_lock(void)
{
    uint32_t before, logn, i;
    pl_config_t c = std_cfg();
    setup(RTSM_SKU_LORA);
    commission_and_run();
    advance(200);
    S.hall = false;
    advance(1);
    CHECK_EQ(R.state, RTSM_ALARM_TAMPER);
    advance(RTSM_TAMPER_WINDOW_S - 1u);
    CHECK_EQ(R.state, RTSM_ALARM_TAMPER);
    advance(1);
    CHECK_EQ(R.state, RTSM_LOCKED);
    CHECK(find_alarm(0, PL_ALM_LOCKED) >= 0);
    CHECK(msg((uint32_t)find_alarm(0, PL_ALM_LOCKED))->t == S.now);

    /* LOCKED: beacon every 6 h, nothing else; reattaching does not help. */
    S.hall = true;
    before = S.nmsg;
    logn = rlog_count(&R.log);
    advance(13u * 3600u);
    CHECK_EQ(R.state, RTSM_LOCKED);
    CHECK_EQ(rlog_count(&R.log), logn);
    CHECK_EQ(S.nmsg - before, 2);
    for (i = before; i < S.nmsg; i++) CHECK_EQ(mtype(i), PL_T_STATUS);
    CHECK_EQ(send_cfg(&c), RTSM_DL_IGNORED);
    CHECK_EQ(send_service(1), RTSM_DL_IGNORED);
    CHECK_EQ(R.state, RTSM_LOCKED);
}

static void t_lid_lock(void)
{
    uint32_t before;
    int32_t a;
    setup(RTSM_SKU_LTEM);
    commission_and_run();
    advance(300);
    before = S.nmsg;
    S.lid = false;
    advance(1);
    CHECK_EQ(R.state, RTSM_LOCKED);
    a = find_alarm(before, PL_ALM_LID_OPENED);
    CHECK(a >= 0);
    if (a >= 0) {
        CHECK(msg((uint32_t)a)->t == S.now);
        CHECK(!(msg((uint32_t)a)->buf[12] & PL_FLAG_LID_CLOSED));
    }
    S.lid = true; /* closing it again does not help */
    advance(3600);
    CHECK_EQ(R.state, RTSM_LOCKED);
}

static void t_service_mode(void)
{
    uint32_t before;
    setup(RTSM_SKU_LORA);
    commission_and_run();
    tick();
    CHECK_EQ(send_service(0), RTSM_DL_APPLIED);
    CHECK_EQ(R.state, RTSM_RUN);
    CHECK_EQ(send_service(1), RTSM_DL_APPLIED);
    CHECK_EQ(R.state, RTSM_SHIP);
    before = S.nmsg;
    advance(30);
    S.imu_event = true; /* lifted off the rail for tamping */
    advance(1800);      /* still attached for half an hour: no re-commission */
    CHECK_EQ(R.state, RTSM_SHIP);
    S.hall = false;
    S.lid = false; /* battery check */
    advance(2u * 3600u);
    CHECK_EQ(R.state, RTSM_SHIP);
    CHECK_EQ(S.nmsg, before); /* no alarms, no data */
    S.lid = true;
    S.hall = true; /* re-installed */
    advance(RTSM_ATTACH_DEBOUNCE_S + 1u);
    CHECK_EQ(R.state, RTSM_COMMISSION);
    CHECK_EQ(count_type_from(before, PL_T_ALARM), 0);
    CHECK_EQ(mtype(S.nmsg - 1u), PL_T_INSTALL);
}

static void t_unlock(void)
{
    uint8_t wrong[8];
    setup(RTSM_SKU_LORA);
    commission_and_run();
    CHECK_EQ(send_unlock(TOKEN), RTSM_DL_IGNORED); /* not locked */
    S.lid = false;
    advance(1);
    CHECK_EQ(R.state, RTSM_LOCKED);
    S.lid = true;
    memcpy(wrong, TOKEN, 8);
    wrong[7] ^= 0x01u;
    CHECK_EQ(send_unlock(wrong), RTSM_DL_REJECTED);
    CHECK_EQ(R.state, RTSM_LOCKED);
    advance(100);
    CHECK_EQ(R.state, RTSM_LOCKED);
    CHECK_EQ(send_unlock(TOKEN), RTSM_DL_APPLIED);
    CHECK_EQ(R.state, RTSM_COMMISSION);
    CHECK(!R.have_config);
    advance(1);
    CHECK_EQ(mtype(S.nmsg - 1u), PL_T_INSTALL);
}

static void t_low_battery(void)
{
    uint32_t before;
    rtsm_log_entry_t e;
    setup(RTSM_SKU_LORA);
    commission_and_run();
    before = S.nmsg;
    S.batt = 9;
    tick();
    CHECK_EQ(count_type_from(before, PL_T_ALARM), 1);
    CHECK(find_alarm(before, PL_ALM_LOW_BATTERY) >= 0);
    CHECK(last_log(&e));
    CHECK(e.flags & PL_FLAG_LOW_BATT);
    tick();
    tick();
    S.batt = 12; /* between thresholds: not re-armed */
    tick();
    S.batt = 9;
    tick();
    CHECK_EQ(count_type_from(before, PL_T_ALARM), 1);
    S.batt = 16; /* re-arm */
    tick();
    CHECK(last_log(&e));
    CHECK(!(e.flags & PL_FLAG_LOW_BATT));
    S.batt = 8;
    tick();
    CHECK_EQ(count_type_from(before, PL_T_ALARM), 2);
}

static void t_sensor_fault(void)
{
    uint32_t before;
    rtsm_log_entry_t e;
    int32_t b;
    setup(RTSM_SKU_LORA);
    commission_and_run();
    tick();
    before = S.nmsg;
    S.rail = PL_TEMP_NA;
    tick();
    CHECK_EQ(count_type_from(before, PL_T_ALARM), 1);
    CHECK(find_alarm(before, PL_ALM_SENSOR_FAULT) >= 0);
    CHECK(last_log(&e));
    CHECK_EQ(e.rail_cC, PL_TEMP_NA);
    CHECK_EQ(e.air_cC, 2500);
    CHECK(e.flags & PL_FLAG_FAULT);
    b = last_index_of(PL_T_BATCH);
    CHECK_EQ(be16(msg((uint32_t)b)->buf + 14), 0x8000); /* on air too */
    CHECK(msg((uint32_t)b)->buf[18] & PL_FLAG_FAULT);
    tick(); /* same episode: no new alarm */
    CHECK_EQ(count_type_from(before, PL_T_ALARM), 1);
    S.rail = 3300; /* recovered; previous raw invalid -> no compensation */
    tick();
    CHECK(last_log(&e));
    CHECK_EQ(e.rail_cC, 3300);
    CHECK(!(e.flags & PL_FLAG_FAULT));
    S.air = PL_TEMP_NA; /* new episode, on the air channel */
    tick();
    CHECK_EQ(count_type_from(before, PL_T_ALARM), 2);
    CHECK(last_log(&e));
    CHECK_EQ(e.rail_cC, 3300);
    CHECK(e.flags & PL_FLAG_FAULT);
}

static void t_seq_wrap_device(void)
{
    uint32_t i, before;
    uint16_t expect;
    setup(RTSM_SKU_LORA);
    commission_and_run();
    R.seq = 0xFFFDu;
    before = S.nmsg;
    tick();
    S.link_up = false; /* failed data attempts must not leave gaps */
    tick();
    tick();
    S.link_up = true;
    S.batt = 5; /* add an alarm into the mix */
    for (i = 0; i < 4; i++) tick();
    expect = 0xFFFDu;
    CHECK(S.nmsg - before >= 6u);
    for (i = before; i < S.nmsg; i++) {
        CHECK_EQ(be16(msg(i)->buf + 1), expect);
        expect = (uint16_t)(expect + 1u);
    }
    CHECK_EQ(rlog_count_unsent(&R.log), 0);
}

static void t_lag_comp_unit(void)
{
    CHECK_EQ(rtsm_rail_compensate(3000, 3000, 600), 3000);          /* steady */
    CHECK_EQ(rtsm_rail_compensate(3100, 3000, 600), 3125);          /* +1.00/10 min -> +0.25 */
    CHECK_EQ(rtsm_rail_compensate(2900, 3000, 600), 2875);          /* falling */
    CHECK_EQ(rtsm_rail_compensate(3100, PL_TEMP_NA, 600), 3100);    /* prev fault */
    CHECK_EQ(rtsm_rail_compensate(PL_TEMP_NA, 3000, 600), PL_TEMP_NA);
    CHECK_EQ(rtsm_rail_compensate(3100, 3000, 299), 3100);          /* dt out of range */
    CHECK_EQ(rtsm_rail_compensate(3100, 3000, 901), 3100);
    CHECK_EQ(rtsm_rail_compensate(3100, 3000, 300), 3150);
    CHECK_EQ(rtsm_rail_compensate(3100, 3000, 900), 3117);          /* 16.67 -> 17 */
    CHECK_EQ(rtsm_rail_compensate(3002, 3000, 600), 3003);          /* 0.5 rounds up */
    CHECK_EQ(rtsm_rail_compensate(2998, 3000, 600), 2997);          /* symmetric */
    CHECK_EQ(rtsm_rail_compensate(3001, 3000, 600), 3001);          /* 0.25 -> 0 */
    CHECK_EQ(rtsm_rail_compensate(32700, 0, 300), 32767);           /* clamped */
    CHECK_EQ(rtsm_rail_compensate(-32700, 0, 300), -32767);         /* never 0x8000 */
}

static void t_lag_comp_integration(void)
{
    rtsm_log_entry_t e;
    int32_t b;
    setup(RTSM_SKU_LORA);
    commission_and_run();
    tick();
    S.rail = 3100; /* ramp +1 degC per 10 min */
    tick();
    CHECK(last_log(&e));
    CHECK_EQ(e.rail_cC, 3125);
    CHECK_EQ(e.air_cC, 2500); /* air not compensated */
    b = last_index_of(PL_T_BATCH);
    CHECK_EQ(bei16(msg((uint32_t)b)->buf + 14), 3125);
    S.rail = 3200;
    tick();
    CHECK(last_log(&e));
    CHECK_EQ(e.rail_cC, 3225);
    tick(); /* steady again */
    CHECK(last_log(&e));
    CHECK_EQ(e.rail_cC, 3200);
    /* Compensated value drives HOT: raw 3450 after 3200 reports 3512. */
    S.rail = 3450;
    tick();
    CHECK(last_log(&e));
    CHECK_EQ(e.rail_cC, 3513); /* 250 * 150 / 600 = 62.5 -> 63 */
    CHECK_EQ(R.state, RTSM_HOT);
}

static void t_time_sync(void)
{
    uint8_t b[5] = { PL_T_TIMESYNC, 0x6A, 0x00, 0x00, 0x00 };
    rtsm_log_entry_t e;
    int32_t bi;
    setup(RTSM_SKU_LORA);
    commission_and_run();
    tick();
    advance(10);
    CHECK_EQ(rtsm_on_downlink(&R, b, sizeof b), RTSM_DL_APPLIED);
    CHECK(rtsm_device_time(&R) == 0x6A000000u);
    advance(TICK - 10u); /* schedule unchanged in HAL time */
    CHECK(last_log(&e));
    CHECK(e.time == 0x6A000000u + TICK - 10u);
    /* The batch does not span the time jump: n = 1. */
    bi = last_index_of(PL_T_BATCH);
    CHECK_EQ(batch_n((uint32_t)bi), 1);
    CHECK(batch_base((uint32_t)bi) == e.time);
    tick();
    bi = last_index_of(PL_T_BATCH);
    CHECK_EQ(batch_n((uint32_t)bi), 2);
}

static void t_next_wakeup(void)
{
    setup(RTSM_SKU_LTEM);
    CHECK(rtsm_next_wakeup(&R) == 0xFFFFFFFFu); /* SHIP: Hall IRQ only */
    S.hall = true;
    rtsm_step(&R);
    CHECK(rtsm_next_wakeup(&R) == S.now + RTSM_ATTACH_DEBOUNCE_S);
    advance(RTSM_ATTACH_DEBOUNCE_S);
    advance(1);
    CHECK(rtsm_next_wakeup(&R) == R.anchor + TICK);
}

/* ======================================================================== */
/* R-9: persistence across reset (nvstate)                                  */
/* ======================================================================== */

static uint8_t seq_seen[65536u / 8u];

/* Number of captured uplinks whose seq was already used by an earlier one. */
static uint32_t seq_reuse_count(void)
{
    uint32_t i, reuse = 0;
    memset(seq_seen, 0, sizeof seq_seen);
    for (i = 0; i < S.nmsg; i++) {
        uint16_t q = be16(msg(i)->buf + 1);
        uint8_t bit = (uint8_t)(1u << (q & 7u));
        if (seq_seen[q >> 3] & bit) reuse++;
        seq_seen[q >> 3] |= bit;
    }
    return reuse;
}

/* How many accepted batch slots carry the record with timestamp t. */
static uint32_t times_delivered(uint32_t t)
{
    uint32_t i, c = 0;
    for (i = 0; i < S.nmsg; i++) {
        uint32_t b;
        if (mtype(i) != PL_T_BATCH) continue;
        b = batch_base(i);
        if (t >= b && (t - b) % TICK == 0u && (t - b) / TICK < batch_n(i)) c++;
    }
    return c;
}

/* Every logged record delivered exactly once (LTE-M), or once or twice
 * (LoRa: the deliberate n=2 "previous" repeat). */
static uint32_t delivery_violations(void)
{
    uint32_t i, viol = 0;
    rtsm_log_entry_t e;
    for (i = R.log.first; i < R.log.next; i++) {
        uint32_t c;
        if (!rlog_get(&R.log, i, &e)) {
            viol++;
            continue;
        }
        c = times_delivered(e.time);
        if (R.id.sku == RTSM_SKU_LTEM ? c != 1u : (c < 1u || c > 2u)) viol++;
    }
    return viol;
}

static bool cfg_equal(const pl_config_t *a, const pl_config_t *b)
{
    uint8_t x[PL_CONFIG_SIZE], y[PL_CONFIG_SIZE];
    (void)pl_encode_config(x, sizeof x, a);
    (void)pl_encode_config(y, sizeof y, b);
    return memcmp(x, y, sizeof x) == 0;
}

static void t_nv_record_unit(void)
{
    static const uint8_t check[9] = { '1', '2', '3', '4', '5', '6', '7', '8', '9' };
    nvs_data_t d, o;
    uint8_t buf[NVS_RECORD_SIZE];
    uint32_t gen = 0;
    pl_config_t c = std_cfg();

    CHECK(nvs_crc32(check, sizeof check) == 0xCBF43926u); /* CRC-32 check value */
    nvs_defaults(&d, TOKEN);
    d.state_class = NVS_CLASS_LOCKED;
    d.have_config = true;
    (void)pl_encode_config(d.config, sizeof d.config, &c);
    d.reset_count = 0x1234u;
    d.seq_reserved = 0xFFF0u;
    d.energy_mAh = 70000u;
    nvs_encode(&d, 0xA0B0C0D0u, buf);
    CHECK(nvs_decode(buf, &o, &gen));
    CHECK(gen == 0xA0B0C0D0u);
    CHECK_EQ(o.state_class, NVS_CLASS_LOCKED);
    CHECK(o.have_config);
    CHECK(memcmp(o.config, d.config, PL_CONFIG_SIZE) == 0);
    CHECK(memcmp(o.token, TOKEN, PL_TOKEN_LEN) == 0);
    CHECK_EQ(o.reset_count, 0x1234);
    CHECK_EQ(o.seq_reserved, 0xFFF0);
    CHECK(o.energy_mAh == 70000u);
    buf[30] ^= 0x10u; /* one bit flipped in the token */
    CHECK(!nvs_decode(buf, &o, &gen));
    memset(buf, 0xFF, sizeof buf); /* erased */
    CHECK(!nvs_decode(buf, &o, &gen));
}

/* Reset in the middle of RUN (LTE-M, between batch boundaries): RUN and the
 * config come back, the log is back-filled exactly once, no seq is reused. */
static void t_nv_reset_resumes_run(void)
{
    pl_config_t c = std_cfg();
    uint32_t i, n_pre;
    uint16_t max_seq_pre = 0;
    int32_t st = -1;
    setup(RTSM_SKU_LTEM);
    CHECK_EQ(R.nv.d.reset_count, 1);   /* first boot from blank flash */
    CHECK_EQ(R.nv.boot_slot, NVS_NO_SLOT);
    commission_and_run();
    CHECK_EQ(R.nv.d.state_class, NVS_CLASS_RUN);
    CHECK(R.nv.d.have_config);
    for (i = 0; i < 6u * 5u + 2u; i++) tick();
    S.energy = 200; /* +80 mAh since boot: energy step written */
    tick();
    CHECK(R.nv.d.energy_mAh == 200u);
    CHECK(rlog_count_unsent(&R.log) >= 3u); /* waiting for the next hour */
    n_pre = S.nmsg;
    for (i = 0; i < n_pre; i++) {
        if (be16(msg(i)->buf + 1) > max_seq_pre) max_seq_pre = be16(msg(i)->buf + 1);
    }
    CHECK(max_seq_pre < RTSM_NV_SEQ_BLOCK);

    S.energy = 0; /* the coulomb counter restarts with the MCU */
    reboot(137);  /* off the 10-min grid on purpose */
    CHECK_EQ(R.state, RTSM_RUN);
    CHECK(R.have_config);
    CHECK(cfg_equal(&R.cfg, &c));
    CHECK_EQ(R.nv.d.reset_count, 2);
    CHECK(R.nv.boot_slot != NVS_NO_SLOT);
    CHECK(rlog_count_unsent(&R.log) >= 3u); /* log recovered */
    for (i = 0; i < 144u; i++) tick();

    /* First uplink after the reset: status with reset_count 2, energy kept,
     * seq resumed at the reservation. */
    CHECK(S.nmsg > n_pre);
    if (S.nmsg > n_pre) {
        st = (int32_t)n_pre;
        CHECK_EQ(mtype((uint32_t)st), PL_T_STATUS);
        CHECK_EQ(be16(msg((uint32_t)st)->buf + 1), RTSM_NV_SEQ_BLOCK);
        CHECK_EQ(msg((uint32_t)st)->buf[12], 2);
        CHECK_EQ(be16(msg((uint32_t)st)->buf + 8), 200);
    }
    CHECK_EQ(R.state, RTSM_RUN);
    CHECK_EQ(seq_reuse_count(), 0);
    CHECK_EQ(rlog_count_unsent(&R.log), 0);
    CHECK_EQ(delivery_violations(), 0);
}

/* Seq block reservation across several blocks, then a reset: the seq
 * resumes at the reservation (a gap, never a reuse). */
static void t_nv_seq_reservation(void)
{
    uint32_t i, n_pre, w0;
    uint16_t last_pre, reserved;
    setup(RTSM_SKU_LORA);
    commission_and_run();
    w0 = S.nv_writes;
    for (i = 0; i < 4u * 144u; i++) tick(); /* ~580 uplinks: 2 more blocks */
    CHECK_EQ(S.nv_writes - w0, 2);
    n_pre = S.nmsg;
    last_pre = be16(msg(n_pre - 1u)->buf + 1);
    reserved = R.nv.d.seq_reserved;
    CHECK((uint16_t)(reserved - last_pre) >= 1u);
    CHECK((uint16_t)(reserved - last_pre) <= RTSM_NV_SEQ_BLOCK);
    reboot(30);
    for (i = 0; i < 6u; i++) tick();
    CHECK(S.nmsg > n_pre);
    CHECK_EQ(be16(msg(n_pre)->buf + 1), reserved);
    CHECK_EQ(seq_reuse_count(), 0);
}

/* Reset in LOCKED stays LOCKED (pulling the battery does not unlock a
 * stolen unit); the persisted token still unlocks it. */
static void t_nv_reset_locked(void)
{
    pl_config_t c = std_cfg();
    uint32_t before, logn, i;
    setup(RTSM_SKU_LORA);
    commission_and_run();
    tick();
    S.lid = false;
    advance(1);
    CHECK_EQ(R.state, RTSM_LOCKED);
    CHECK_EQ(R.nv.d.state_class, NVS_CLASS_LOCKED);
    S.lid = true; /* thief closes the lid, pulls the battery, re-inserts it */
    logn = rlog_count(&R.log);
    before = S.nmsg;
    reboot(3600);
    CHECK_EQ(R.state, RTSM_LOCKED);
    advance(7u * 3600u);
    CHECK_EQ(R.state, RTSM_LOCKED);
    CHECK_EQ(rlog_count(&R.log), logn); /* no sampling */
    CHECK_EQ(S.nmsg - before, 2);        /* boot beacon + 6-h beacon */
    for (i = before; i < S.nmsg; i++) CHECK_EQ(mtype(i), PL_T_STATUS);
    CHECK_EQ(send_cfg(&c), RTSM_DL_IGNORED);
    reboot(10);
    CHECK_EQ(R.state, RTSM_LOCKED);
    CHECK_EQ(send_unlock(TOKEN), RTSM_DL_APPLIED);
    CHECK_EQ(R.state, RTSM_COMMISSION);
    CHECK_EQ(R.nv.d.state_class, NVS_CLASS_SHIP);
    CHECK(!R.nv.d.have_config);
    reboot(10); /* unlocked but not yet configured: back to SHIP */
    CHECK_EQ(R.state, RTSM_SHIP);
    CHECK(!R.have_config);
    CHECK_EQ(seq_reuse_count(), 0);
}

/* Token rotation is persisted; service-mode SHIP survives a reset. */
static void t_nv_token_and_service(void)
{
    static const uint8_t TOK2[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    uint32_t w;
    setup(RTSM_SKU_LORA);
    commission_and_run();
    w = S.nv_writes;
    rtsm_set_unlock_token(&R, TOK2);
    CHECK_EQ(S.nv_writes - w, 1);
    rtsm_set_unlock_token(&R, TOK2); /* unchanged: no write */
    CHECK_EQ(S.nv_writes - w, 1);
    reboot(5);
    CHECK_EQ(R.state, RTSM_RUN);
    S.lid = false;
    advance(1);
    CHECK_EQ(R.state, RTSM_LOCKED);
    S.lid = true;
    reboot(5);
    CHECK_EQ(send_unlock(TOKEN), RTSM_DL_REJECTED); /* factory token replaced */
    CHECK_EQ(send_unlock(TOK2), RTSM_DL_APPLIED);

    /* Service SHIP while still on the rail: a reset must not re-commission. */
    setup(RTSM_SKU_LORA);
    commission_and_run();
    CHECK_EQ(send_service(PL_SERVICE_SHIP), RTSM_DL_APPLIED);
    CHECK_EQ(R.nv.d.state_class, NVS_CLASS_SHIP_SERVICE);
    reboot(5);
    CHECK_EQ(R.state, RTSM_SHIP);
    advance(600);
    CHECK_EQ(R.state, RTSM_SHIP); /* still attached, not re-armed */
    S.hall = false;
    advance(1);
    CHECK_EQ(R.nv.d.state_class, NVS_CLASS_SHIP);
    S.hall = true;
    advance(RTSM_ATTACH_DEBOUNCE_S + 1u);
    CHECK_EQ(R.state, RTSM_COMMISSION);
}

/* Make slot A hold the newest record (B the previous one) by rotating the
 * token; returns with both slots valid. */
static void newest_in_slot_a(void)
{
    static const uint8_t TOK2[8] = { 9, 9, 9, 9, 9, 9, 9, 2 };
    static const uint8_t TOK3[8] = { 9, 9, 9, 9, 9, 9, 9, 3 };
    rtsm_set_unlock_token(&R, TOK2);
    if (R.nv.active_slot != NVS_SLOT_A) {
        rtsm_set_unlock_token(&R, TOK3);
    }
}

static void t_nv_corrupt_a_uses_b(void)
{
    pl_config_t c = std_cfg();
    nvs_data_t a, b;
    nvs_t probe;
    uint32_t ga = 0, gb = 0;
    setup(RTSM_SKU_LTEM);
    commission_and_run();
    tick();
    newest_in_slot_a();
    CHECK(nvs_decode(S.nv[0], &a, &ga));
    CHECK(nvs_decode(S.nv[1], &b, &gb));
    CHECK(ga == gb + 1u);
    nvs_load(&probe, &S.hal, TOKEN); /* highest generation wins */
    CHECK_EQ(probe.active_slot, NVS_SLOT_A);
    CHECK(memcmp(probe.d.token, a.token, PL_TOKEN_LEN) == 0);

    S.nv[0][20] ^= 0x04u; /* bit rot in slot A */
    reboot(60);
    CHECK_EQ(R.nv.boot_slot, NVS_SLOT_B);
    CHECK_EQ(R.state, RTSM_RUN);
    CHECK(R.have_config);
    CHECK(cfg_equal(&R.cfg, &c));
    CHECK(memcmp(R.nv.d.token, b.token, PL_TOKEN_LEN) == 0);
    CHECK_EQ(R.nv.d.reset_count, b.reset_count + 1u);
    CHECK_EQ(R.nv.active_slot, NVS_SLOT_A); /* boot record rewrote slot A */
    CHECK(nvs_decode(S.nv[0], &a, &ga));
    CHECK(ga == gb + 1u);
}

static void t_nv_both_corrupt_defaults(void)
{
    setup(RTSM_SKU_LTEM);
    commission_and_run();
    tick();
    newest_in_slot_a();
    S.nv[0][5] ^= 0x01u;
    S.nv[1][40] ^= 0x80u;
    reboot(60);
    CHECK_EQ(R.nv.boot_slot, NVS_NO_SLOT);
    CHECK_EQ(R.state, RTSM_SHIP);
    CHECK(!R.have_config);
    CHECK_EQ(R.nv.d.state_class, NVS_CLASS_SHIP);
    CHECK(memcmp(R.nv.d.token, TOKEN, PL_TOKEN_LEN) == 0); /* factory token */
    CHECK_EQ(R.nv.d.reset_count, 1);
    CHECK_EQ(R.seq, 0);
    /* The unit is still on the rail: it re-commissions and needs config. */
    advance(RTSM_ATTACH_DEBOUNCE_S + 1u);
    CHECK_EQ(R.state, RTSM_COMMISSION);
}

/* Flash wear in steady operation over 30 days (link up, rail 30 degC,
 * energy rising ~5 mAh/day). Returns writes in the 30 days. */
static uint32_t wear_scenario(rtsm_sku_t sku, uint32_t *uplinks)
{
    uint32_t i, w0, m0;
    setup(sku);
    commission_and_run();
    for (i = 0; i < 144u; i++) tick();
    w0 = S.nv_writes;
    m0 = S.nmsg;
    for (i = 0; i < 30u * 144u; i++) {
        if (i % 29u == 0u) S.energy++;
        tick();
    }
    *uplinks = S.nmsg - m0;
    CHECK_EQ(R.nv.write_failures, 0);
    return S.nv_writes - w0;
}

static void t_nv_flash_wear(void)
{
    uint32_t up_lora, up_ltem;
    uint32_t w_lora = wear_scenario(RTSM_SKU_LORA, &up_lora);
    uint32_t w_ltem = wear_scenario(RTSM_SKU_LTEM, &up_ltem);
    CHECK(w_lora * 100u < up_lora);      /* < 1 write per 100 uplinks */
    CHECK(w_lora <= 30u);                /* <= 1 write/day */
    CHECK(w_ltem <= 8u);                 /* << 1 write/day */
    printf("    info: 30 days steady: LoRa %u NV writes / %u uplinks, LTE-M %u NV writes / %u uplinks\n",
           (unsigned)w_lora, (unsigned)up_lora, (unsigned)w_ltem, (unsigned)up_ltem);
}

/* ======================================================================== */
/* R-9: outage behaviour (long data back-off, link_available)               */
/* ======================================================================== */

/* 48 h outage with the link state unknown. Returns the status/data send
 * attempts made during the outage. */
static uint32_t outage48_unknown(rtsm_sku_t sku)
{
    uint32_t i, k, n, a0, f0;
    setup(sku);
    commission_and_run();
    for (i = 0; i < 144u; i++) tick();
    S.link_up = false;
    S.da_n = 0;
    a0 = R.send_attempts_total;
    f0 = R.send_failures_total;
    for (i = 0; i < 288u; i++) tick();
    n = S.da_n;
    CHECK_EQ(R.send_attempts_total - a0, n); /* no alarms in this scenario */
    CHECK_EQ(R.send_failures_total - f0, n);
    /* Retry gaps: 10, 20, 40, 80, 120, 120, ... min. */
    for (k = 1; k < n && k < SIM_MAX_ATTEMPT_LOG; k++) {
        uint32_t exp = k <= 4u ? RTSM_DATA_BACKOFF_FIRST_S << (k - 1u) : RTSM_DATA_BACKOFF_MAX_S;
        CHECK_EQ(S.da_t[k] - S.da_t[k - 1u], exp);
    }
    CHECK(n <= 48u * 60u / 120u + 6u);
    CHECK(n >= 20u);
    S.link_up = true; /* reconnect; next retry is at most 120 min away */
    for (i = 0; i < 144u; i++) tick();
    CHECK_EQ(rlog_count_unsent(&R.log), 0);
    CHECK_EQ(delivery_violations(), 0);
    CHECK_EQ(seq_reuse_count(), 0);
    return n;
}

static void t_outage48_unknown_ltem(void)
{
    uint32_t n = outage48_unknown(RTSM_SKU_LTEM);
    CHECK_EQ(n, 27); /* first failure at the next hourly boundary, then 10/20/40/80/120... */
    printf("    info: LTE-M 48 h outage, link unknown: %u status/data attempts (288 before)\n",
           (unsigned)n);
}

static void t_outage48_unknown_lora(void)
{
    uint32_t n = outage48_unknown(RTSM_SKU_LORA);
    CHECK_EQ(n, 27);
    printf("    info: LoRa 48 h outage, link unknown: %u status/data attempts (288 before)\n",
           (unsigned)n);
}

/* link_available() == 0: no status/data attempt at all, but a pending alarm
 * is still attempted on every tick; everything goes out on reconnection. */
static void t_link_down_no_data_attempts(void)
{
    uint32_t i, d0, c, alarm_ticks = 0, before;
    setup(RTSM_SKU_LTEM);
    sim_enable_link_available(&S, 1);
    commission_and_run();
    for (i = 0; i < 144u; i++) tick();
    S.link_up = false;
    S.link_avail = 0;
    d0 = S.da_n;
    before = S.nmsg;
    feed(4100); /* level 1 alarm during the outage */
    for (i = 0; i < 287u; i++) {
        advance(TICK - 1u);
        c = S.attempts_by_type[PL_T_ALARM];
        advance(1); /* the tick */
        if (S.attempts_by_type[PL_T_ALARM] > c) alarm_ticks++;
    }
    CHECK_EQ(alarm_ticks, 287);
    CHECK_EQ(S.da_n - d0, 0);
    S.link_up = true;
    S.link_avail = 1;
    for (i = 0; i < 144u; i++) tick();
    CHECK(find_alarm(before, PL_ALM_LEVEL_UP) >= 0);
    CHECK_EQ(rlog_count_unsent(&R.log), 0);
    CHECK_EQ(delivery_violations(), 0);
    CHECK_EQ(seq_reuse_count(), 0);
}

const test_case_t rtsm_tests[] = {
    { "rtsm: SHIP -> COMMISSION after 60 s attached (not 59 s)", t_ship_to_commission },
    { "rtsm: no RUN without valid config", t_no_run_without_config },
    { "rtsm: logging during COMMISSION, back-filled after config", t_logging_during_commission },
    { "rtsm: install retry with back-off", t_install_backoff },
    { "rtsm: LoRa cadence, one n=2 batch per 10 min", t_lora_cadence },
    { "rtsm: LTE-M NORMAL hourly batch n=6", t_ltem_hourly },
    { "rtsm: LTE-M COOL 4-hourly batch, alarms still immediate", t_ltem_cool_mode },
    { "rtsm: LTE-M NORMAL/HOT/NORMAL/COOL transitions", t_ltem_cadence_transitions },
    { "rtsm: HOT by threshold, exit 1 h after clear", t_hot_threshold },
    { "rtsm: HOT by rate of rise", t_hot_rate_of_rise },
    { "rtsm: alarm levels with hysteresis", t_alarm_levels },
    { "rtsm: 30-day outage LTE-M, exactly once, alarms first", t_outage_ltem },
    { "rtsm: 30-day outage LoRa, no loss, no backlog duplicates", t_outage_lora },
    { "rtsm: tamper, reattach within 30 min resumes", t_tamper_reattach },
    { "rtsm: tamper, 30 min -> LOCKED, beacon only", t_tamper_lock },
    { "rtsm: lid open -> LOCKED", t_lid_lock },
    { "rtsm: service mode suppresses tamper, back to SHIP", t_service_mode },
    { "rtsm: unlock with wrong/right token", t_unlock },
    { "rtsm: low battery alarm once, re-arm above 15%", t_low_battery },
    { "rtsm: sensor fault episode handling", t_sensor_fault },
    { "rtsm: seq wraps and stays gap-free", t_seq_wrap_device },
    { "rtsm: D-13 lag compensation unit", t_lag_comp_unit },
    { "rtsm: D-13 lag compensation in the data path", t_lag_comp_integration },
    { "rtsm: time sync offsets timestamps", t_time_sync },
    { "rtsm: next wake-up", t_next_wakeup },
    { "nvstate: record encode/decode, CRC-32", t_nv_record_unit },
    { "nvstate: reset mid-RUN resumes RUN, config kept, no seq reuse", t_nv_reset_resumes_run },
    { "nvstate: seq block reservation across reset", t_nv_seq_reservation },
    { "nvstate: reset in LOCKED stays LOCKED", t_nv_reset_locked },
    { "nvstate: token rotation and service SHIP persisted", t_nv_token_and_service },
    { "nvstate: corrupt slot A falls back to slot B", t_nv_corrupt_a_uses_b },
    { "nvstate: both slots corrupt -> SHIP factory defaults", t_nv_both_corrupt_defaults },
    { "nvstate: flash wear bound in steady operation", t_nv_flash_wear },
    { "rtsm: 48 h outage LTE-M, link unknown: long back-off, exactly once", t_outage48_unknown_ltem },
    { "rtsm: 48 h outage LoRa, link unknown: long back-off, no loss", t_outage48_unknown_lora },
    { "rtsm: link_available()=0: no data attempts, alarms every tick", t_link_down_no_data_attempts },
    { NULL, NULL }
};
