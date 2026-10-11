/*
 * payload.c - Encoders/decoders for payload format v0. See payload.h.
 */
#include "payload.h"

/* ---- Big-endian helpers ------------------------------------------------- */
static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t get_u32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/* Two's complement conversion without implementation-defined casts. */
static int16_t get_i16(const uint8_t *p)
{
    uint16_t v = get_u16(p);
    return v < 0x8000u ? (int16_t)v : (int16_t)((int32_t)v - 65536);
}

/* Common uplink header: type, seq, time. Returns bytes written (7). */
static size_t put_header(uint8_t *out, uint8_t type, uint16_t seq, uint32_t time)
{
    out[0] = type;
    put_u16(out + 1, seq);
    put_u32(out + 3, time);
    return 7;
}

/* ---- Public API ---------------------------------------------------------- */
uint8_t pl_make_flags(bool attached, bool lid_closed, bool hot, uint8_t level,
                      bool low_batt, bool fault)
{
    uint8_t f = 0;
    if (level > 3u) {
        level = 3u;
    }
    if (attached) f |= PL_FLAG_ATTACHED;
    if (lid_closed) f |= PL_FLAG_LID_CLOSED;
    if (hot) f |= PL_FLAG_HOT;
    f |= (uint8_t)(level << PL_FLAG_LEVEL_SHIFT);
    if (low_batt) f |= PL_FLAG_LOW_BATT;
    if (fault) f |= PL_FLAG_FAULT;
    return f;
}

uint16_t pl_seq_next(uint16_t *counter)
{
    uint16_t v = *counter;
    *counter = (uint16_t)(v + 1u); /* wraps 0xFFFF -> 0 */
    return v;
}

size_t pl_encode_record(uint8_t *out, const pl_record_t *rec)
{
    put_u16(out, (uint16_t)rec->rail_cC);
    put_u16(out + 2, (uint16_t)rec->air_cC);
    out[4] = (uint8_t)(rec->flags & 0x7Fu); /* bit7 reserved = 0 */
    return PL_RECORD_SIZE;
}

size_t pl_encode_batch(uint8_t *out, size_t cap, uint16_t seq, uint32_t base_time,
                       uint8_t interval_min, const pl_record_t *recs, uint8_t n)
{
    size_t len = PL_BATCH_SIZE(n);
    uint8_t i;
    if (n == 0u || cap < len) {
        return 0;
    }
    out[0] = PL_T_BATCH;
    put_u16(out + 1, seq);
    put_u32(out + 3, base_time); /* time of the OLDEST record */
    out[7] = interval_min;
    out[8] = n;
    for (i = 0; i < n; i++) {
        (void)pl_encode_record(out + PL_BATCH_HDR_SIZE + (size_t)i * PL_RECORD_SIZE, &recs[i]);
    }
    return len;
}

size_t pl_encode_alarm(uint8_t *out, size_t cap, uint16_t seq, uint32_t time,
                       uint8_t code, const pl_record_t *rec)
{
    if (cap < PL_ALARM_SIZE) {
        return 0;
    }
    (void)put_header(out, PL_T_ALARM, seq, time);
    out[7] = code;
    (void)pl_encode_record(out + 8, rec);
    return PL_ALARM_SIZE;
}

size_t pl_encode_status(uint8_t *out, size_t cap, const pl_status_t *s)
{
    if (cap < PL_STATUS_SIZE) {
        return 0;
    }
    (void)put_header(out, PL_T_STATUS, s->seq, s->time);
    out[7] = s->battery_pct;
    put_u16(out + 8, s->energy_mAh);
    put_u16(out + 10, s->fw_version);
    out[12] = s->reset_count;
    out[13] = s->tx_fail_count;
    out[14] = s->link_quality;
    return PL_STATUS_SIZE;
}

size_t pl_encode_install(uint8_t *out, size_t cap, const pl_install_t *s)
{
    if (cap < PL_INSTALL_SIZE) {
        return 0;
    }
    (void)put_header(out, PL_T_INSTALL, s->seq, s->time);
    out[7] = s->hw_rev;
    put_u16(out + 8, s->fw_version);
    out[10] = s->attached;
    out[11] = s->battery_pct;
    return PL_INSTALL_SIZE;
}

size_t pl_encode_config(uint8_t *out, size_t cap, const pl_config_t *c)
{
    if (cap < PL_CONFIG_SIZE) {
        return 0;
    }
    out[0] = PL_T_CONFIG;
    put_u16(out + 1, (uint16_t)c->l1);
    put_u16(out + 3, (uint16_t)c->l2);
    put_u16(out + 5, (uint16_t)c->l3);
    put_u16(out + 7, (uint16_t)c->hot_threshold);
    put_u16(out + 9, c->rate_of_rise);
    put_u16(out + 11, c->hysteresis);
    out[13] = c->batch_period_min;
    put_u16(out + 14, (uint16_t)c->cool_threshold);
    out[16] = c->cool_batch_min;
    out[17] = c->hot_batch_min;
    return PL_CONFIG_SIZE;
}

bool pl_config_valid(const pl_config_t *c)
{
    bool period_ok = c->batch_period_min == 10u || c->batch_period_min == 20u ||
                     c->batch_period_min == 30u || c->batch_period_min == 60u;
    bool cool_ok = c->cool_batch_min == 60u || c->cool_batch_min == 120u ||
                   c->cool_batch_min == 240u;
    bool hot_ok = c->hot_batch_min == 10u || c->hot_batch_min == 20u ||
                  c->hot_batch_min == 30u;
    return c->l1 < c->l2 && c->l2 < c->l3 && c->hysteresis <= 500u && period_ok &&
           cool_ok && hot_ok && c->cool_threshold < c->hot_threshold;
}

pl_dl_status_t pl_decode_downlink(const uint8_t *buf, size_t len, pl_downlink_t *out)
{
    uint8_t i;
    if (buf == NULL || len == 0u || out == NULL) {
        return PL_DL_ERR_EMPTY;
    }
    out->type = buf[0];
    switch (buf[0]) {
    case PL_T_CONFIG: {
        pl_config_t *c = &out->u.config;
        if (len != PL_CONFIG_SIZE) return PL_DL_ERR_LEN;
        c->l1 = get_i16(buf + 1);
        c->l2 = get_i16(buf + 3);
        c->l3 = get_i16(buf + 5);
        c->hot_threshold = get_i16(buf + 7);
        c->rate_of_rise = get_u16(buf + 9);
        c->hysteresis = get_u16(buf + 11);
        c->batch_period_min = buf[13];
        c->cool_threshold = get_i16(buf + 14);
        c->cool_batch_min = buf[16];
        c->hot_batch_min = buf[17];
        return pl_config_valid(c) ? PL_DL_OK : PL_DL_ERR_INVALID;
    }
    case PL_T_SERVICE:
        if (len != PL_SERVICE_SIZE) return PL_DL_ERR_LEN;
        if (buf[1] > PL_SERVICE_SHIP) return PL_DL_ERR_INVALID;
        out->u.service_mode = buf[1];
        return PL_DL_OK;
    case PL_T_UNLOCK:
        if (len != PL_UNLOCK_SIZE) return PL_DL_ERR_LEN;
        for (i = 0; i < PL_TOKEN_LEN; i++) {
            out->u.token[i] = buf[1 + i];
        }
        return PL_DL_OK;
    case PL_T_TIMESYNC:
        if (len != PL_TIMESYNC_SIZE) return PL_DL_ERR_LEN;
        out->u.time = get_u32(buf + 1);
        return PL_DL_OK;
    default:
        return PL_DL_ERR_TYPE;
    }
}
