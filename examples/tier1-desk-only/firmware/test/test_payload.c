/*
 * test_payload.c - Byte-exact encoder vectors, downlink decoding and
 * validation, queue and log unit tests.
 */
#include <string.h>

#include "payload.h"
#include "queue.h"
#include "rlog.h"
#include "sim_hal.h"
#include "test.h"

#define T_BASE 0x6955B900u /* 2026-01-01T00:00:00Z */

static int bytes_eq(const uint8_t *a, const uint8_t *b, size_t n)
{
    return memcmp(a, b, n) == 0;
}

static void t_flags(void)
{
    CHECK_EQ(pl_make_flags(true, true, false, 0, false, false), 0x03);
    CHECK_EQ(pl_make_flags(true, true, true, 2, false, false), 0x17);
    CHECK_EQ(pl_make_flags(false, false, false, 3, true, true), 0x78);
    CHECK_EQ(pl_make_flags(false, false, false, 7, false, false), 0x18); /* clamped */
}

static void t_encode_batch(void)
{
    const pl_record_t recs[2] = { { 2512, 1830, 0x03 }, { -150, PL_TEMP_NA, 0x43 } };
    const uint8_t exp[] = { 0x01, 0x12, 0x34, 0x69, 0x55, 0xB9, 0x00, 0x0A, 0x02,
                            0x09, 0xD0, 0x07, 0x26, 0x03,
                            0xFF, 0x6A, 0x80, 0x00, 0x43 };
    uint8_t out[64];
    size_t n = pl_encode_batch(out, sizeof out, 0x1234, T_BASE, 10, recs, 2);
    CHECK_EQ(n, 19);
    CHECK_EQ(PL_BATCH_SIZE(6), 39);
    CHECK(bytes_eq(out, exp, sizeof exp));
    CHECK_EQ(pl_encode_batch(out, 18, 0x1234, T_BASE, 10, recs, 2), 0); /* cap */
    CHECK_EQ(pl_encode_batch(out, sizeof out, 0, T_BASE, 10, recs, 0), 0); /* n = 0 */
}

static void t_encode_alarm(void)
{
    const pl_record_t rec = { 4650, 3100, 0x17 };
    const uint8_t exp[] = { 0x02, 0xFF, 0xFF, 0x69, 0x55, 0xB9, 0x00, 0x01,
                            0x12, 0x2A, 0x0C, 0x1C, 0x17 };
    uint8_t out[16];
    CHECK_EQ(pl_encode_alarm(out, sizeof out, 0xFFFF, T_BASE, PL_ALM_LEVEL_UP, &rec), 13);
    CHECK(bytes_eq(out, exp, sizeof exp));
    CHECK_EQ(pl_encode_alarm(out, 12, 0, 0, 0, &rec), 0);
}

static void t_encode_status(void)
{
    const pl_status_t s = { 0x0001, T_BASE, 87, 1234, 0x0102, 3, 5, 200 };
    const uint8_t exp[] = { 0x03, 0x00, 0x01, 0x69, 0x55, 0xB9, 0x00, 0x57,
                            0x04, 0xD2, 0x01, 0x02, 0x03, 0x05, 0xC8 };
    uint8_t out[16];
    CHECK_EQ(pl_encode_status(out, sizeof out, &s), 15);
    CHECK(bytes_eq(out, exp, sizeof exp));
}

static void t_encode_install(void)
{
    const pl_install_t s = { 0x0000, T_BASE, 2, 0x0102, 1, 100 };
    const uint8_t exp[] = { 0x04, 0x00, 0x00, 0x69, 0x55, 0xB9, 0x00, 0x02,
                            0x01, 0x02, 0x01, 0x64 };
    uint8_t out[16];
    CHECK_EQ(pl_encode_install(out, sizeof out, &s), 12);
    CHECK(bytes_eq(out, exp, sizeof exp));
}

static const uint8_t CFG_OK[18] = { 0x81, 0x0F, 0xA0, 0x11, 0x94, 0x13, 0x88, 0x0D, 0xAC,
                                    0x03, 0x20, 0x00, 0x64, 0x3C, 0x09, 0xC4, 0xF0, 0x14 };

static void t_decode_config(void)
{
    pl_downlink_t dl;
    uint8_t enc[18];
    CHECK_EQ(pl_decode_downlink(CFG_OK, sizeof CFG_OK, &dl), PL_DL_OK);
    CHECK_EQ(dl.type, PL_T_CONFIG);
    CHECK_EQ(dl.u.config.l1, 4000);
    CHECK_EQ(dl.u.config.l2, 4500);
    CHECK_EQ(dl.u.config.l3, 5000);
    CHECK_EQ(dl.u.config.hot_threshold, 3500);
    CHECK_EQ(dl.u.config.rate_of_rise, 800);
    CHECK_EQ(dl.u.config.hysteresis, 100);
    CHECK_EQ(dl.u.config.batch_period_min, 60);
    CHECK_EQ(dl.u.config.cool_threshold, 2500);
    CHECK_EQ(dl.u.config.cool_batch_min, 240);
    CHECK_EQ(dl.u.config.hot_batch_min, 20);
    /* Encoder round trip. */
    CHECK_EQ(pl_encode_config(enc, sizeof enc, &dl.u.config), 18);
    CHECK(bytes_eq(enc, CFG_OK, 18));

    /* Negative thresholds decode as signed. */
    {
        pl_config_t c = dl.u.config;
        c.l1 = -500;
        c.cool_threshold = -1000;
        (void)pl_encode_config(enc, sizeof enc, &c);
        CHECK_EQ(pl_decode_downlink(enc, 18, &dl), PL_DL_OK);
        CHECK_EQ(dl.u.config.l1, -500);
        CHECK_EQ(dl.u.config.cool_threshold, -1000);
    }
}

/* Mutate one field of the good config and expect rejection. */
static pl_dl_status_t decode_mut(void (*mut)(pl_config_t *))
{
    pl_downlink_t dl;
    uint8_t buf[18];
    (void)pl_decode_downlink(CFG_OK, sizeof CFG_OK, &dl);
    mut(&dl.u.config);
    (void)pl_encode_config(buf, sizeof buf, &dl.u.config);
    return pl_decode_downlink(buf, sizeof buf, &dl);
}
static void m_l1_eq_l2(pl_config_t *c) { c->l1 = c->l2; }
static void m_l2_gt_l3(pl_config_t *c) { c->l2 = (int16_t)(c->l3 + 1); }
static void m_hyst(pl_config_t *c) { c->hysteresis = 501; }
static void m_hyst_ok(pl_config_t *c) { c->hysteresis = 500; }
static void m_period(pl_config_t *c) { c->batch_period_min = 15; }
static void m_period0(pl_config_t *c) { c->batch_period_min = 0; }
static void m_cool_b(pl_config_t *c) { c->cool_batch_min = 180; }
static void m_hot_b(pl_config_t *c) { c->hot_batch_min = 60; }
static void m_cool_eq_hot(pl_config_t *c) { c->cool_threshold = c->hot_threshold; }
static void m_all_ok(pl_config_t *c) { c->batch_period_min = 10; c->cool_batch_min = 60; c->hot_batch_min = 30; }

static void t_decode_config_reject(void)
{
    pl_downlink_t dl;
    CHECK_EQ(decode_mut(m_l1_eq_l2), PL_DL_ERR_INVALID);
    CHECK_EQ(decode_mut(m_l2_gt_l3), PL_DL_ERR_INVALID);
    CHECK_EQ(decode_mut(m_hyst), PL_DL_ERR_INVALID);
    CHECK_EQ(decode_mut(m_hyst_ok), PL_DL_OK);
    CHECK_EQ(decode_mut(m_period), PL_DL_ERR_INVALID);
    CHECK_EQ(decode_mut(m_period0), PL_DL_ERR_INVALID);
    CHECK_EQ(decode_mut(m_cool_b), PL_DL_ERR_INVALID);
    CHECK_EQ(decode_mut(m_hot_b), PL_DL_ERR_INVALID);
    CHECK_EQ(decode_mut(m_cool_eq_hot), PL_DL_ERR_INVALID);
    CHECK_EQ(decode_mut(m_all_ok), PL_DL_OK);
    CHECK_EQ(pl_decode_downlink(CFG_OK, 17, &dl), PL_DL_ERR_LEN);
    CHECK_EQ(pl_decode_downlink(CFG_OK, 14, &dl), PL_DL_ERR_LEN); /* old v0 size */
    CHECK_EQ(pl_decode_downlink(CFG_OK, 0, &dl), PL_DL_ERR_EMPTY);
}

static void t_decode_other(void)
{
    pl_downlink_t dl;
    const uint8_t svc1[] = { 0x82, 0x01 }, svc0[] = { 0x82, 0x00 }, svc2[] = { 0x82, 0x02 };
    const uint8_t unl[] = { 0x83, 1, 2, 3, 4, 5, 6, 7, 8 };
    const uint8_t ts[] = { 0x84, 0x69, 0x55, 0xB9, 0x00 };
    const uint8_t bad[] = { 0x85, 0x00 };
    CHECK_EQ(pl_decode_downlink(svc1, 2, &dl), PL_DL_OK);
    CHECK_EQ(dl.u.service_mode, 1);
    CHECK_EQ(pl_decode_downlink(svc0, 2, &dl), PL_DL_OK);
    CHECK_EQ(dl.u.service_mode, 0);
    CHECK_EQ(pl_decode_downlink(svc2, 2, &dl), PL_DL_ERR_INVALID);
    CHECK_EQ(pl_decode_downlink(svc1, 1, &dl), PL_DL_ERR_LEN);
    CHECK_EQ(pl_decode_downlink(unl, 9, &dl), PL_DL_OK);
    CHECK(bytes_eq(dl.u.token, unl + 1, 8));
    CHECK_EQ(pl_decode_downlink(unl, 8, &dl), PL_DL_ERR_LEN);
    CHECK_EQ(pl_decode_downlink(ts, 5, &dl), PL_DL_OK);
    CHECK(dl.u.time == T_BASE);
    CHECK_EQ(pl_decode_downlink(ts, 6, &dl), PL_DL_ERR_LEN);
    CHECK_EQ(pl_decode_downlink(bad, 2, &dl), PL_DL_ERR_TYPE);
    CHECK_EQ(pl_decode_downlink(NULL, 2, &dl), PL_DL_ERR_EMPTY);
}

static void t_seq_wrap(void)
{
    uint16_t c = 0xFFFE;
    CHECK_EQ(pl_seq_next(&c), 0xFFFE);
    CHECK_EQ(pl_seq_next(&c), 0xFFFF);
    CHECK_EQ(pl_seq_next(&c), 0x0000);
    CHECK_EQ(c, 1);
}

static void t_queue(void)
{
    mq_msg_t st[3];
    mq_t q;
    uint8_t b[1];
    uint8_t i;
    mq_init(&q, st, 3);
    CHECK(mq_peek(&q) == NULL);
    for (i = 1; i <= 3; i++) {
        b[0] = i;
        CHECK(mq_push(&q, b, 1));
    }
    b[0] = 4;
    CHECK(!mq_push(&q, b, 1)); /* drops oldest (1) */
    CHECK_EQ(q.dropped, 1);
    CHECK_EQ(mq_peek(&q)->buf[0], 2);
    mq_pop(&q);
    CHECK_EQ(mq_peek(&q)->buf[0], 3);
    mq_pop(&q);
    CHECK_EQ(mq_peek(&q)->buf[0], 4);
    mq_pop(&q);
    CHECK(mq_peek(&q) == NULL);
    CHECK(!mq_push(&q, b, MQ_MSG_MAX + 1u));
}

static sim_t LS;

static void t_rlog(void)
{
    rlog_t l;
    rtsm_log_entry_t e;
    uint32_t abs = 0;
    uint32_t i;
    sim_init(&LS, T_BASE);
    rlog_init(&l, &LS.hal, RLOG_CAPACITY);
    CHECK_EQ(rlog_count(&l), 0);
    memset(&e, 0, sizeof e);
    for (i = 0; i < 10; i++) {
        e.time = T_BASE + i * 600u;
        CHECK(rlog_append(&l, &e, &abs));
        CHECK_EQ(abs, i);
    }
    CHECK(rlog_mark_sent(&l, 0));
    CHECK(rlog_mark_sent(&l, 1));
    CHECK(rlog_mark_sent(&l, 5)); /* island */
    CHECK_EQ(rlog_first_unsent(&l), 2);
    CHECK_EQ(rlog_count_unsent(&l), 7);

    /* Recovery after "reset" keeps contents and sent flags. */
    rlog_init(&l, &LS.hal, RLOG_CAPACITY);
    CHECK_EQ(rlog_count(&l), 10);
    CHECK_EQ(rlog_first_unsent(&l), 2);
    CHECK(!rlog_is_unsent(&l, 5));

    /* Fill past capacity: oldest overwritten, unsent loss counted. */
    for (i = 10; i < RLOG_CAPACITY + 5u; i++) {
        e.time = T_BASE + i * 600u;
        CHECK(rlog_append(&l, &e, NULL));
    }
    CHECK_EQ(rlog_count(&l), RLOG_CAPACITY);
    CHECK_EQ(l.first, 5);
    CHECK_EQ(l.lost_unsent, 3); /* abs 2,3,4 were unsent; 0,1 sent */
    CHECK(rlog_get(&l, l.next - 1u, &e));
    CHECK(e.time == T_BASE + (RLOG_CAPACITY + 4u) * 600u);

    /* Recovery of a full, wrapped ring. */
    {
        rlog_t l2;
        rlog_init(&l2, &LS.hal, RLOG_CAPACITY);
        CHECK_EQ(rlog_count(&l2), RLOG_CAPACITY);
        CHECK(rlog_get(&l2, l2.first, &e));
        CHECK(e.time == T_BASE + 5u * 600u);
        CHECK(rlog_get(&l2, l2.next - 1u, &e));
        CHECK(e.time == T_BASE + (RLOG_CAPACITY + 4u) * 600u);
        CHECK_EQ(rlog_count_unsent(&l2), rlog_count_unsent(&l));
    }
}

const test_case_t payload_tests[] = {
    { "payload: record flags", t_flags },
    { "payload: batch encode vector (n=2, fault 0x8000)", t_encode_batch },
    { "payload: alarm encode vector", t_encode_alarm },
    { "payload: status encode vector", t_encode_status },
    { "payload: install encode vector", t_encode_install },
    { "payload: config 0x81 decode (18 B) + encoder round trip", t_decode_config },
    { "payload: config 0x81 validation rejects", t_decode_config_reject },
    { "payload: service/unlock/timesync decode, bad type/len", t_decode_other },
    { "payload: seq counter wraps", t_seq_wrap },
    { "queue: FIFO, drop-oldest", t_queue },
    { "rlog: append, sent flags, recovery, wrap", t_rlog },
    { NULL, NULL }
};
