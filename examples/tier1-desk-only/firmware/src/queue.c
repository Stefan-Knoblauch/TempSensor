/*
 * queue.c - Fixed-size FIFO of encoded uplink messages. See queue.h.
 */
#include "queue.h"

#include <string.h>

void mq_init(mq_t *q, mq_msg_t *storage, uint8_t cap)
{
    q->slots = storage;
    q->cap = cap;
    q->head = 0;
    q->count = 0;
    q->dropped = 0;
}

bool mq_push(mq_t *q, const uint8_t *buf, uint8_t len)
{
    bool ok = true;
    mq_msg_t *m;
    if (len == 0u || len > MQ_MSG_MAX || q->cap == 0u) {
        return false;
    }
    if (q->count == q->cap) {
        /* Full: drop the oldest so the newest state still gets out. */
        mq_pop(q);
        if (q->dropped < 0xFFFFu) {
            q->dropped++;
        }
        ok = false;
    }
    m = &q->slots[(uint8_t)((q->head + q->count) % q->cap)];
    m->len = len;
    memcpy(m->buf, buf, len);
    q->count++;
    return ok;
}

const mq_msg_t *mq_peek(const mq_t *q)
{
    return q->count ? &q->slots[q->head] : NULL;
}

void mq_pop(mq_t *q)
{
    if (q->count) {
        q->head = (uint8_t)((q->head + 1u) % q->cap);
        q->count--;
    }
}

uint8_t mq_count(const mq_t *q)
{
    return q->count;
}
