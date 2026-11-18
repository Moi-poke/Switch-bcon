// test_kb_queue.c -- keyboard report FIFO (UART boundary -> USB) host tests.
#include <stdio.h>
#include <string.h>

#include "../../src/proto/kb_queue.h"

static int fails = 0;
#define CHECK(c, msg) do { \
    if (c) { printf("  PASS %s\n", msg); } \
    else { printf("  FAIL %s\n", msg); fails++; } \
} while (0)

static void rep(uint8_t out[8], uint8_t key)
{
    memset(out, 0, 8);
    out[2] = key;
}

int main(void)
{
    kb_queue_t q;
    uint8_t out[8];
    uint8_t r[8];

    printf("[1] initial state is empty\n");
    kb_queue_init(&q);
    CHECK(kb_queue_count(&q) == 0, "count 0");
    CHECK(!kb_queue_pop(&q, out), "pop empty -> false");
    CHECK(kb_queue_dropped(&q) == 0, "dropped 0");

    printf("[2] same-tick press -> neutral preserves order\n");
    rep(r, 0x04); CHECK(kb_queue_push(&q, r, false), "push press");
    rep(r, 0x00); CHECK(kb_queue_push(&q, r, false), "push neutral");
    CHECK(kb_queue_count(&q) == 2, "count 2");
    CHECK(kb_queue_pop(&q, out) && out[2] == 0x04, "pop press first");
    CHECK(kb_queue_pop(&q, out) && out[2] == 0x00, "pop neutral second");
    CHECK(!kb_queue_pop(&q, out), "empty after both");

    printf("[3] full + drop newest (receive ring policy)\n");
    kb_queue_init(&q);
    for (uint8_t i = 0; i < KB_QUEUE_CAP; i++) {
        rep(r, (uint8_t)(0x10u + i));
        CHECK(kb_queue_push(&q, r, false), "push into free slot");
    }
    rep(r, 0xFF);
    CHECK(!kb_queue_push(&q, r, false), "full -> reject newest");
    CHECK(kb_queue_dropped(&q) == 1, "dropped counted");
    CHECK(kb_queue_count(&q) == KB_QUEUE_CAP, "count stays capacity");
    CHECK(kb_queue_pop(&q, out) && out[2] == 0x10, "oldest retained");

    printf("[4] full + drop oldest (USB FIFO policy)\n");
    kb_queue_init(&q);
    for (uint8_t i = 0; i < KB_QUEUE_CAP; i++) {
        rep(r, (uint8_t)(0x10u + i));
        (void)kb_queue_push(&q, r, true);
    }
    rep(r, 0xFF);
    CHECK(kb_queue_push(&q, r, true), "full -> accepts newest");
    CHECK(kb_queue_dropped(&q) == 1, "dropped counted");
    CHECK(kb_queue_pop(&q, out) && out[2] == 0x11, "oldest dropped");
    CHECK(kb_queue_count(&q) == KB_QUEUE_CAP - 1u, "one slot free after pops");

    printf("[5] clear keeps drop counter for diagnostics\n");
    kb_queue_clear(&q);
    CHECK(kb_queue_count(&q) == 0, "count 0 after clear");
    CHECK(kb_queue_dropped(&q) == 1, "dropped survives clear");

    printf("[6] NULL-safe helpers\n");
    kb_queue_init(NULL);
    kb_queue_clear(NULL);
    CHECK(!kb_queue_push(NULL, r, true), "push NULL false");
    CHECK(!kb_queue_pop(NULL, out), "pop NULL false");
    CHECK(kb_queue_count(NULL) == 0 && kb_queue_dropped(NULL) == 0,
          "count/dropped NULL 0");

    printf("\nRESULT: %s (%d failures)\n",
           fails == 0 ? "ALL PASS" : "HAS FAILURES", fails);
    return fails;
}
