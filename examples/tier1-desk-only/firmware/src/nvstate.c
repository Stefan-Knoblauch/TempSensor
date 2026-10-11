/*
 * nvstate.c - A/B persistent state record. See nvstate.h.
 */
#include "nvstate.h"

#include <string.h>

#define OFF_MAGIC   0u
#define OFF_VERSION 2u
#define OFF_GEN     3u
#define OFF_CLASS   7u
#define OFF_FLAGS   8u
#define OFF_CONFIG  9u
#define OFF_TOKEN   (OFF_CONFIG + PL_CONFIG_SIZE)
#define OFF_RESETS  (OFF_TOKEN + PL_TOKEN_LEN)
#define OFF_SEQRES  (OFF_RESETS + 2u)
#define OFF_ENERGY  (OFF_SEQRES + 2u)
#define OFF_CRC     (OFF_ENERGY + 4u)

#if OFF_CRC + 4u != NVS_RECORD_SIZE
#error "nvstate record layout does not match NVS_RECORD_SIZE"
#endif

#define FLAG_HAVE_CONFIG 0x01u

uint32_t nvs_crc32(const uint8_t *buf, size_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    size_t i;
    uint8_t b;
    for (i = 0; i < len; i++) {
        crc ^= buf[i];
        for (b = 0; b < 8u; b++) {
            crc = (crc & 1u) ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t get32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

void nvs_encode(const nvs_data_t *d, uint32_t generation, uint8_t out[NVS_RECORD_SIZE])
{
    out[OFF_MAGIC] = NVS_MAGIC0;
    out[OFF_MAGIC + 1u] = NVS_MAGIC1;
    out[OFF_VERSION] = NVS_VERSION;
    put32(out + OFF_GEN, generation);
    out[OFF_CLASS] = d->state_class;
    out[OFF_FLAGS] = d->have_config ? FLAG_HAVE_CONFIG : 0u;
    memcpy(out + OFF_CONFIG, d->config, PL_CONFIG_SIZE);
    memcpy(out + OFF_TOKEN, d->token, PL_TOKEN_LEN);
    put16(out + OFF_RESETS, d->reset_count);
    put16(out + OFF_SEQRES, d->seq_reserved);
    put32(out + OFF_ENERGY, d->energy_mAh);
    put32(out + OFF_CRC, nvs_crc32(out, OFF_CRC));
}

bool nvs_decode(const uint8_t in[NVS_RECORD_SIZE], nvs_data_t *d, uint32_t *generation)
{
    if (in[OFF_MAGIC] != NVS_MAGIC0 || in[OFF_MAGIC + 1u] != NVS_MAGIC1 ||
        in[OFF_VERSION] != NVS_VERSION || get32(in + OFF_CRC) != nvs_crc32(in, OFF_CRC) ||
        in[OFF_CLASS] >= (uint8_t)NVS_CLASS_COUNT || (in[OFF_FLAGS] & ~FLAG_HAVE_CONFIG) != 0u) {
        return false;
    }
    *generation = get32(in + OFF_GEN);
    d->state_class = in[OFF_CLASS];
    d->have_config = (in[OFF_FLAGS] & FLAG_HAVE_CONFIG) != 0u;
    memcpy(d->config, in + OFF_CONFIG, PL_CONFIG_SIZE);
    memcpy(d->token, in + OFF_TOKEN, PL_TOKEN_LEN);
    d->reset_count = get16(in + OFF_RESETS);
    d->seq_reserved = get16(in + OFF_SEQRES);
    d->energy_mAh = get32(in + OFF_ENERGY);
    return true;
}

void nvs_defaults(nvs_data_t *d, const uint8_t factory_token[PL_TOKEN_LEN])
{
    memset(d, 0, sizeof *d);
    d->state_class = (uint8_t)NVS_CLASS_SHIP;
    memcpy(d->token, factory_token, PL_TOKEN_LEN);
}

void nvs_load(nvs_t *n, const rtsm_hal_t *hal, const uint8_t factory_token[PL_TOKEN_LEN])
{
    uint8_t buf[NVS_RECORD_SIZE];
    nvs_data_t d;
    uint32_t gen;
    uint8_t slot;

    memset(n, 0, sizeof *n);
    n->hal = hal;
    n->active_slot = NVS_NO_SLOT;
    nvs_defaults(&n->d, factory_token);

    if (hal->nv_read == NULL) {
        n->boot_slot = NVS_NO_SLOT;
        return;
    }
    for (slot = NVS_SLOT_A; slot <= NVS_SLOT_B; slot++) {
        if (!hal->nv_read(hal->ctx, slot, buf, sizeof buf) || !nvs_decode(buf, &d, &gen)) {
            continue;
        }
        if (n->active_slot == NVS_NO_SLOT || gen > n->generation) {
            n->d = d;
            n->generation = gen;
            n->active_slot = slot;
        }
    }
    n->boot_slot = n->active_slot;
}

bool nvs_differs(const nvs_t *n, const nvs_data_t *d)
{
    const nvs_data_t *s = &n->d;
    return s->state_class != d->state_class || s->have_config != d->have_config ||
           memcmp(s->config, d->config, PL_CONFIG_SIZE) != 0 ||
           memcmp(s->token, d->token, PL_TOKEN_LEN) != 0 || s->reset_count != d->reset_count ||
           s->seq_reserved != d->seq_reserved || s->energy_mAh != d->energy_mAh;
}

bool nvs_commit(nvs_t *n, const nvs_data_t *d)
{
    uint8_t buf[NVS_RECORD_SIZE];
    uint8_t target = n->active_slot == NVS_SLOT_A ? NVS_SLOT_B : NVS_SLOT_A;
    uint32_t gen = n->generation + 1u;
    bool ok;

    n->d = *d;
    if (n->hal->nv_write == NULL) {
        return false; /* no persistence on this port */
    }
    nvs_encode(d, gen, buf);
    n->writes++;
    ok = n->hal->nv_write(n->hal->ctx, target, buf, sizeof buf);
    if (ok) {
        n->generation = gen;
        n->active_slot = target;
    } else {
        n->write_failures++;
    }
    return ok;
}
