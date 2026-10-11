/*
 * rtsm.h - Rail Temperature Sensor state Machine and scheduler (Tier 1).
 *
 * Usage on a target:
 *     rtsm_init(&sm, &hal, &init);
 *     for (;;) {
 *         rtsm_step(&sm);                        // after every wake-up
 *         if (downlink received) rtsm_on_downlink(&sm, buf, len);
 *         sleep_until(rtsm_next_wakeup(&sm));   // or Hall/IMU/lid IRQ
 *     }
 *
 * rtsm_t is a plain struct so it can be placed statically; callers should
 * treat its fields as private (tests read a few of them).
 */
#ifndef RTSM_H
#define RTSM_H

#include "nvstate.h"
#include "payload.h"
#include "queue.h"
#include "rlog.h"
#include "rtsm_hal.h"

/* ---- Tunable constants (override with -D...) ---------------------------- */
#ifndef RTSM_SAMPLE_PERIOD_S
#define RTSM_SAMPLE_PERIOD_S 600u   /* T1-03 */
#endif
#ifndef RTSM_ATTACH_DEBOUNCE_S
#define RTSM_ATTACH_DEBOUNCE_S 60u  /* SHIP -> COMMISSION */
#endif
#ifndef RTSM_TAMPER_WINDOW_S
#define RTSM_TAMPER_WINDOW_S 1800u  /* ALARM_TAMPER -> LOCKED */
#endif
#ifndef RTSM_TAMPER_SETTLE_S
#define RTSM_TAMPER_SETTLE_S 60u    /* attached + quiet this long = reattached */
#endif
#ifndef RTSM_HOT_HOLD_S
#define RTSM_HOT_HOLD_S 3600u       /* HOT left 1 h after condition clears */
#endif
#ifndef RTSM_HEARTBEAT_S
#define RTSM_HEARTBEAT_S 86400u
#endif
#ifndef RTSM_LOCKED_BEACON_S
#define RTSM_LOCKED_BEACON_S 21600u
#endif
#ifndef RTSM_INSTALL_RESEND_S
#define RTSM_INSTALL_RESEND_S 21600u /* re-send Install while no config */
#endif
#ifndef RTSM_BACKOFF_MIN_S
#define RTSM_BACKOFF_MIN_S 60u
#endif
#ifndef RTSM_BACKOFF_MAX_S
#define RTSM_BACKOFF_MAX_S 600u
#endif
/* Long back-off for status/data sends while the link state is unknown and
 * sends keep failing (R-9): retry after 10, 20, 40, 80, 120 min, then every
 * 120 min. Any successful uplink resets it. Alarms are not subject to it. */
#ifndef RTSM_DATA_BACKOFF_FIRST_S
#define RTSM_DATA_BACKOFF_FIRST_S 600u
#endif
#ifndef RTSM_DATA_BACKOFF_MAX_S
#define RTSM_DATA_BACKOFF_MAX_S 7200u
#endif
/* Seq block reservation: NV holds seq + this; written again when reached. */
#ifndef RTSM_NV_SEQ_BLOCK
#define RTSM_NV_SEQ_BLOCK 256u
#endif
/* Persist the cumulative energy counter after this much consumption. */
#ifndef RTSM_NV_ENERGY_STEP_MAH
#define RTSM_NV_ENERGY_STEP_MAH 50u
#endif
#ifndef RTSM_BACKLOG_MAX_PER_TICK
#define RTSM_BACKLOG_MAX_PER_TICK 6u /* back-fill messages per 10-min tick */
#endif
#ifndef RTSM_LORA_MAX_N
#define RTSM_LORA_MAX_N 8u   /* 49 B: fits EU868 DR0-DR2 (51 B) */
#endif
#ifndef RTSM_LTEM_MAX_N
#define RTSM_LTEM_MAX_N 48u  /* 249 B */
#endif
#ifndef RTSM_LOWBAT_ON_PCT
#define RTSM_LOWBAT_ON_PCT 10u
#endif
#ifndef RTSM_LOWBAT_OFF_PCT
#define RTSM_LOWBAT_OFF_PCT 15u
#endif
#ifndef RAIL_TAU_COMP_S
#define RAIL_TAU_COMP_S 150   /* D-13 rail lag compensation time constant */
#endif
#define RAIL_COMP_DT_MIN_S 300u
#define RAIL_COMP_DT_MAX_S 900u

#define RTSM_ALARM_QUEUE_LEN  32u
#define RTSM_STATUS_QUEUE_LEN 8u
#define RTSM_TX_BUF_SIZE      PL_BATCH_SIZE(RTSM_LTEM_MAX_N)
#define RTSM_INTERVAL_MIN     ((uint8_t)(RTSM_SAMPLE_PERIOD_S / 60u))

typedef enum { RTSM_SKU_LORA = 0, RTSM_SKU_LTEM = 1 } rtsm_sku_t;

typedef enum {
    RTSM_SHIP = 0,
    RTSM_COMMISSION,
    RTSM_RUN,
    RTSM_HOT,
    RTSM_ALARM_TAMPER,
    RTSM_LOCKED
} rtsm_state_t;

/* LTE-M reporting cadence (D-11). Informational for LoRa. */
typedef enum { RTSM_CAD_COOL = 0, RTSM_CAD_NORMAL, RTSM_CAD_HOT } rtsm_cadence_t;

typedef enum {
    RTSM_DL_APPLIED = 0,  /* accepted and acted upon */
    RTSM_DL_IGNORED,      /* valid but not applicable in the current state */
    RTSM_DL_REJECTED      /* malformed, invalid config or wrong unlock token */
} rtsm_dl_result_t;

/* Identity / provisioning data, normally from the factory NV area.
 * reset_count, seq, config, state and the active unlock token are kept by
 * the core itself in nvstate (hal->nv_read / nv_write). */
typedef struct {
    rtsm_sku_t sku;
    uint8_t hw_rev;
    uint16_t fw_version;
    /* Factory default token: used only when nvstate holds no valid record
     * (first boot or both slots corrupt); afterwards the NV copy rules. */
    uint8_t unlock_token[PL_TOKEN_LEN];
} rtsm_init_t;

typedef struct {
    const rtsm_hal_t *hal;
    rtsm_init_t id;

    rtsm_state_t state;     /* what the unit is doing now */
    rtsm_state_t op_state;  /* COMMISSION/RUN/HOT underneath ALARM_TAMPER */
    bool have_config;
    pl_config_t cfg;
    uint16_t seq;           /* shared uplink sequence counter */
    uint32_t time_offset;   /* device Unix time = HAL now + offset (mod 2^32) */

    /* SHIP */
    bool ship_armed;        /* false after service mode until a detach is seen */
    bool attach_pending;
    uint32_t attach_since;

    /* Schedules (HAL time) */
    uint32_t anchor;        /* commissioning start; 10-min grid origin */
    uint32_t next_sample;
    uint32_t next_status;
    uint32_t next_install;
    uint32_t next_beacon;   /* LOCKED */

    /* Transmission */
    uint32_t next_tx_ok;    /* retry back-off for queued messages */
    uint16_t backoff_s;
    bool tx_kick;           /* new alarm: try now */
    uint8_t tx_fail_count;  /* since last status message, saturating */
    uint32_t data_backoff_s;/* long status/data back-off, 0 = none */
    uint32_t data_next_ok;  /* no status/data attempt before this (link unknown) */
    uint32_t send_attempts_total;
    uint32_t send_failures_total;
    uint8_t backlog_budget; /* back-fill messages left in this tick */
    uint32_t pending_from;  /* records >= this abs are not yet due (LTE-M) */
    bool data_link_lost;    /* last data attempt failed: next tick sends newest first */

    /* Measurement / alarm logic */
    pl_record_t last_rec;
    bool have_prev_raw;
    int16_t prev_raw_rail;
    uint32_t prev_raw_time;
    uint8_t level;
    rtsm_cadence_t cadence;
    bool hot_clear_pending;
    uint32_t hot_clear_since;
    bool low_batt;
    bool fault_active;

    /* Tamper */
    bool lid_was_closed;
    uint32_t tamper_deadline;
    bool settle_ok;
    uint32_t settle_since;
    bool sent_move;
    bool sent_detach;

    /* Persistence (R-9) */
    nvs_t nv;
    uint32_t energy_base;   /* cumulative mAh before this boot (from NV) */

    rlog_t log;
    mq_t q_alarm;
    mq_t q_status;
    mq_msg_t alarm_store[RTSM_ALARM_QUEUE_LEN];
    mq_msg_t status_store[RTSM_STATUS_QUEUE_LEN];
    uint8_t txbuf[RTSM_TX_BUF_SIZE];
} rtsm_t;

void rtsm_init(rtsm_t *r, const rtsm_hal_t *hal, const rtsm_init_t *init);
void rtsm_step(rtsm_t *r);
rtsm_dl_result_t rtsm_on_downlink(rtsm_t *r, const uint8_t *buf, size_t len);
/* Earliest HAL time at which rtsm_step() has timed work to do. Hall, IMU,
 * lid and downlink interrupts must also wake the MCU. */
uint32_t rtsm_next_wakeup(const rtsm_t *r);
uint32_t rtsm_device_time(const rtsm_t *r);

/* Replace the unlock token (provisioning / rotation); persisted at once. */
void rtsm_set_unlock_token(rtsm_t *r, const uint8_t token[PL_TOKEN_LEN]);
/* Cumulative energy: NV total from earlier boots + HAL counter (mAh). */
uint32_t rtsm_energy_total(const rtsm_t *r);

/* D-13 rail lag compensation (exposed for unit tests):
 * raw + RAIL_TAU_COMP_S * (raw - prev) / dt, rounded to nearest, only when
 * both values are valid and 300 <= dt <= 900; otherwise raw. */
int16_t rtsm_rail_compensate(int16_t raw, int16_t prev, uint32_t dt_s);

const char *rtsm_state_name(rtsm_state_t s);

#endif /* RTSM_H */
