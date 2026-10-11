/*
 * queue.h - Fixed-size FIFO of small, already-encoded uplink messages.
 *
 * Used for the alarm queue and the status/install queue. Storage is
 * supplied by the caller (no allocation). When full, the oldest message is
 * dropped so that the newest state always gets through; drops are counted.
 */
#ifndef QUEUE_H
#define QUEUE_H

#include <stdbool.h>
#include <stdint.h>

#define MQ_MSG_MAX 16u /* largest queued message: status, 15 B */

typedef struct {
    uint8_t len;
    uint8_t buf[MQ_MSG_MAX];
} mq_msg_t;

typedef struct {
    mq_msg_t *slots;
    uint8_t cap;
    uint8_t head;   /* index of oldest message */
    uint8_t count;
    uint16_t dropped;
} mq_t;

void mq_init(mq_t *q, mq_msg_t *storage, uint8_t cap);
/* Returns false if an older message had to be dropped (or len too big,
 * in which case nothing is queued). */
bool mq_push(mq_t *q, const uint8_t *buf, uint8_t len);
const mq_msg_t *mq_peek(const mq_t *q); /* NULL when empty */
void mq_pop(mq_t *q);
uint8_t mq_count(const mq_t *q);

#endif /* QUEUE_H */
