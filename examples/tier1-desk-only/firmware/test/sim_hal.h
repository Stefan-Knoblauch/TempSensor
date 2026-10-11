/*
 * sim_hal.h - Simulated HAL for host tests: controllable clock, sensors,
 * Hall/lid/IMU, battery and link; captures every accepted uplink.
 */
#ifndef SIM_HAL_H
#define SIM_HAL_H

#include "rtsm.h"

#define SIM_MAX_MSGS 20000u
#define SIM_MAX_ATTEMPT_LOG 256u

typedef struct {
    uint32_t t;   /* HAL time when accepted */
    uint16_t len;
    uint8_t buf[RTSM_TX_BUF_SIZE];
} sim_msg_t;

typedef struct {
    uint32_t now;
    int16_t rail;
    int16_t air;
    bool hall;
    bool lid;
    bool imu_event;
    uint8_t batt;
    uint16_t energy;
    uint8_t lq;
    bool link_up;

    uint32_t tx_attempts;
    uint32_t attempt_t[SIM_MAX_ATTEMPT_LOG]; /* time of the first attempts */
    uint32_t nmsg;
    sim_msg_t *msgs; /* points at a static buffer in sim_hal.c */

    rtsm_log_entry_t store[RLOG_CAPACITY];
    rtsm_hal_t hal;
} sim_t;

void sim_init(sim_t *s, uint32_t t0);

#endif /* SIM_HAL_H */
