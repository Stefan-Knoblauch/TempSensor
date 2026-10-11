/*
 * rtsm_hal.h - Hardware abstraction for the Tier 1 firmware core.
 *
 * Every hardware access made by the portable core goes through this struct
 * of function pointers. A port (Zephyr on STM32WL / nRF91) or the host test
 * simulator fills it in. `ctx` is passed back unchanged to every call.
 *
 * Optional functions may be NULL; this is noted per entry.
 */
#ifndef RTSM_HAL_H
#define RTSM_HAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* One slot of the persistent sample log (stored by the HAL, e.g. SPI NOR).
 * `status` is chosen so that marking a record sent only clears bits
 * (0xA5 -> 0x05), which a NOR flash can do in place without an erase. */
#define RLOG_ST_ERASED 0xFFu
#define RLOG_ST_UNSENT 0xA5u
#define RLOG_ST_SENT   0x05u

typedef struct {
    uint32_t time;    /* Unix seconds (device time, after time sync) */
    int16_t rail_cC;  /* lag-compensated rail temperature or 0x8000 */
    int16_t air_cC;   /* near-rail air temperature or 0x8000 */
    uint8_t flags;    /* PL_FLAG_* as sent on air */
    uint8_t status;   /* RLOG_ST_* */
} rtsm_log_entry_t;

typedef struct rtsm_hal {
    void *ctx;

    /* RTC, Unix seconds. Must be monotonic between time syncs; the core
     * applies 0x84 time sync as an offset and never sets the RTC. */
    uint32_t (*now)(void *ctx);

    /* Temperatures in cC; return 0x8000 (PL_TEMP_NA) on sensor fault. */
    int16_t (*read_rail_cC)(void *ctx);
    int16_t (*read_air_cC)(void *ctx);

    bool (*hall_attached)(void *ctx);  /* magnet/rail present */
    bool (*lid_closed)(void *ctx);     /* tamper microswitch */
    bool (*imu_movement)(void *ctx);   /* latched event flag; read clears it */
    uint8_t (*battery_pct)(void *ctx); /* 0..100 */
    /* Coulomb counter since power-up (restarts at 0 after a reset); the
     * core adds the total persisted in nvstate. Optional (NULL -> 0). */
    uint16_t (*energy_mAh)(void *ctx);
    uint8_t (*link_quality)(void *ctx);/* 0..255; optional (NULL -> 0) */

    /* Transmit one uplink. Returns true only when the bearer accepted it
     * (LoRaWAN: TX done; LTE-M: CoAP ACK / MQTT-SN PUBACK). */
    bool (*radio_send)(void *ctx, const uint8_t *buf, size_t len);

    /* Persistent log storage, slot = 0 .. capacity-1. */
    bool (*log_read)(void *ctx, uint16_t slot, rtsm_log_entry_t *out);
    bool (*log_write)(void *ctx, uint16_t slot, const rtsm_log_entry_t *e);

    /* Network availability hint, e.g. LTE-M "registered" or LoRaWAN
     * "joined and duty cycle free": 1 yes, 0 no, -1 unknown. Optional
     * (NULL -> unknown). With 0 the core makes no status/data attempt;
     * with -1 it uses the long data back-off (RTSM_DATA_BACKOFF_*). */
    int (*link_available)(void *ctx);

    /* Persistent state record (nvstate.h), two slots: 0 = A, 1 = B. Each
     * call transfers one whole record of `len` bytes; nv_write must erase
     * the slot first if the medium needs it. Optional (both NULL -> nothing
     * persists; every boot starts from factory defaults). */
    bool (*nv_read)(void *ctx, uint8_t slot, uint8_t *buf, size_t len);
    bool (*nv_write)(void *ctx, uint8_t slot, const uint8_t *buf, size_t len);
} rtsm_hal_t;

#endif /* RTSM_HAL_H */
