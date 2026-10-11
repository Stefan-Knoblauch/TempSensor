/*
 * rlog.h - Persistent ring log of 10-minute samples.
 *
 * Records are addressed by a monotonically increasing absolute index
 * ("abs"); the storage slot is abs % capacity. Each slot carries its own
 * sent/unsent status (per-record sent flag scheme, see README). A scan
 * pointer remembers the oldest record that may still be unsent so the
 * back-fill does not rescan the whole ring.
 *
 * When the ring is full the oldest record is overwritten; if it was still
 * unsent it is counted in `lost_unsent` (only happens after > 32 days
 * without a link).
 */
#ifndef RLOG_H
#define RLOG_H

#include "rtsm_hal.h"

#ifndef RLOG_CAPACITY
#define RLOG_CAPACITY 4608u /* 32 days at 10 min */
#endif

typedef struct {
    const rtsm_hal_t *hal;
    uint16_t cap;
    uint32_t first;  /* abs of oldest stored record */
    uint32_t next;   /* abs the next append will get */
    uint32_t scan;   /* no unsent record exists below this abs */
    uint32_t lost_unsent;
} rlog_t;

/* Attach to the HAL storage and recover indices by scanning the slots.
 * An all-erased store gives an empty log. */
void rlog_init(rlog_t *l, const rtsm_hal_t *hal, uint16_t cap);
bool rlog_append(rlog_t *l, const rtsm_log_entry_t *e, uint32_t *abs_out);
bool rlog_get(const rlog_t *l, uint32_t abs, rtsm_log_entry_t *out);
bool rlog_mark_sent(rlog_t *l, uint32_t abs);
bool rlog_is_unsent(const rlog_t *l, uint32_t abs);
/* abs of the oldest unsent record, or l->next if none. */
uint32_t rlog_first_unsent(rlog_t *l);
uint32_t rlog_count(const rlog_t *l);
uint32_t rlog_count_unsent(const rlog_t *l); /* full scan; for tests/diagnostics */

#endif /* RLOG_H */
