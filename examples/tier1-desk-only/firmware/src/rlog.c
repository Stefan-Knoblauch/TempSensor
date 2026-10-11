/*
 * rlog.c - Persistent ring log of samples with per-record sent flags.
 * See rlog.h and README ("Log and sent-tracking scheme").
 */
#include "rlog.h"

static uint16_t slot_of(const rlog_t *l, uint32_t abs)
{
    return (uint16_t)(abs % l->cap);
}

static bool status_valid(uint8_t st)
{
    return st == RLOG_ST_UNSENT || st == RLOG_ST_SENT;
}

void rlog_init(rlog_t *l, const rtsm_hal_t *hal, uint16_t cap)
{
    uint16_t s;
    uint16_t valid = 0;
    uint16_t newest = 0;
    uint32_t newest_t = 0;
    bool any = false;
    rtsm_log_entry_t e;

    l->hal = hal;
    l->cap = cap;
    l->first = 0;
    l->next = 0;
    l->scan = 0;
    l->lost_unsent = 0;

    /* Recovery after reset: records are written in slot order starting at
     * slot 0, so a partly filled ring holds slots 0..valid-1, and a full
     * ring's newest record is the one with the latest timestamp. */
    for (s = 0; s < cap; s++) {
        if (!hal->log_read(hal->ctx, s, &e) || !status_valid(e.status)) {
            continue;
        }
        valid++;
        if (!any || e.time >= newest_t) {
            any = true;
            newest = s;
            newest_t = e.time;
        }
    }
    if (valid == cap) {
        l->first = (uint32_t)((newest + 1u) % cap);
        l->next = l->first + cap;
    } else {
        l->first = 0;
        l->next = valid;
    }
    l->scan = l->first;
}

bool rlog_append(rlog_t *l, const rtsm_log_entry_t *e, uint32_t *abs_out)
{
    rtsm_log_entry_t w = *e;
    if (l->next - l->first == l->cap) {
        /* Ring full: the oldest record is overwritten. */
        if (rlog_is_unsent(l, l->first)) {
            l->lost_unsent++;
        }
        l->first++;
        if (l->scan < l->first) {
            l->scan = l->first;
        }
    }
    w.status = RLOG_ST_UNSENT;
    if (!l->hal->log_write(l->hal->ctx, slot_of(l, l->next), &w)) {
        return false;
    }
    if (abs_out) {
        *abs_out = l->next;
    }
    l->next++;
    return true;
}

bool rlog_get(const rlog_t *l, uint32_t abs, rtsm_log_entry_t *out)
{
    if (abs < l->first || abs >= l->next) {
        return false;
    }
    return l->hal->log_read(l->hal->ctx, slot_of(l, abs), out) && status_valid(out->status);
}

bool rlog_is_unsent(const rlog_t *l, uint32_t abs)
{
    rtsm_log_entry_t e;
    return rlog_get(l, abs, &e) && e.status == RLOG_ST_UNSENT;
}

bool rlog_mark_sent(rlog_t *l, uint32_t abs)
{
    rtsm_log_entry_t e;
    if (!rlog_get(l, abs, &e)) {
        return false;
    }
    e.status = RLOG_ST_SENT; /* bit-clear only: NOR-friendly */
    return l->hal->log_write(l->hal->ctx, slot_of(l, abs), &e);
}

uint32_t rlog_first_unsent(rlog_t *l)
{
    if (l->scan < l->first) {
        l->scan = l->first;
    }
    while (l->scan < l->next && !rlog_is_unsent(l, l->scan)) {
        l->scan++;
    }
    return l->scan;
}

uint32_t rlog_count(const rlog_t *l)
{
    return l->next - l->first;
}

uint32_t rlog_count_unsent(const rlog_t *l)
{
    uint32_t a;
    uint32_t n = 0;
    for (a = l->first; a < l->next; a++) {
        if (rlog_is_unsent(l, a)) {
            n++;
        }
    }
    return n;
}
