/*
 * sim_hal.c - Simulated HAL implementation. See sim_hal.h.
 */
#include "sim_hal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static sim_msg_t g_msgs[SIM_MAX_MSGS];

static uint32_t s_now(void *c) { return ((sim_t *)c)->now; }
static int16_t s_rail(void *c) { return ((sim_t *)c)->rail; }
static int16_t s_air(void *c) { return ((sim_t *)c)->air; }
static bool s_hall(void *c) { return ((sim_t *)c)->hall; }
static bool s_lid(void *c) { return ((sim_t *)c)->lid; }
static uint8_t s_batt(void *c) { return ((sim_t *)c)->batt; }
static uint16_t s_energy(void *c) { return ((sim_t *)c)->energy; }
static uint8_t s_lq(void *c) { return ((sim_t *)c)->lq; }

static bool s_imu(void *c)
{
    sim_t *s = (sim_t *)c;
    bool v = s->imu_event;
    s->imu_event = false; /* read clears the latched event */
    return v;
}

static bool s_send(void *c, const uint8_t *buf, size_t len)
{
    sim_t *s = (sim_t *)c;
    if (s->tx_attempts < SIM_MAX_ATTEMPT_LOG) {
        s->attempt_t[s->tx_attempts] = s->now;
    }
    s->tx_attempts++;
    if (!s->link_up) {
        return false;
    }
    if (s->nmsg >= SIM_MAX_MSGS || len > RTSM_TX_BUF_SIZE) {
        fprintf(stderr, "sim: message capture overflow\n");
        exit(2);
    }
    s->msgs[s->nmsg].t = s->now;
    s->msgs[s->nmsg].len = (uint16_t)len;
    memcpy(s->msgs[s->nmsg].buf, buf, len);
    s->nmsg++;
    return true;
}

static bool s_log_read(void *c, uint16_t slot, rtsm_log_entry_t *out)
{
    sim_t *s = (sim_t *)c;
    if (slot >= RLOG_CAPACITY) return false;
    *out = s->store[slot];
    return true;
}

static bool s_log_write(void *c, uint16_t slot, const rtsm_log_entry_t *e)
{
    sim_t *s = (sim_t *)c;
    if (slot >= RLOG_CAPACITY) return false;
    s->store[slot] = *e;
    return true;
}

void sim_init(sim_t *s, uint32_t t0)
{
    uint16_t i;
    memset(s, 0, sizeof *s);
    s->now = t0;
    s->rail = 3000;
    s->air = 2500;
    s->hall = false;
    s->lid = true;
    s->batt = 90;
    s->energy = 120;
    s->lq = 180;
    s->link_up = true;
    s->msgs = g_msgs;
    for (i = 0; i < RLOG_CAPACITY; i++) {
        memset(&s->store[i], 0xFF, sizeof s->store[i]); /* erased flash */
    }
    s->hal.ctx = s;
    s->hal.now = s_now;
    s->hal.read_rail_cC = s_rail;
    s->hal.read_air_cC = s_air;
    s->hal.hall_attached = s_hall;
    s->hal.lid_closed = s_lid;
    s->hal.imu_movement = s_imu;
    s->hal.battery_pct = s_batt;
    s->hal.energy_mAh = s_energy;
    s->hal.link_quality = s_lq;
    s->hal.radio_send = s_send;
    s->hal.log_read = s_log_read;
    s->hal.log_write = s_log_write;
}
