// test_baud.c -- UART baud hunt pure-logic host tests (task-16 / B-0).
// No Pico SDK needed. CTest name: baud.
// Covers: rate table (B §3 shared), sweep order (slot0=last-good +
// descending, build-max capped), 2-frame lock rule.
#include <stdio.h>
#include <string.h>
#include "../../src/proto/baud.h"

static int fails = 0;
#define CHECK(c, msg) do { \
    if (c) { printf("  PASS %s\n", msg); } \
    else { printf("  FAIL %s\n", msg); fails++; } \
} while (0)

int main(void) {
    printf("[0] rate table index->bps (B §3 shared)\n");
    CHECK(baud_bps(0) == 115200u, "idx0=115200");
    CHECK(baud_bps(1) == 460800u, "idx1=460800");
    CHECK(baud_bps(2) == 921600u, "idx2=921600");
    CHECK(baud_bps(3) == 1000000u, "idx3=1000000 default");
    CHECK(baud_bps(4) == 2000000u, "idx4=2000000 opt-in");

    printf("[1] invalid index rejected\n");
    CHECK(!baud_idx_valid(5) && baud_bps(5) == 0u, "idx5 invalid");
    CHECK(!baud_idx_valid(255) && baud_bps(255) == 0u, "idx255 invalid");
    CHECK(baud_idx_valid(0) && baud_idx_valid(4), "0..4 valid");

    printf("[2] sweep order: slot0=last-good, then descending, max-capped\n");
    {
        uint8_t out[BAUD_N];
        uint8_t n = baud_sweep_order(0, 1000000u, out);
        CHECK(n == 4 && out[0] == 0 && out[1] == 3 &&
              out[2] == 2 && out[3] == 1, "last-good 115200 first");
    }
    {
        uint8_t out[BAUD_N];
        uint8_t n = baud_sweep_order(3, 1000000u, out);
        CHECK(n == 4 && out[0] == 3 && out[1] == 2 &&
              out[2] == 1 && out[3] == 0, "last-good 1M stays slot0, no dup");
    }
    {
        uint8_t out[BAUD_N];
        uint8_t n = baud_sweep_order(4, 1000000u, out);
        CHECK(n == 4 && out[0] == 3 && out[3] == 0,
              "last-good 2M over max -> descending from 1M");
    }
    {
        uint8_t out[BAUD_N];
        uint8_t n = baud_sweep_order(9, 1000000u, out);
        CHECK(n == 4 && out[0] == 3 && out[3] == 0,
              "last-good invalid -> descending from max");
    }
    {
        uint8_t out[BAUD_N];
        uint8_t n = baud_sweep_order(0, 115200u, out);
        CHECK(n == 1 && out[0] == 0, "derated build: single slot");
    }
    {
        uint8_t out[BAUD_N];
        uint8_t n = baud_sweep_order(2, 460800u, out);
        CHECK(n == 2 && out[0] == 1 && out[1] == 0,
              "last-good over max demoted, rest descending");
    }

    printf("[3] lock needs 2 consecutive valid frames\n");
    {
        baud_lock_t l;
        baud_lock_reset(&l);
        CHECK(!baud_lock_feed(&l, true), "1st valid: not locked");
        CHECK(baud_lock_feed(&l, true), "2nd consecutive: locked");
    }
    {
        baud_lock_t l;
        baud_lock_reset(&l);
        (void)baud_lock_feed(&l, true);
        CHECK(!baud_lock_feed(&l, false), "gap resets");
        CHECK(!baud_lock_feed(&l, true), "needs 2 again after reset");
        CHECK(baud_lock_feed(&l, true), "2nd consecutive: locked");
    }

    if (fails == 0) printf("baud: ALL PASS\n");
    else printf("baud: %d FAILURES\n", fails);
    return fails != 0;
}
