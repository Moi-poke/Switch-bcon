// kb_queue.c -- 8-byte HID keyboard report の順序保持キュー。
#include "kb_queue.h"

#include <string.h>

void kb_queue_init(kb_queue_t *q)
{
    if (q == NULL) {
        return;
    }
    memset(q, 0, sizeof(*q));
}

void kb_queue_clear(kb_queue_t *q)
{
    if (q == NULL) {
        return;
    }
    q->head = 0u;
    q->tail = 0u;
    q->count = 0u;
}

bool kb_queue_push(kb_queue_t *q, const uint8_t report[8], bool drop_oldest)
{
    if (q == NULL || report == NULL) {
        return false;
    }
    if (q->count >= KB_QUEUE_CAP) {
        q->dropped++;
        if (!drop_oldest) {
            return false;
        }
        q->head = (uint8_t)((q->head + 1u) % KB_QUEUE_CAP);
        q->count--;
    }
    memcpy(q->report[q->tail], report, 8u);
    q->tail = (uint8_t)((q->tail + 1u) % KB_QUEUE_CAP);
    q->count++;
    return true;
}

bool kb_queue_pop(kb_queue_t *q, uint8_t out[8])
{
    if (q == NULL || out == NULL || q->count == 0u) {
        return false;
    }
    memcpy(out, q->report[q->head], 8u);
    q->head = (uint8_t)((q->head + 1u) % KB_QUEUE_CAP);
    q->count--;
    return true;
}

uint8_t kb_queue_count(const kb_queue_t *q)
{
    return (q == NULL) ? 0u : q->count;
}

uint32_t kb_queue_dropped(const kb_queue_t *q)
{
    return (q == NULL) ? 0u : q->dropped;
}
