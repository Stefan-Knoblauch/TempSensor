/*
 * payload.h - Tier 1 payload format v0 (bearer-independent).
 *
 * All multi-byte fields are big-endian. Temperatures are int16 in 0.01 degC
 * ("centi-degC", cC). The value 0x8000 (INT16_MIN) means "not available /
 * sensor fault".
 *
 * Uplinks are produced with the pl_encode_* functions; downlinks are parsed
 * and validated with pl_decode_downlink(). No allocation, no global state.
 */
#ifndef PAYLOAD_H
#define PAYLOAD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* "Not available / sensor fault" temperature marker. */
#define PL_TEMP_NA ((int16_t)(-32767 - 1))

/* ---- Message types ---------------------------------------------------- */
#define PL_T_BATCH    0x01u
#define PL_T_ALARM    0x02u
#define PL_T_STATUS   0x03u
#define PL_T_INSTALL  0x04u
#define PL_T_CONFIG   0x81u
#define PL_T_SERVICE  0x82u
#define PL_T_UNLOCK   0x83u
#define PL_T_TIMESYNC 0x84u

/* ---- Sizes (bytes) ----------------------------------------------------- */
#define PL_RECORD_SIZE    5u
#define PL_BATCH_HDR_SIZE 9u
#define PL_BATCH_SIZE(n)  (PL_BATCH_HDR_SIZE + PL_RECORD_SIZE * (size_t)(n))
#define PL_ALARM_SIZE     13u
#define PL_STATUS_SIZE    15u
#define PL_INSTALL_SIZE   12u
#define PL_CONFIG_SIZE    18u /* D-11: extended from 14 B */
#define PL_SERVICE_SIZE   2u
#define PL_UNLOCK_SIZE    9u
#define PL_TIMESYNC_SIZE  5u
#define PL_TOKEN_LEN      8u

/* ---- Record flag bits --------------------------------------------------- */
#define PL_FLAG_ATTACHED    0x01u
#define PL_FLAG_LID_CLOSED  0x02u
#define PL_FLAG_HOT         0x04u
#define PL_FLAG_LEVEL_SHIFT 3u
#define PL_FLAG_LEVEL_MASK  0x18u
#define PL_FLAG_LOW_BATT    0x20u
#define PL_FLAG_FAULT       0x40u
/* bit7 reserved, always 0 */

/* ---- Alarm codes -------------------------------------------------------- */
#define PL_ALM_LEVEL_UP    0x01u
#define PL_ALM_LEVEL_DOWN  0x02u
#define PL_ALM_MOVEMENT    0x10u
#define PL_ALM_DETACHED    0x11u
#define PL_ALM_LID_OPENED  0x12u
#define PL_ALM_LOW_BATTERY 0x20u
#define PL_ALM_SENSOR_FAULT 0x21u
#define PL_ALM_LOCKED      0x30u

/* ---- Service modes (0x82) ---------------------------------------------- */
#define PL_SERVICE_RUN  0u
#define PL_SERVICE_SHIP 1u

/* One 10-minute sample as carried on air (5 B). */
typedef struct {
    int16_t rail_cC;
    int16_t air_cC;
    uint8_t flags;
} pl_record_t;

/* 0x03 Status / heartbeat. */
typedef struct {
    uint16_t seq;
    uint32_t time;
    uint8_t battery_pct;
    uint16_t energy_mAh;
    uint16_t fw_version;
    uint8_t reset_count;
    uint8_t tx_fail_count;
    uint8_t link_quality;
} pl_status_t;

/* 0x04 Install. */
typedef struct {
    uint16_t seq;
    uint32_t time;
    uint8_t hw_rev;
    uint16_t fw_version;
    uint8_t attached;
    uint8_t battery_pct;
} pl_install_t;

/* 0x81 Site configuration (18 B on air, D-11). */
typedef struct {
    int16_t l1;                /* alarm level 1, cC */
    int16_t l2;                /* alarm level 2, cC */
    int16_t l3;                /* alarm level 3, cC */
    int16_t hot_threshold;     /* cC */
    uint16_t rate_of_rise;     /* cC per hour; 0 disables rate-based HOT */
    uint16_t hysteresis;       /* cC, applied on the way down, <= 500 */
    uint8_t batch_period_min;  /* NORMAL LTE-M cadence: 10/20/30/60 */
    int16_t cool_threshold;    /* cC, rail below this -> COOL cadence */
    uint8_t cool_batch_min;    /* COOL LTE-M cadence: 60/120/240 */
    uint8_t hot_batch_min;     /* HOT LTE-M cadence: 10/20/30 */
} pl_config_t;

/* Any decoded downlink. */
typedef struct {
    uint8_t type;
    union {
        pl_config_t config;
        uint8_t service_mode;
        uint8_t token[PL_TOKEN_LEN];
        uint32_t time;
    } u;
} pl_downlink_t;

typedef enum {
    PL_DL_OK = 0,
    PL_DL_ERR_EMPTY,   /* NULL or zero-length buffer */
    PL_DL_ERR_TYPE,    /* unknown type byte */
    PL_DL_ERR_LEN,     /* wrong length for the type */
    PL_DL_ERR_INVALID  /* well-formed but fails validation */
} pl_dl_status_t;

/* Build a record flags byte. level is clamped to 0..3. */
uint8_t pl_make_flags(bool attached, bool lid_closed, bool hot, uint8_t level,
                      bool low_batt, bool fault);

/* Return the current value of the shared uplink counter and advance it
 * (wraps 0xFFFF -> 0x0000). */
uint16_t pl_seq_next(uint16_t *counter);

/* Encoders. Each returns the number of bytes written, or 0 if `cap` is too
 * small (or n == 0 for a batch). */
size_t pl_encode_record(uint8_t *out, const pl_record_t *rec);
size_t pl_encode_batch(uint8_t *out, size_t cap, uint16_t seq, uint32_t base_time,
                       uint8_t interval_min, const pl_record_t *recs, uint8_t n);
size_t pl_encode_alarm(uint8_t *out, size_t cap, uint16_t seq, uint32_t time,
                       uint8_t code, const pl_record_t *rec);
size_t pl_encode_status(uint8_t *out, size_t cap, const pl_status_t *s);
size_t pl_encode_install(uint8_t *out, size_t cap, const pl_install_t *s);

/* Platform-side encoder for 0x81 (used by tests and test vectors).
 * Does not validate; returns 0 if cap is too small. */
size_t pl_encode_config(uint8_t *out, size_t cap, const pl_config_t *c);

/* Validation rules for 0x81 (see README). */
bool pl_config_valid(const pl_config_t *c);

/* Decode and validate a downlink. `out` is only meaningful on PL_DL_OK. */
pl_dl_status_t pl_decode_downlink(const uint8_t *buf, size_t len, pl_downlink_t *out);

#endif /* PAYLOAD_H */
