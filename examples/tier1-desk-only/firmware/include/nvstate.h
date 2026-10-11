/*
 * nvstate.h - Persistent device state that must survive a reset (R-9).
 *
 * One small record is kept in two A/B slots through the optional HAL
 * functions nv_read() / nv_write(). Each record carries a generation counter
 * and a CRC-32. At boot the valid slot with the highest generation wins; a
 * blank or corrupt store (both slots invalid) gives factory defaults
 * (SHIP, no config, factory unlock token). A write always goes to the slot
 * that does NOT hold the current record, so a power cut during a write can
 * only lose the record being written, never the previous one.
 *
 * The module only stores and loads; the state machine (rtsm.c) decides when
 * to write (on change of config / state class / token, on seq block
 * reservation, on an energy step and once per boot).
 *
 * Record layout (big-endian, NVS_RECORD_SIZE bytes):
 *   0  magic 'R','9'        2  version            3  generation u32
 *   7  state class          8  flags (bit0 have_config)
 *   9  config, 18 B as the 0x81 downlink (type byte included)
 *  27  unlock token, 8 B   35  reset_count u16   37  seq_reserved u16
 *  39  energy_mAh u32      43  CRC-32 (IEEE, over bytes 0..42)
 */
#ifndef NVSTATE_H
#define NVSTATE_H

#include "payload.h"
#include "rtsm_hal.h"

#define NVS_MAGIC0 0x52u /* 'R' */
#define NVS_MAGIC1 0x39u /* '9' */
#define NVS_VERSION 1u
#define NVS_RECORD_SIZE 47u
#define NVS_SLOT_A 0u
#define NVS_SLOT_B 1u
#define NVS_NO_SLOT 0xFFu

/* Operating state class kept across a reset. */
typedef enum {
    NVS_CLASS_SHIP = 0,     /* factory / no config: SHIP, re-commission on attach */
    NVS_CLASS_SHIP_SERVICE, /* SHIP after 0x82 mode 1: must be seen detached first */
    NVS_CLASS_RUN,          /* commissioned, valid config: resume RUN */
    NVS_CLASS_LOCKED,       /* stays LOCKED until an authorised 0x83 */
    NVS_CLASS_COUNT
} nvs_class_t;

typedef struct {
    uint8_t state_class;            /* nvs_class_t */
    bool have_config;
    uint8_t config[PL_CONFIG_SIZE]; /* encoded 0x81; all zero when !have_config */
    uint8_t token[PL_TOKEN_LEN];
    uint16_t reset_count;
    uint16_t seq_reserved;          /* first seq that may be used after a reset */
    uint32_t energy_mAh;            /* cumulative, as of the last write */
} nvs_data_t;

typedef struct {
    const rtsm_hal_t *hal;
    nvs_data_t d;           /* what was last written (or loaded) */
    uint32_t generation;    /* generation of d */
    uint8_t active_slot;    /* slot holding d, or NVS_NO_SLOT */
    uint8_t boot_slot;      /* slot loaded at boot, NVS_NO_SLOT = defaults */
    uint32_t writes;        /* nv_write calls made (flash wear) */
    uint32_t write_failures;
} nvs_t;

/* IEEE 802.3 CRC-32 (reflected, poly 0xEDB88320, init/xorout 0xFFFFFFFF). */
uint32_t nvs_crc32(const uint8_t *buf, size_t len);

/* Serialise / parse one record. nvs_decode() returns false on a bad magic,
 * version, CRC or field value. */
void nvs_encode(const nvs_data_t *d, uint32_t generation, uint8_t out[NVS_RECORD_SIZE]);
bool nvs_decode(const uint8_t in[NVS_RECORD_SIZE], nvs_data_t *d, uint32_t *generation);

/* Factory defaults: SHIP, no config, the factory token, counters 0. */
void nvs_defaults(nvs_data_t *d, const uint8_t factory_token[PL_TOKEN_LEN]);

/* Read both slots and load the newest valid record, or factory defaults.
 * Never writes. hal->nv_read may be NULL (no persistence). */
void nvs_load(nvs_t *n, const rtsm_hal_t *hal, const uint8_t factory_token[PL_TOKEN_LEN]);

/* True if `d` differs from what is stored (energy is compared too). */
bool nvs_differs(const nvs_t *n, const nvs_data_t *d);

/* Write `d` with generation+1 into the other slot. `n->d` is updated even
 * if the write fails (the failure is counted, and the next change retries),
 * so a broken flash cannot cause a write storm. Returns true on success. */
bool nvs_commit(nvs_t *n, const nvs_data_t *d);

#endif /* NVSTATE_H */
